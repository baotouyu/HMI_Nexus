#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/net/http_client.h"

namespace hmi_nexus::system {
class UiDispatcher;
}

namespace hmi_nexus::net {

enum class HttpAsyncCallbackContext {
    kWorkerThread = 0,
    kUiThread,
};

const char* ToString(HttpAsyncCallbackContext context);

class HttpAsyncClient {
public:
    using ResponseHandler = std::function<void(HttpResponse response)>;
    using DownloadHandler = std::function<void(common::Result result)>;

    explicit HttpAsyncClient(HttpClient* http_client,
                             system::UiDispatcher* ui_dispatcher = nullptr);
    ~HttpAsyncClient();

    common::Result start(std::size_t worker_count = 1, std::size_t max_pending_jobs = 8);
    void stop();

    common::Result performAsync(
        HttpRequest request,
        ResponseHandler handler,
        HttpAsyncCallbackContext callback_context = HttpAsyncCallbackContext::kWorkerThread);
    common::Result downloadAsync(
        HttpDownloadRequest request,
        DownloadHandler handler,
        HttpAsyncCallbackContext callback_context = HttpAsyncCallbackContext::kWorkerThread);

    std::size_t pendingJobs() const;
    bool running() const;

private:
    common::Result enqueueJob(std::function<void()> job);
    common::Result validateCallbackContext(HttpAsyncCallbackContext callback_context) const;
    void dispatchCompletion(std::function<void()> completion,
                            HttpAsyncCallbackContext callback_context);
    void workerLoop();

    HttpClient* http_client_ = nullptr;
    system::UiDispatcher* ui_dispatcher_ = nullptr;

    mutable std::mutex mutex_;
    std::condition_variable condition_variable_;
    std::queue<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    bool running_ = false;
    bool stopping_ = false;
    std::size_t max_pending_jobs_ = 8;
};

}  // namespace hmi_nexus::net
