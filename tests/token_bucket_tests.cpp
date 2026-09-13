#include "services/token_bucket.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
} // namespace

int main() {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    squarestar::http::TokenBucket bucket(2.0, 2.0);
    const auto start = Clock::time_point{} + 1s;
    Check(bucket.TryConsume(start), "first burst token must be available");
    Check(bucket.TryConsume(start), "second burst token must be available");
    Check(!bucket.TryConsume(start), "burst capacity must cap immediate provider traffic");
    const auto wait = bucket.TimeUntilAvailable(start);
    Check(wait >= 499ms && wait <= 501ms,
          "token bucket must report the deterministic refill delay");
    Check(!bucket.TryConsume(start + 249ms), "partial refill must not fabricate a token");
    Check(bucket.TryConsume(start + 500ms), "one token must refill after half a second");
    Check(!bucket.TryConsume(start + 500ms), "refill must not exceed elapsed-time capacity");
    Check(bucket.TryConsume(start + 1500ms), "later refill must restore a token");
    Check(bucket.TryConsume(start + 1500ms), "burst capacity must refill but stay bounded");
    Check(!bucket.TryConsume(start + 1500ms), "refilled burst must remain bounded");
    return EXIT_SUCCESS;
}
