// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "addrman.h"
#include "chainparams.h"
#include "config.h"
#include "hash.h"
#include "net.h"
#include "netbase.h"
#include "serialize.h"
#include "streams.h"
#include "test/test_bitcoin.h"

#include <string>

#include <boost/test/unit_test.hpp>

class CAddrManSerializationMock : public CAddrMan {
public:
    virtual void Serialize(CDataStream &s) const = 0;

    //! Ensure that bucket placement is always the same for testing purposes.
    void MakeDeterministic() {
        nKey.SetNull();
        insecure_rand = FastRandomContext(true);
    }
};

class CAddrManUncorrupted : public CAddrManSerializationMock {
public:
    void Serialize(CDataStream &s) const override { CAddrMan::Serialize(s); }
};

class CAddrManCorrupted : public CAddrManSerializationMock {
public:
    void Serialize(CDataStream &s) const override {
        // Produces corrupt output that claims addrman has 20 addrs when it only
        // has one addr.
        uint8_t nVersion = 1;
        s << nVersion;
        s << uint8_t(32);
        s << nKey;
        s << 10; // nNew
        s << 10; // nTried

        int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT ^ (1 << 30);
        s << nUBuckets;

        CService serv;
        Lookup("252.1.1.1", serv, 7777, false);
        CAddress addr = CAddress(serv, NODE_NONE);
        CNetAddr resolved;
        LookupHost("252.2.2.2", resolved, false);
        CAddrInfo info = CAddrInfo(addr, resolved);
        s << info;
    }
};

CDataStream AddrmanToStream(CAddrManSerializationMock &_addrman) {
    CDataStream ssPeersIn(SER_DISK, CLIENT_VERSION);
    ssPeersIn << FLATDATA(Params().DiskMagic());
    ssPeersIn << _addrman;
    std::string str = ssPeersIn.str();
    std::vector<uint8_t> vchData(str.begin(), str.end());
    return CDataStream(vchData, SER_DISK, CLIENT_VERSION);
}

BOOST_FIXTURE_TEST_SUITE(net_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(caddrdb_read) {
    CAddrManUncorrupted addrmanUncorrupted;
    addrmanUncorrupted.MakeDeterministic();

    CService addr1, addr2, addr3;
    Lookup("250.7.1.1", addr1, 8333, false);
    Lookup("250.7.2.2", addr2, 9999, false);
    Lookup("250.7.3.3", addr3, 9999, false);

    // Add three addresses to new table.
    CService source;
    Lookup("252.5.1.1", source, 8333, false);
    addrmanUncorrupted.Add(CAddress(addr1, NODE_NONE), source);
    addrmanUncorrupted.Add(CAddress(addr2, NODE_NONE), source);
    addrmanUncorrupted.Add(CAddress(addr3, NODE_NONE), source);

    // Test that the de-serialization does not throw an exception.
    CDataStream ssPeers1 = AddrmanToStream(addrmanUncorrupted);
    bool exceptionThrown = false;
    CAddrMan addrman1;

    BOOST_CHECK(addrman1.size() == 0);
    try {
        uint8_t pchMsgTmp[4];
        ssPeers1 >> FLATDATA(pchMsgTmp);
        ssPeers1 >> addrman1;
    } catch (const std::exception &e) {
        exceptionThrown = true;
    }

    BOOST_CHECK(addrman1.size() == 3);
    BOOST_CHECK(exceptionThrown == false);

    // Test that CAddrDB::Read creates an addrman with the correct number of
    // addrs.
    CDataStream ssPeers2 = AddrmanToStream(addrmanUncorrupted);

    CAddrMan addrman2;
    CAddrDB adb(Params());
    BOOST_CHECK(addrman2.size() == 0);
    adb.Read(addrman2, ssPeers2);
    BOOST_CHECK(addrman2.size() == 3);
}

BOOST_AUTO_TEST_CASE(caddrdb_read_corrupted) {
    CAddrManCorrupted addrmanCorrupted;
    addrmanCorrupted.MakeDeterministic();

    // Test that the de-serialization of corrupted addrman throws an exception.
    CDataStream ssPeers1 = AddrmanToStream(addrmanCorrupted);
    bool exceptionThrown = false;
    CAddrMan addrman1;
    BOOST_CHECK(addrman1.size() == 0);
    try {
        uint8_t pchMsgTmp[4];
        ssPeers1 >> FLATDATA(pchMsgTmp);
        ssPeers1 >> addrman1;
    } catch (const std::exception &e) {
        exceptionThrown = true;
    }
    // Even through de-serialization failed addrman is not left in a clean
    // state.
    BOOST_CHECK(addrman1.size() == 1);
    BOOST_CHECK(exceptionThrown);

    // Test that CAddrDB::Read leaves addrman in a clean state if
    // de-serialization fails.
    CDataStream ssPeers2 = AddrmanToStream(addrmanCorrupted);

    CAddrMan addrman2;
    CAddrDB adb(Params());
    BOOST_CHECK(addrman2.size() == 0);
    adb.Read(addrman2, ssPeers2);
    BOOST_CHECK(addrman2.size() == 0);
}

BOOST_AUTO_TEST_CASE(cnode_simple_test) {
    SOCKET hSocket = INVALID_SOCKET;
    NodeId id = 0;
    int height = 0;

    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;

    CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    std::string pszDest = "";
    bool fInboundIn = false;

    // Test that fFeeler is false by default.

    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    CNodePtr pnode1 =
        CNode::Make(
            id++,
            NODE_NETWORK,
            height,
            hSocket,
            addr,
            0u,
            0u,
            asyncTaskPool,
            pszDest,
            fInboundIn);
    BOOST_CHECK(pnode1->fInbound == false);
    BOOST_CHECK(pnode1->fFeeler == false);

    fInboundIn = true;
    CNodePtr pnode2 =
        CNode::Make(
            id++,
            NODE_NETWORK,
            height,
            hSocket,
            addr,
            1u,
            1u,
            asyncTaskPool,
            pszDest,
            fInboundIn);
    BOOST_CHECK(pnode2->fInbound == true);
    BOOST_CHECK(pnode2->fFeeler == false);
}

BOOST_AUTO_TEST_CASE(test_userAgentLength) {
    std::string long_uacomment = "very very very very very very very very very "
                                 "very very very very very very very very very "
                                 "very very very very very very very very very "
                                 "very very very very very very very very very "
                                 "very very very very very very very very very "
                                 "very very very very very very very very very "
                                 "very very very very very very very very very "
                                 "very very very very very very long comment";
    gArgs.ForceSetMultiArg("-uacomment", long_uacomment);

    BOOST_CHECK_EQUAL(userAgent().size(), MAX_SUBVERSION_LENGTH);

    BOOST_CHECK(userAgent().find(
                      "very very very very very "
                      "very very very very very very very very very very very "
                      "very very very very very very very very very very very "
                      "very very very very very very very very very very very "
                      "very very very very very very very v)/") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
