#include "hmi_nexus/net/http_async_client.h"

#include <sstream>
#include <utility>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/system/logger.h"
#include "hmi_nexus/system/ui_dispatcher.h"

namespace hmi_nexus::net {

const char* ToString(HttpAsyncCallbackContext context) {
    switch (context) {
    case HttpAsyncCallbackContext::kWorkerThread:
        return "worker";
    case HttpAsyncCallbackContext::kUiThread:
        return "ui";
    }
    return "worker";
}

HttpAsyncClient::HttpAsyncClient(HttpClient* http_client, system::UiDispatcher* ui_dispatcher)
    : http_client_(http_client), ui_dispatcher_(ui_dispatcher) {}

HttpAsyncClient::~HttpAsyncClient() {
    stop();
}

common::Result HttpAsyncClient::start(std::size_t worker_count,
                                      std::size_t max_pending_jobs) {
    if (http_client_ == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "http client is null");
    }
    if (worker_count == 0) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "worker_count must be greater than zero");
    }
    if (max_pending_jobs == 0) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "max_pending_jobs must be greater than zero");
    }

    auto init_result = http_client_->initialize();
    if (!init_result) {
        return init_result;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return common::Result::Ok();
        }

        running_ = true;
        stopping_ = false;
        max_pending_jobs_ = max_pending_jobs;
    }

    try {
        workers_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_.emplace_back(&HttpAsyncClient::workerLoop, this);
        }
    } catch (...) {
        stop();
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "failed to start HTTP async worker threads");
    }

    std::ostringstream message;
    message << "HTTP async client started with " << worker_count
            << " worker(s), queue limit=" << max_pending_jobs_;
    system::Logger::Info("net.http.async", message.str());
    return common::Result::Ok();
}

void HttpAsyncClient::stop() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && workers_.empty()) {
            return;
        }

        stopping_ = true;
        workers.swap(workers_);
    }

    condition_variable_.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        stopping_ = false;
        std::queue<std::function<void()>> empty_jobs;
        jobs_.swap(empty_jobs);
    }

    system::Logger::Info("net.http.async", "HTTP async client stopped");
}

common::Result HttpAsyncClient::performAsync(HttpRequest request,
                                             ResponseHandler handler,
                                             HttpAsyncCallbackContext callback_context) {
    auto context_result = validateCallbackContext(callback_context);
    if (!context_result) {
        return context_result;
    }

    return enqueueJob([
        this,
        request = std::move(request),
        handler = std::move(handler),
        callback_context
    ]() mutable {
        auto response = http_client_->perform(request);
        if (!handler) {
            return;
        }

        dispatchCompletion(
            [handler = std::move(handler), response = std::move(response)]() mutable {
                handler(std::move(response));
            },
            callback_context);
    });
}

common::Result HttpAsyncClient::downloadAsync(HttpDownloadRequest request,
                                              DownloadHandler handler,
                                              HttpAsyncCallbackContext callback_context) {
    auto context_result = validateCallbackContext(callback_context);
    if (!context_result) {
        return context_result;
    }

    return enqueueJob([
        this,
        request = std::move(request),
        handler = std::move(handler),
        callback_context
    ]() mutable {
        auto result = http_client_->download(request);
        if (!handler) {
            return;
        }

        dispatchCompletion(
            [handler = std::move(handler), result = std::move(result)]() mutable {
                handler(std::move(result));
            },
            callback_context);
    });
}

std::size_t HttpAsyncClient::pendingJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

bool HttpAsyncClient::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

common::Result HttpAsyncClient::enqueueJob(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "HTTP async client is not running");
    }

    if (jobs_.size() >= max_pending_jobs_) {
        return common::Result::Fail(common::ErrorCode::kBusy,
                                    "HTTP async queue is full");
    }

    jobs_.push(std::move(job));
    condition_variable_.notify_one();
    return common::Result::Ok();
}

common::Result HttpAsyncClient::validateCallbackContext(
    HttpAsyncCallbackContext callback_context) const {
    if (callback_context == HttpAsyncCallbackContext::kUiThread && ui_dispatcher_ == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "UI callback context requires a UiDispatcher");
    }
    return common::Result::Ok();
}

void HttpAsyncClient::dispatchCompletion(std::function<void()> completion,
                                         HttpAsyncCallbackContext callback_context) {
    if (!completion) {
        return;
    }

    if (callback_context == HttpAsyncCallbackContext::kUiThread && ui_dispatcher_ != nullptr) {
        ui_dispatcher_->post(std::move(completion));
        return;
    }

    completion();
}

void HttpAsyncClient::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_variable_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });

            if (stopping_ && jobs_.empty()) {
                return;
            }

            job = std::move(jobs_.front());
            jobs_.pop();
        }

        if (job) {
            job();
        }
    }
}

}  // namespace hmi_nexus::net
