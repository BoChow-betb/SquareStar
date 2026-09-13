#include "application/screener_executor.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    std::atomic_bool start{false};
    std::atomic_uint executed{0};
    std::vector<std::thread> submitters;
    submitters.reserve(8);
    for (int worker = 0; worker < 8; ++worker) {
        submitters.emplace_back([&, worker] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int attempt = 0; attempt < 1000; ++attempt) {
                const auto job = [&executed] {
                    executed.fetch_add(1, std::memory_order_relaxed);
                };
                (void)worker;
                (void)squarestar::application::SubmitLatestScreenerJob(job);
            }
        });
    }
    std::thread closer([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        squarestar::application::ShutdownLatestScreenerJobExecutor();
    });
    start.store(true, std::memory_order_release);
    for (auto& submitter : submitters)
        submitter.join();
    closer.join();

    if (squarestar::application::SubmitLatestScreenerJob([] {}) ||
        squarestar::application::ScreenerJobBusy()) {
        std::cerr << "screener executor accepted work after shutdown\n";
        return EXIT_FAILURE;
    }
    std::cout << "Executor lifecycle race test passed (executed="
              << executed.load(std::memory_order_relaxed) << ")\n";
    return EXIT_SUCCESS;
}
