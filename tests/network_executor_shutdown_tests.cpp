#include "services/network_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main() {
    PersistentExecutor executor(1, "shutdown-test");
    std::promise<void> started;
    std::promise<void> releasePromise;
    std::shared_future<void> release = releasePromise.get_future().share();

    auto running = executor.Submit([&] {
        started.set_value();
        release.wait();
        return 1;
    });
    started.get_future().wait();

    std::atomic_uint queuedExecutions{0};
    std::vector<std::future<int>> queued;
    for (int index = 0; index < 16; ++index) {
        queued.push_back(executor.Submit([&] {
            queuedExecutions.fetch_add(1, std::memory_order_relaxed);
            return 2;
        }));
    }

    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        releasePromise.set_value();
    });
    executor.Shutdown();
    releaser.join();

    if (running.get() != 1 || queuedExecutions.load(std::memory_order_relaxed) != 0) {
        std::cerr << "shutdown executed work that was still queued\n";
        return EXIT_FAILURE;
    }
    for (auto& future : queued) {
        if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            std::cerr << "cancelled task future was not released\n";
            return EXIT_FAILURE;
        }
        try {
            (void)future.get();
            std::cerr << "cancelled task unexpectedly produced a value\n";
            return EXIT_FAILURE;
        } catch (const std::future_error& error) {
            if (error.code() != std::make_error_code(std::future_errc::broken_promise)) {
                std::cerr << "cancelled task produced the wrong future error\n";
                return EXIT_FAILURE;
            }
        }
    }

    PersistentExecutor dependency(1, "dependency-test");
    PersistentExecutor consumer(1, "consumer-test");
    std::promise<void> blockerStarted;
    std::promise<void> releaseBlocker;
    const std::shared_future<void> blockerRelease = releaseBlocker.get_future().share();
    auto blocker = dependency.Submit([&] {
        blockerStarted.set_value();
        blockerRelease.wait();
        return 1;
    });
    blockerStarted.get_future().wait();

    std::promise<void> dependencyQueued;
    auto dependent = consumer.Submit([&] {
        auto queuedDependency = dependency.Submit([] { return 9; });
        dependencyQueued.set_value();
        try {
            (void)queuedDependency.get();
            return 0;
        } catch (const std::future_error& error) {
            return error.code() == std::make_error_code(std::future_errc::broken_promise)
                       ? 1
                       : -1;
        }
    });
    dependencyQueued.get_future().wait();

    dependency.RequestShutdown();
    consumer.RequestShutdown();
    releaseBlocker.set_value();
    consumer.Join();
    dependency.Join();
    if (blocker.get() != 1 || dependent.get() != 1) {
        std::cerr << "two-phase shutdown did not release a dependent executor\n";
        return EXIT_FAILURE;
    }

    PersistentExecutor saturated(1, "saturation-test");
    std::promise<void> saturationStarted;
    std::promise<void> releaseSaturation;
    const auto saturationRelease = releaseSaturation.get_future().share();
    auto saturationBlocker = saturated.Submit([&] {
        saturationStarted.set_value();
        saturationRelease.wait();
        return squarestar::http::HttpResponse{
            "ok", 200, squarestar::http::HttpError::None};
    });
    saturationStarted.get_future().wait();
    std::vector<std::future<squarestar::http::HttpResponse>> backlog;
    squarestar::http::HttpResponse rejectedResponse;
    bool observedRejection = false;


for (std::size_t index = 0; index < 4096; ++index) {
        auto candidate = saturated.Submit([] {
            return squarestar::http::HttpResponse{
                "queued", 200, squarestar::http::HttpError::None};
        });
        if (candidate.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            rejectedResponse = candidate.get();
            observedRejection = true;
            break;
        }
        backlog.push_back(std::move(candidate));
    }
    if (!observedRejection ||
        rejectedResponse.error != squarestar::http::HttpError::ExecutorRejected ||
        rejectedResponse.statusCode != 0 || !rejectedResponse.body.empty() ||
        saturated.RejectedSubmissionCount() != 1) {
        std::cerr << "saturated HTTP executor returned an ambiguous response\n";
        return EXIT_FAILURE;
    }
    saturated.RequestShutdown();
    releaseSaturation.set_value();
    saturated.Join();
    if (!saturationBlocker.get().IsSuccess()) {
        std::cerr << "running saturation blocker did not finish normally\n";
        return EXIT_FAILURE;
    }


PersistentExecutor fair(1, "priority-fairness-test");
    std::promise<void> fairnessStarted;
    std::promise<void> releaseFairness;
    const auto fairnessRelease = releaseFairness.get_future().share();
    auto fairnessBlocker = fair.Submit([&] {
        fairnessStarted.set_value();
        fairnessRelease.wait();
    });
    fairnessStarted.get_future().wait();
    std::mutex orderMutex;
    std::vector<int> executionOrder;
    std::vector<std::future<void>> fairnessFutures;
    const auto record = [&](int value) {
        return fair.Submit(
            [&, value] {
                std::lock_guard<std::mutex> lock(orderMutex);
                executionOrder.push_back(value);
            },
            value == 1 ? ExecutorPriority::High
                       : value == 2 ? ExecutorPriority::Normal
                                    : ExecutorPriority::Low);
    };
    for (int index = 0; index < 32; ++index)
        fairnessFutures.push_back(record(1));
    for (int index = 0; index < 8; ++index)
        fairnessFutures.push_back(record(2));
    fairnessFutures.push_back(record(3));
    releaseFairness.set_value();
    fairnessBlocker.get();
    for (auto& future : fairnessFutures)
        future.get();
    const auto firstNormal = std::find(executionOrder.begin(), executionOrder.end(), 2);
    const auto firstLow = std::find(executionOrder.begin(), executionOrder.end(), 3);
    if (firstNormal == executionOrder.end() ||
        std::distance(executionOrder.begin(), firstNormal) > 4) {
        std::cerr << "normal priority starved behind high-priority work\n";
        return EXIT_FAILURE;
    }
    if (firstLow == executionOrder.end() ||
        std::distance(executionOrder.begin(), firstLow) > 24) {
        std::cerr << "low priority starved behind higher-priority work\n";
        return EXIT_FAILURE;
    }
    fair.Shutdown();


PersistentExecutor twoLane(2, "two-lane-latency-test", std::nullopt);
    std::promise<void> slowStarted;
    std::promise<void> releaseSlow;
    const auto slowRelease = releaseSlow.get_future().share();
    auto slow = twoLane.Submit(
        [&] {
            slowStarted.set_value();
            slowRelease.wait();
            return 1;
        },
        ExecutorPriority::Normal);
    slowStarted.get_future().wait();
    auto interactive = twoLane.Submit([] { return 2; }, ExecutorPriority::High);
    if (interactive.wait_for(std::chrono::milliseconds(250)) !=
            std::future_status::ready ||
        interactive.get() != 2) {
        releaseSlow.set_value();
        twoLane.Shutdown();
        std::cerr << "two-lane executor allowed in-flight work to block interactive work\n";
        return EXIT_FAILURE;
    }
    releaseSlow.set_value();
    if (slow.get() != 1) {
        std::cerr << "two-lane slow task did not finish normally\n";
        return EXIT_FAILURE;
    }
    twoLane.Shutdown();


PersistentExecutor persistent(1, "persistent-idle-test", std::nullopt);
    std::atomic_uint workerEpochs{0};
    const auto captureWorkerEpoch = [&workerEpochs] {
        thread_local const unsigned epoch =
            workerEpochs.fetch_add(1, std::memory_order_relaxed) + 1;
        return epoch;
    };
    const unsigned firstWorkerEpoch = persistent.Submit(captureWorkerEpoch).get();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const unsigned secondWorkerEpoch = persistent.Submit(captureWorkerEpoch).get();
    if (firstWorkerEpoch != secondWorkerEpoch ||
        workerEpochs.load(std::memory_order_relaxed) != 1) {
        std::cerr << "persistent executor recreated its worker after idle\n";
        return EXIT_FAILURE;
    }
    persistent.Shutdown();

    PersistentExecutor stopped(1, "post-shutdown-test");
    stopped.Shutdown();
    auto postShutdown = stopped.Submit([] { return 1; });
    try {
        (void)postShutdown.get();
        std::cerr << "stopped executor returned an ambiguous default value\n";
        return EXIT_FAILURE;
    } catch (const ExecutorRejected&) {

    }

    std::cout << "Network executor cancellation and rejection semantics passed\n";
    return EXIT_SUCCESS;
}
