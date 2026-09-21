// Copyright (c) 2026 The Bitcoin SV developers
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "compat.h"
#include "config.h"
#include "net/net.h"
#include "net/net_message.h"
#include "net/socket_wait_set.h"
#include "net/stream.h"
#include "netmessagemaker.h"
#include "protocol.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "test/testutil.h"

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
    using Flags = SocketWaitSet::Flags;

    constexpr Flags RECV_SEND { SocketWaitSet::RECV | SocketWaitSet::SEND };
    constexpr Flags RECV_ERR { SocketWaitSet::RECV | SocketWaitSet::ERR };
    constexpr Flags ALL { SocketWaitSet::RECV | SocketWaitSet::SEND | SocketWaitSet::ERR };

    CNodePtr MakeDummyNode(const CAddress& dummyAddr, CConnman::CAsyncTaskPool& taskPool)
    {
        return CNode::Make(
            0,
            NODE_NETWORK,
            0,
            INVALID_SOCKET,
            dummyAddr,
            0u,
            0u,
            taskPool,
            "",
            true);
    }

#ifndef WIN32
    // Placeholder descriptors held open to push the next free descriptor
    // number past FD_SETSIZE. Closed on destruction so that a failing
    // assertion cannot leak them into the rest of the test binary.
    class LowDescriptorHolder
    {
      public:
        LowDescriptorHolder() = default;
        ~LowDescriptorHolder() { Release(); }
        LowDescriptorHolder(const LowDescriptorHolder&) = delete;
        LowDescriptorHolder& operator=(const LowDescriptorHolder&) = delete;
        LowDescriptorHolder(LowDescriptorHolder&&) = delete;
        LowDescriptorHolder& operator=(LowDescriptorHolder&&) = delete;

        // Returns false if the process rlimit will not allow it.
        bool Occupy(int firstFreeAtLeast)
        {
            struct rlimit lim {};
            if(getrlimit(RLIMIT_NOFILE, &lim) != 0)
            {
                return false;
            }
            const rlim_t needed { static_cast<rlim_t>(firstFreeAtLeast) + 64 };
            if(lim.rlim_max != RLIM_INFINITY && lim.rlim_max < needed)
            {
                return false;
            }
            if(lim.rlim_cur < needed)
            {
                lim.rlim_cur = needed;
                if(setrlimit(RLIMIT_NOFILE, &lim) != 0)
                {
                    return false;
                }
            }
            for(;;)
            {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
                const int fd { open("/dev/null", O_RDONLY) };
                if(fd < 0)
                {
                    return false;
                }
                mFds.push_back(fd);
                if(fd + 1 >= firstFreeAtLeast)
                {
                    return true;
                }
            }
        }

        void Release()
        {
            for(const int fd : mFds)
            {
                close(fd);
            }
            mFds.clear();
        }

      private:
        std::vector<int> mFds {};
    };
#endif
}

BOOST_FIXTURE_TEST_SUITE(TestNetSocketWaitSet, TestingSetup)

BOOST_AUTO_TEST_CASE(SocketWaitSetBasics)
{
    SocketWaitSet waitSet {};
    BOOST_CHECK(waitSet.Entries().empty());

    // Unknown sockets read as nothing wanted, nothing ready.
    BOOST_CHECK_EQUAL(waitSet.Wanted(7), SocketWaitSet::NONE);
    BOOST_CHECK_EQUAL(waitSet.Ready(7), SocketWaitSet::NONE);
    BOOST_CHECK(!waitSet.IsReady(7, ALL));

    // An invalid socket is ignored.
    waitSet.Add(INVALID_SOCKET, ALL);
    BOOST_CHECK(waitSet.Entries().empty());

    // Registration and flag merging.
    waitSet.Add(7, SocketWaitSet::RECV);
    waitSet.Add(9, SocketWaitSet::SEND);
    waitSet.Add(7, SocketWaitSet::ERR);
    BOOST_CHECK_EQUAL(waitSet.Entries().size(), 2U);
    BOOST_CHECK_EQUAL(waitSet.Wanted(7), RECV_ERR);
    BOOST_CHECK_EQUAL(waitSet.Wanted(9), SocketWaitSet::SEND);
    BOOST_CHECK_EQUAL(waitSet.Ready(7), SocketWaitSet::NONE);

    // Readiness for registered sockets only.
    waitSet.SetReady(7, SocketWaitSet::RECV);
    waitSet.SetReady(7, SocketWaitSet::ERR);
    waitSet.SetReady(11, ALL);
    BOOST_CHECK_EQUAL(waitSet.Ready(7), RECV_ERR);
    BOOST_CHECK(waitSet.IsReady(7, SocketWaitSet::RECV));
    BOOST_CHECK(waitSet.IsReady(7, SocketWaitSet::ERR));
    BOOST_CHECK(!waitSet.IsReady(7, SocketWaitSet::SEND));
    BOOST_CHECK_EQUAL(waitSet.Ready(9), SocketWaitSet::NONE);
    BOOST_CHECK_EQUAL(waitSet.Ready(11), SocketWaitSet::NONE);
    BOOST_CHECK_EQUAL(waitSet.Entries().size(), 2U);

    // Entries are exposed read-only, in registration order, and readiness is
    // written back by position; an out-of-range position is ignored.
    BOOST_REQUIRE_EQUAL(waitSet.Entries().size(), 2U);
    BOOST_CHECK_EQUAL(waitSet.Entries()[0].socket, 7);
    BOOST_CHECK_EQUAL(waitSet.Entries()[1].socket, 9);
    waitSet.SetReadyAt(1, SocketWaitSet::SEND);
    waitSet.SetReadyAt(2, ALL);
    BOOST_CHECK(waitSet.IsReady(9, SocketWaitSet::SEND));
    BOOST_CHECK_EQUAL(waitSet.Ready(9), SocketWaitSet::SEND);
    BOOST_CHECK_EQUAL(waitSet.Ready(7), RECV_ERR);

    // Clear forgets everything.
    waitSet.Clear();
    BOOST_CHECK(waitSet.Entries().empty());
    BOOST_CHECK_EQUAL(waitSet.Wanted(7), SocketWaitSet::NONE);
    BOOST_CHECK_EQUAL(waitSet.Ready(9), SocketWaitSet::NONE);
}

BOOST_AUTO_TEST_CASE(FlagsOperatorsAndNames)
{
    // Scoped enum with bitwise combination.
    Flags f { SocketWaitSet::NONE };
    f |= SocketWaitSet::RECV;
    BOOST_CHECK_EQUAL(f, SocketWaitSet::RECV);
    f |= SocketWaitSet::ERR;
    BOOST_CHECK_EQUAL(f, RECV_ERR);
    BOOST_CHECK_EQUAL(f & SocketWaitSet::SEND, SocketWaitSet::NONE);
    BOOST_CHECK_EQUAL(f & SocketWaitSet::ERR, SocketWaitSet::ERR);

    // Names via enum_cast, including the printed form of a combination.
    BOOST_CHECK_EQUAL(enum_cast<std::string>(SocketWaitSet::SEND), "SEND");
    BOOST_CHECK_EQUAL(enum_cast<Flags>("ERR"), SocketWaitSet::ERR);
    const auto printed { [](Flags flags) { std::ostringstream os {}; os << flags; return os.str(); } };
    BOOST_CHECK_EQUAL(printed(SocketWaitSet::NONE), "NONE");
    BOOST_CHECK_EQUAL(printed(SocketWaitSet::RECV), "RECV");
    BOOST_CHECK_EQUAL(printed(RECV_ERR), "RECV|ERR");
    BOOST_CHECK_EQUAL(printed(ALL), "RECV|SEND|ERR");
}

BOOST_AUTO_TEST_CASE(IsSelectableSocketMatchesBackend)
{
    BOOST_CHECK(IsSelectableSocket(5));
#if defined(USE_POLL) || defined(WIN32)
    // No socket number limit with poll() (or the Windows fd_set).
    BOOST_CHECK(IsSelectableSocket(2000));
    BOOST_CHECK(IsSelectableSocket(FD_SETSIZE));
#else
    BOOST_CHECK(!IsSelectableSocket(2000));
    BOOST_CHECK(!IsSelectableSocket(FD_SETSIZE));
    BOOST_CHECK(IsSelectableSocket(FD_SETSIZE - 1));
#endif
}

#ifdef USE_POLL
BOOST_AUTO_TEST_CASE(PollEventMapping)
{
    // Wanted flags -> poll(2) request bits. Error conditions are always
    // reported by poll(), so ERR requests nothing extra.
    BOOST_CHECK_EQUAL(PollRequestedEvents(SocketWaitSet::NONE), 0);
    BOOST_CHECK_EQUAL(PollRequestedEvents(SocketWaitSet::ERR), 0);
    BOOST_CHECK_EQUAL(PollRequestedEvents(SocketWaitSet::RECV), POLLIN);
    BOOST_CHECK_EQUAL(PollRequestedEvents(SocketWaitSet::SEND), POLLOUT);
    BOOST_CHECK_EQUAL(PollRequestedEvents(RECV_SEND), POLLIN | POLLOUT);
    BOOST_CHECK_EQUAL(PollRequestedEvents(ALL), POLLIN | POLLOUT);

    // poll(2) result bits -> ready flags.
    BOOST_CHECK_EQUAL(PollReadyFlags(0), SocketWaitSet::NONE);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLIN), SocketWaitSet::RECV);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLOUT), SocketWaitSet::SEND);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLERR), SocketWaitSet::ERR);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLHUP), SocketWaitSet::ERR);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLNVAL), SocketWaitSet::ERR);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLIN | POLLHUP), RECV_ERR);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLOUT | POLLERR | POLLHUP), SocketWaitSet::SEND | SocketWaitSet::ERR);
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLIN | POLLOUT), RECV_SEND);
    // Urgent data is deliberately ignored.
    BOOST_CHECK_EQUAL(PollReadyFlags(POLLPRI), SocketWaitSet::NONE);
}
#endif

#ifndef WIN32
// Drive a Stream over one end of a socketpair whose descriptors are numbered
// past FD_SETSIZE, the situation the poll() port exists for.
BOOST_AUTO_TEST_CASE(StreamOverHighNumberedSocketpair)
{
    LowDescriptorHolder placeholders {};
    if(!placeholders.Occupy(FD_SETSIZE + 100))
    {
        BOOST_TEST_MESSAGE("RLIMIT_NOFILE too low to number sockets past FD_SETSIZE; skipping");
        return;
    }

    std::array<int, 2> fds {};
    BOOST_REQUIRE_EQUAL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);
    for(const int fd : fds)
    {
        BOOST_CHECK_GE(fd, FD_SETSIZE);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        BOOST_REQUIRE_NE(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK), -1);
    }
    // Placeholders have done their job once the pair is numbered.
    placeholders.Release();

    const Config& config { GlobalConfig::GetConfig() };
    CConnman::CAsyncTaskPool asyncTaskPool { GlobalConfig::GetConfig() };
    CNodePtr sender { MakeDummyNode(CAddress{ ip(0xa0b0c001), NODE_NONE }, asyncTaskPool) };
    CNodePtr receiver { MakeDummyNode(CAddress{ ip(0xa0b0c002), NODE_NONE }, asyncTaskPool) };

    // Streams take ownership of the descriptors and close them on destruction.
    Stream sendStream { sender.get(), StreamType::GENERAL, fds[0], 1'000'000 };
    Stream recvStream { receiver.get(), StreamType::GENERAL, fds[1], 1'000'000 };

    // Registration: an idle stream wants RECV and ERR, not SEND.
    SocketWaitSet waitSet {};
    BOOST_CHECK(recvStream.RegisterSockets(waitSet));
    BOOST_CHECK_EQUAL(waitSet.Entries().size(), 1U);
    BOOST_CHECK_EQUAL(waitSet.Wanted(fds[1]), RECV_ERR);

    // Nothing ready: servicing is a no-op.
    bool gotNewMsgs { false };
    uint64_t bytesRecv { 0 };
    uint64_t bytesSent { 0 };
    recvStream.ServiceSocket(waitSet, config, gotNewMsgs, bytesRecv, bytesSent);
    BOOST_CHECK(!gotNewMsgs);
    BOOST_CHECK_EQUAL(bytesRecv, 0U);

    // Push a ping from the sending end. The optimistic send in PushMessage
    // writes it straight to the socket.
    const CNetMsgMaker msgMaker { INIT_PROTO_VERSION };
    CSerializedNetMsg msg { msgMaker.Make(NetMsgType::PING, uint64_t{0x1234}) };
    const CMessageHeader hdr { config, msg };
    std::vector<uint8_t> serializedHeader {};
    serializedHeader.reserve(hdr.GetLength());
    CVectorWriter { SER_NETWORK, INIT_PROTO_VERSION, serializedHeader, 0, hdr };
    const uint64_t payloadLength { msg.Size() };
    const uint64_t totalSize { payloadLength + serializedHeader.size() };
    const uint64_t pushed { sendStream.PushMessage(std::move(serializedHeader), std::move(msg), payloadLength, totalSize) };
    BOOST_CHECK_EQUAL(pushed, totalSize);

    // The receiving end, told it is readable, parses the whole message.
    waitSet.Clear();
    BOOST_CHECK(recvStream.RegisterSockets(waitSet));
    waitSet.SetReady(fds[1], SocketWaitSet::RECV);
    recvStream.ServiceSocket(waitSet, config, gotNewMsgs, bytesRecv, bytesSent);
    BOOST_CHECK(gotNewMsgs);
    BOOST_CHECK_EQUAL(bytesRecv, totalSize);
    auto [netMsg, moreMsgs] { recvStream.GetNextMessage() };
    BOOST_REQUIRE(netMsg);
    BOOST_CHECK(!moreMsgs);
    BOOST_CHECK_EQUAL(netMsg->GetHeader().GetCommand(), std::string{NetMsgType::PING});
    BOOST_CHECK(!receiver->GetDisconnect());

    // Peer hangs up: reported as readable and errored (POLLIN|POLLHUP under
    // poll), recv() returns 0 and the node is marked for disconnect.
    sendStream.Shutdown();
    waitSet.Clear();
    BOOST_CHECK(recvStream.RegisterSockets(waitSet));
    waitSet.SetReady(fds[1], RECV_ERR);
    gotNewMsgs = false;
    recvStream.ServiceSocket(waitSet, config, gotNewMsgs, bytesRecv, bytesSent);
    BOOST_CHECK(!gotNewMsgs);
    BOOST_CHECK(receiver->GetDisconnect());
}
#endif

BOOST_AUTO_TEST_SUITE_END()
