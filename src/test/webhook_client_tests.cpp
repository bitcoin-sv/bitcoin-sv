// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "rpc/webhook_client.h"

#include <boost/asio.hpp>
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>

namespace
{
    using boost::asio::ip::tcp;

    // Handle an asynchronous communications session
    class Session : public std::enable_shared_from_this<Session>
    {
      public:
        Session(tcp::socket&& socket) : mSocket{std::move(socket)}
        {}

        void start()
        {
            do_read();
        }

      private:
        void do_read()
        {
            auto self { shared_from_this() };
            mSocket.async_read_some(boost::asio::buffer(mData, max_length),
                [this, self](boost::system::error_code ec, size_t length)
                {
                    if(!ec)
                    {
                        send_response();
                    }
                });
        }

        void send_response()
        {
            int status {200};

            // Every 4 requests send an error response
            static std::atomic_int request_counter {0};
            if(++request_counter % 4 == 0)
            {
                status = 404;
            }

            std::stringstream resp {};
            resp << "HTTP/1.1 " << status << " OK\r\n";
            resp << "\r\n";

            boost::system::error_code ignored_error;
            boost::asio::write(mSocket, boost::asio::buffer(resp.str()), boost::asio::transfer_all(), ignored_error);
        }

        tcp::socket mSocket;
        enum { max_length = 1024 };
        char mData[max_length];
    };

    // Simple asynchronous socket server
    class Server
    {
      public:
        Server(boost::asio::io_service& io_service, unsigned short port)
        : mAcceptor { io_service, tcp::endpoint { tcp::v4(), port } },
          mSocket { io_service }
        {
            do_accept();
        }

      private:
        void do_accept()
        {
            mAcceptor.async_accept(mSocket,
                [this](boost::system::error_code ec)
                {
                    if(!ec)
                    {
                        // Process incoming connection
                        std::make_shared<Session>(std::move(mSocket))->start();
                    }

                    do_accept();
                });
        }

        tcp::acceptor mAcceptor;
        tcp::socket mSocket;
    };

    // A testing fixture that runs a TCP server
    class ServerSetup
    {
      public:
        ServerSetup()
        : mContext{}, mServer { mContext, 8888 }
        {
            // Run TCP server in its own thread in the background
            mFuture = std::async(std::launch::async, &ServerSetup::run, this);
        }

        ~ServerSetup()
        {
            mContext.stop();
            mFuture.wait();
        }

      private:
        void run()
        {
            mContext.run();
        }

        boost::asio::io_service mContext;
        Server mServer;
        std::future<void> mFuture {};
    };

    // Make a dummy HTTPRequest to use for testing
    rpc::client::HTTPRequest MakeRequest(const rpc::client::RPCClientConfig& config)
    {
        UniValue obj { UniValue::VOBJ };
        obj.push_back(Pair("name", "value"));
        return rpc::client::HTTPRequest::CreateJSONPostRequest(config, obj);
    }
}

BOOST_FIXTURE_TEST_SUITE(webhook_client_tests, ServerSetup)

BOOST_AUTO_TEST_CASE(RequestResponse)
{
    using namespace rpc::client;
    using HTTPRequest = rpc::client::HTTPRequest;

    // Configure webhook address and number of threads
    GlobalConfig::GetModifiableGlobalConfig().SetSafeModeWebhookURL("http://127.0.0.1:8888/");
    GlobalConfig::GetModifiableGlobalConfig().SetWebhookClientNumThreads(2, nullptr);

    // Create webhook client
    WebhookClient webhooks { GlobalConfig::GetConfig() };

    // Test synchronous request
    RPCClientConfig rpcConfig { RPCClientConfig::CreateForSafeModeWebhook(GlobalConfig::GetConfig()) };
    std::shared_ptr<HTTPRequest> request { std::make_shared<HTTPRequest>(MakeRequest(rpcConfig)) };
    std::shared_ptr<StringHTTPResponse> response { std::make_shared<StringHTTPResponse>() };
    auto result { webhooks.SubmitRequest(rpcConfig, std::move(request), std::move(response)) };
    // Wait for comms to complete
    response = std::dynamic_pointer_cast<StringHTTPResponse>(result.get());
    BOOST_REQUIRE(response);
    BOOST_CHECK_EQUAL(response->GetStatus(), 200);
}

BOOST_AUTO_TEST_CASE(ErrorResponseRetry)
{
    using namespace rpc::client;
    using HTTPRequest = rpc::client::HTTPRequest;

    // Configure webhook address and number of threads
    GlobalConfig::GetModifiableGlobalConfig().SetSafeModeWebhookURL("http://127.0.0.1:8888/");
    GlobalConfig::GetModifiableGlobalConfig().SetWebhookClientNumThreads(2, nullptr);

    // Create webhook client
    WebhookClient webhooks { GlobalConfig::GetConfig() };

    // Create a bunch of requests, some of which will have errors that need retrying
    std::vector<std::future<std::shared_ptr<HTTPResponse>>> responses {};
    RPCClientConfig rpcConfig { RPCClientConfig::CreateForSafeModeWebhook(GlobalConfig::GetConfig()) };
    for(int i = 0; i < 20; ++i)
    {
        std::shared_ptr<HTTPRequest> request { std::make_shared<HTTPRequest>(MakeRequest(rpcConfig)) };
        std::shared_ptr<StringHTTPResponse> response { std::make_shared<StringHTTPResponse>() };
        responses.push_back(webhooks.SubmitRequest(rpcConfig, std::move(request), std::move(response)));
    }

    // Check each request eventually succeeds
    for(auto& fut : responses)
    {
        const auto& response { fut.get() };
        BOOST_CHECK_EQUAL(response->GetStatus(), 200);
    }
}

BOOST_AUTO_TEST_SUITE_END()

