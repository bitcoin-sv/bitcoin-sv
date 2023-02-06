// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/webhook_client.h"
#include "rpc/client.h"
#include "config.h"
#include "logging.h"
#include "task_helpers.h"

namespace
{
    // Number of times we will retry HTTP comm's before giving up
    static constexpr unsigned NumHTTPRetries {3};
}

namespace rpc::client
{

std::unique_ptr<WebhookClient> g_pWebhookClient {nullptr};

WebhookClient::WebhookClient(const Config& config)
: mSubmitPool { true, "WebhookClient", config.GetWebhookClientNumThreads() } 
{
}

// Submit the given request to the specified server
std::future<std::shared_ptr<HTTPResponse>> WebhookClient::SubmitRequest(
    const RPCClientConfig& clientConfig,
    std::shared_ptr<HTTPRequest>&& request,
    std::shared_ptr<HTTPResponse>&& response)
{
    LogPrint(BCLog::HTTP, "Queuing HTTP webhook request to %s\n", clientConfig.GetServerIP());

    // Lambda to perform the HTTP request
    auto submit = [config=clientConfig, request=std::move(request), response=std::move(response)]()
    {
        // Ask libevent to perform the request
        rpc::client::RPCClient client { config };
        bool success {false};
        unsigned retryCount {NumHTTPRetries};
        while(!success && retryCount-- > 0)
        {
            try
            {
                LogPrint(BCLog::HTTP, "Submitting HTTP webhook request to %s\n", config.GetServerIP());
                client.SubmitRequest(*request, response.get());

                // Consider anything other than a 2XX response as an error we should retry
                int status { response->GetStatus() };
                if(status >= 200 && status < 300)
                {
                    LogPrint(BCLog::HTTP, "Submitted HTTP webhook request to %s, status %d\n", config.GetServerIP(), status);
                    success = true;
                }
                else
                {
                    LogPrint(BCLog::HTTP, "Failed to submit HTTP webhook request to %s, status %d\n", config.GetServerIP(), status);
                }
            }
            catch(const std::exception& e)
            {
                LogPrint(BCLog::HTTP, "Error submitting HTTP webhook request to %s: %s\n", config.GetServerIP(),  e.what());
            }
        }

        // Return completed response
        return response;
    };

    // Submit to the thread pool
    return make_task(mSubmitPool, std::move(submit));
}

}   // namespace

