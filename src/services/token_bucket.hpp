#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace squarestar::http {

class TokenBucket {
  public:
    TokenBucket(double tokensPerSecond, double burstCapacity)
        : rate_(std::max(tokensPerSecond, 0.001)),
          capacity_(std::max(burstCapacity, 1.0)),
          tokens_(capacity_) {}

    bool TryConsume(std::chrono::steady_clock::time_point now,
                    double amount = 1.0) {
        if (!(amount > 0.0) || amount > capacity_)
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        Refill(now);
        if (tokens_ + 1e-12 < amount)
            return false;
        tokens_ -= amount;
        return true;
    }

    [[nodiscard]] std::chrono::milliseconds TimeUntilAvailable(
        std::chrono::steady_clock::time_point now,
        double amount = 1.0) {
        if (!(amount > 0.0) || amount > capacity_)
            return std::chrono::milliseconds::max();
        std::lock_guard<std::mutex> lock(mutex_);
        Refill(now);
        if (tokens_ + 1e-12 >= amount)
            return std::chrono::milliseconds::zero();
        const double seconds = (amount - tokens_) / rate_;
        return std::chrono::milliseconds(
            std::max<long long>(1, static_cast<long long>(std::ceil(seconds * 1000.0))));
    }

  private:
    void Refill(std::chrono::steady_clock::time_point now) {
        if (lastRefill_ == std::chrono::steady_clock::time_point{}) {
            lastRefill_ = now;
            return;
        }
        if (now <= lastRefill_)
            return;
        const double elapsed =
            std::chrono::duration<double>(now - lastRefill_).count();
        tokens_ = std::min(capacity_, tokens_ + elapsed * rate_);
        lastRefill_ = now;
    }

    std::mutex mutex_;
    double rate_ = 1.0;
    double capacity_ = 1.0;
    double tokens_ = 1.0;
    std::chrono::steady_clock::time_point lastRefill_{};
};

} // namespace squarestar::http
