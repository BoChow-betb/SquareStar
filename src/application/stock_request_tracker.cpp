#include "application/stock_request_tracker.hpp"

namespace squarestar::application {

std::uint64_t StockRequestTracker::Request(StockRequestChannel channel) noexcept {
    return ++generations_[Index(channel)].desired;
}

std::uint64_t StockRequestTracker::EnsureDesired(
    StockRequestChannel channel) noexcept {
    StockRequestGeneration& state = generations_[Index(channel)];
    return state.desired == state.applied ? Request(channel) : state.desired;
}

bool StockRequestTracker::Begin(StockRequestChannel channel,
                                    std::uint64_t generation) noexcept {
    StockRequestGeneration& state = generations_[Index(channel)];
    if (generation == 0 || generation != state.desired || state.inFlight != 0)
        return false;
    state.inFlight = generation;
    return true;
}

bool StockRequestTracker::CanApply(StockRequestChannel channel,
                                       std::uint64_t generation) const noexcept {
    const StockRequestGeneration& state = generations_[Index(channel)];
    return generation != 0 && generation == state.desired &&
           generation == state.inFlight && generation > state.applied;
}

bool StockRequestTracker::Complete(StockRequestChannel channel,
                                       std::uint64_t generation,
                                       bool succeeded) noexcept {
    StockRequestGeneration& state = generations_[Index(channel)];
    if (generation == 0 || generation != state.inFlight)
        return false;
    const bool apply = succeeded && generation == state.desired &&
                       generation > state.applied;
    state.inFlight = 0;
    if (apply)
        state.applied = generation;
    return apply;
}

void StockRequestTracker::CancelDesired(StockRequestChannel channel) noexcept {
    StockRequestGeneration& state = generations_[Index(channel)];
    if (state.inFlight == 0)
        state.desired = state.applied;
}

const StockRequestGeneration& StockRequestTracker::State(
    StockRequestChannel channel) const noexcept {
    return generations_[Index(channel)];
}

} // namespace squarestar::application
