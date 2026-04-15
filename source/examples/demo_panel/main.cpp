#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "hmi_nexus/app/application.h"

namespace {

std::atomic<bool> g_running{true};

void HandleSignal(int /*signal_number*/) {
    g_running.store(false);
}

}  // namespace

int main() {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    hmi_nexus::app::Application application;
    const auto result = application.start();
    if (!result) {
        std::cerr << "application failed: " << result.message() << std::endl;
        return 1;
    }

    while (g_running.load()) {
        const uint32_t delay_ms = application.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    return 0;
}
