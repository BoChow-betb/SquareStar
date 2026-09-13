#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "application/screener_item.hpp"

namespace squarestar::providers {

inline constexpr int kPersistentScreenerCacheVersion = 1;

std::string EncodePersistentScreenerCache(
    const std::string& cacheKey,
    std::chrono::system_clock::time_point fetchedAt,
    const std::vector<squarestar::application::ScreenerItem>& items);

bool DecodePersistentScreenerCache(
    std::string payload,
    const std::string& expectedCacheKey,
    std::chrono::system_clock::time_point& fetchedAt,
    std::vector<squarestar::application::ScreenerItem>& items);

bool LoadPersistentScreenerCacheFile(
    const std::string& path,
    const std::string& expectedCacheKey,
    std::chrono::system_clock::time_point& fetchedAt,
    std::vector<squarestar::application::ScreenerItem>& items);
bool QueuePersistentScreenerCacheFile(
    const std::string& path,
    const std::string& cacheKey,
    std::chrono::system_clock::time_point fetchedAt,
    const std::vector<squarestar::application::ScreenerItem>& items);

// Persist only the filtered Trending Now snapshot; other screeners stay memory-only.
bool LoadPersistentScreenerCache(
    const std::string& cacheKey,
    std::chrono::system_clock::time_point& fetchedAt,
    std::vector<squarestar::application::ScreenerItem>& items);
bool QueuePersistentScreenerCache(
    const std::string& cacheKey,
    std::chrono::system_clock::time_point fetchedAt,
    const std::vector<squarestar::application::ScreenerItem>& items);

} // namespace squarestar::providers
