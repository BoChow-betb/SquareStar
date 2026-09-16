#pragma once

#include "domain/text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace squarestar::search {

inline std::string NormalizeSearchText(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    bool pendingSpace = false;
    for (const char raw : value) {
        const unsigned char character = static_cast<unsigned char>(raw);
        if (std::isalnum(character)) {
            if (pendingSpace && !normalized.empty())
                normalized.push_back(' ');
            normalized.push_back(squarestar::text::AsciiUpper(character));
            pendingSpace = false;
        } else if (!normalized.empty()) {
            pendingSpace = true;
        }
    }
    return normalized;
}

inline std::string NormalizeSymbolSearchKey(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const char raw : value) {
        const unsigned char character = static_cast<unsigned char>(raw);
        if (std::isalnum(character))
            normalized.push_back(squarestar::text::AsciiUpper(character));
    }
    return normalized;
}

inline std::vector<std::string_view> SearchWords(std::string_view normalizedText) {
    std::vector<std::string_view> words;
    size_t start = 0;
    while (start < normalizedText.size()) {
        const size_t end = normalizedText.find(' ', start);
        words.push_back(normalizedText.substr(
            start, end == std::string_view::npos ? normalizedText.size() - start : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return words;
}

inline bool IsCompanyNoiseWord(std::string_view word) {
    constexpr std::array<std::string_view, 18> noiseWords = {
        "INC",      "INCORPORATED", "CORP",     "CORPORATION", "CO",      "COMPANY",
        "LTD",      "LIMITED",      "PLC",      "LLC",         "LP",      "GROUP",
        "HOLDING",  "HOLDINGS",     "HLDGS",    "THE",         "SA",      "NV",
    };
    return std::find(noiseWords.begin(), noiseWords.end(), word) != noiseWords.end();
}

inline int BoundedEditDistance(std::string_view left,
                               std::string_view right,
                               int maximumDistance) {
    if (maximumDistance < 0)
        return maximumDistance + 1;
    if (left == right)
        return 0;

    const int lengthDifference =
        std::abs(static_cast<int>(left.size()) - static_cast<int>(right.size()));
    if (lengthDifference > maximumDistance)
        return maximumDistance + 1;
    if (left.empty())
        return static_cast<int>(right.size()) <= maximumDistance
                   ? static_cast<int>(right.size())
                   : maximumDistance + 1;
    if (right.empty())
        return static_cast<int>(left.size()) <= maximumDistance
                   ? static_cast<int>(left.size())
                   : maximumDistance + 1;


if (maximumDistance == 1) {
        if (left.size() > right.size())
            std::swap(left, right);
        size_t leftIndex = 0;
        size_t rightIndex = 0;
        int edits = 0;
        while (leftIndex < left.size() && rightIndex < right.size()) {
            if (left[leftIndex] == right[rightIndex]) {
                ++leftIndex;
                ++rightIndex;
                continue;
            }
            if (++edits > 1)
                return 2;
            if (left.size() == right.size())
                ++leftIndex;
            ++rightIndex;
        }
        if (leftIndex < left.size() || rightIndex < right.size())
            ++edits;
        return edits <= 1 ? edits : 2;
    }


    if (right.size() > left.size())
        std::swap(left, right);
    std::vector<int> previous(right.size() + 1);
    std::vector<int> current(right.size() + 1);
    for (size_t column = 0; column <= right.size(); ++column)
        previous[column] = static_cast<int>(column);

    for (size_t row = 1; row <= left.size(); ++row) {
        current[0] = static_cast<int>(row);
        int rowMinimum = current[0];
        for (size_t column = 1; column <= right.size(); ++column) {
            const int substitutionCost = left[row - 1] == right[column - 1] ? 0 : 1;
            current[column] = std::min({previous[column] + 1,
                                        current[column - 1] + 1,
                                        previous[column - 1] + substitutionCost});
            rowMinimum = std::min(rowMinimum, current[column]);
        }
        if (rowMinimum > maximumDistance)
            return maximumDistance + 1;
        previous.swap(current);
    }
    return previous[right.size()] <= maximumDistance ? previous[right.size()]
                                                      : maximumDistance + 1;
}

inline bool PhraseOccursOnWordBoundary(std::string_view text, std::string_view phrase) {
    if (text.empty() || phrase.empty())
        return false;
    size_t position = text.find(phrase);
    while (position != std::string_view::npos) {
        const size_t end = position + phrase.size();
        const bool startsOnBoundary = position == 0 || text[position - 1] == ' ';
        const bool endsOnBoundary = end == text.size() || text[end] == ' ';
        if (startsOnBoundary && endsOnBoundary)
            return true;
        position = text.find(phrase, position + 1);
    }
    return false;
}

inline bool IsPlaceholderSymbolDescription(std::string_view symbol, std::string_view description) {
    const std::string symbolKey = NormalizeSymbolSearchKey(symbol);
    const std::string descriptionKey = NormalizeSymbolSearchKey(description);
    return !symbolKey.empty() && (descriptionKey.empty() || descriptionKey == symbolKey);
}

inline int NameWordMatchQuality(std::string_view queryWord, std::string_view nameWord) {
    if (queryWord == nameWord)
        return 4;
    if (queryWord.size() >= 2 && nameWord.starts_with(queryWord))
        return 3;
    if (queryWord.size() >= 3 && nameWord.find(queryWord) != std::string_view::npos)
        return 2;
    if (queryWord.size() >= 4 && nameWord.size() >= 4 &&
        BoundedEditDistance(queryWord, nameWord, 1) <= 1)
        return 1;
    return 0;
}

struct QueryInfo {
    std::string text;
    std::string key;
    std::vector<std::string> words;
};

inline QueryInfo ParseQuery(std::string_view query) {
    QueryInfo info;
    info.text = NormalizeSearchText(query);
    info.key = NormalizeSymbolSearchKey(query);
    const auto words = SearchWords(info.text);
    info.words.reserve(words.size());
    for (const std::string_view word : words) {
        if (words.size() > 1 && IsCompanyNoiseWord(word))
            continue;
        info.words.emplace_back(word);
    }
    if (info.words.empty()) {
        for (const std::string_view word : words)
            info.words.emplace_back(word);
    }
    return info;
}

inline int ScoreMatch(const QueryInfo& query,
                      std::string_view symbol,
                      std::string_view name,
                      std::string_view assetType) {
    const std::string symbolKey = NormalizeSymbolSearchKey(symbol);
    if (query.text.empty() || query.key.empty() || symbolKey.empty())
        return 0;

    const std::string normalizedName = NormalizeSearchText(name);
    const std::string normalizedType = NormalizeSearchText(assetType);
    const std::vector<std::string_view> nameWords = SearchWords(normalizedName);

    int symbolScore = 0;
    if (symbolKey == query.key) {
        symbolScore = 20'000;
    } else if (symbolKey.starts_with(query.key)) {
        symbolScore = 14'500 -
                      std::min<int>(static_cast<int>(symbolKey.size() - query.key.size()) * 90,
                                    1'200);
    } else if (query.key.size() >= 2 && symbolKey.find(query.key) != std::string::npos) {
        symbolScore = 5'200;
    } else if (query.key.size() >= 3 && query.key.size() <= 8 && symbolKey.size() <= 8 &&
               BoundedEditDistance(query.key, symbolKey, 1) <= 1) {


        symbolScore = 8'200;
    }

    int nameScore = 0;
    bool completeNameTokenMatch = false;
    if (!normalizedName.empty()) {
        if (normalizedName == query.text) {
            nameScore = 17'000;
        } else if (query.text.size() >= 2 && normalizedName.starts_with(query.text)) {
            nameScore = 13'500;
        } else if (query.text.size() >= 2 &&
                   PhraseOccursOnWordBoundary(normalizedName, query.text)) {
            nameScore = 11'500;
        } else if (query.text.size() >= 3 &&
                   normalizedName.find(query.text) != std::string::npos) {
            nameScore = 7'500;
        }

        if (!query.words.empty() && !nameWords.empty()) {
            std::vector<bool> usedNameWords(nameWords.size(), false);
            size_t previousMatch = 0;
            bool ordered = true;
            int tokenScore = 0;
            int matchedWords = 0;
            size_t firstMatchedIndex = nameWords.size();
            size_t lastMatchedIndex = 0;

            for (const std::string& queryWord : query.words) {
                int bestQuality = 0;
                size_t bestIndex = nameWords.size();
                for (size_t index = 0; index < nameWords.size(); ++index) {
                    if (usedNameWords[index])
                        continue;
                    const int quality = NameWordMatchQuality(queryWord, nameWords[index]);
                    if (quality > bestQuality) {
                        bestQuality = quality;
                        bestIndex = index;
                    } else if (quality == bestQuality && quality > 0 &&
                               bestIndex != nameWords.size()) {
                        bestIndex = std::min(bestIndex, index);
                    }
                }
                if (bestQuality == 0)
                    continue;

                usedNameWords[bestIndex] = true;
                if (matchedWords > 0 && bestIndex < previousMatch)
                    ordered = false;
                previousMatch = bestIndex;
                firstMatchedIndex = std::min(firstMatchedIndex, bestIndex);
                lastMatchedIndex = std::max(lastMatchedIndex, bestIndex);
                ++matchedWords;
                tokenScore += bestQuality == 4   ? 2'650
                              : bestQuality == 3 ? 1'850
                              : bestQuality == 2 ? 850
                                                 : 420;
            }

            if (matchedWords == static_cast<int>(query.words.size())) {
                completeNameTokenMatch = true;
                tokenScore += 4'400;
                if (ordered && query.words.size() > 1)
                    tokenScore += 750;
                if (firstMatchedIndex == 0)
                    tokenScore += 550;
                if (query.words.size() > 1 && lastMatchedIndex >= firstMatchedIndex) {
                    const size_t span = lastMatchedIndex - firstMatchedIndex + 1;
                    const size_t extraWords =
                        span > query.words.size() ? span - query.words.size() : 0;
                    tokenScore -= static_cast<int>(std::min<size_t>(extraWords * 180, 720));
                }
                nameScore = std::max(nameScore, tokenScore);
            }
        }
    }

    if (symbolScore == 0 && nameScore == 0 && !completeNameTokenMatch)
        return 0;

    int score = std::max(symbolScore, nameScore);
    if (normalizedType.find("COMMON STOCK") != std::string::npos) {
        score += 420;
    } else if (normalizedType.find("ETF") != std::string::npos ||
               normalizedType.find("EXCHANGE TRADED") != std::string::npos) {
        score += 300;
    } else if (normalizedType.find("INDEX") != std::string::npos ||
               normalizedType.find("FUND") != std::string::npos) {
        score += 140;
    }

    if (normalizedType.find("OTC") != std::string::npos)
        score -= 1'600;
    if (normalizedType.find("PREFERRED") != std::string::npos)
        score -= 650;
    if (normalizedType.find("WARRANT") != std::string::npos ||
        normalizedType.find("RIGHT") != std::string::npos ||
        normalizedType.find("UNIT") != std::string::npos) {
        score -= 2'400;
    }

    if (symbolKey.size() > 5)
        score -= static_cast<int>(std::min<size_t>((symbolKey.size() - 5) * 35, 350));
    return std::max(score, 1);
}

inline int ScoreSymbolSearchResult(std::string_view query,
                                   std::string_view symbol,
                                   std::string_view name,
                                   std::string_view assetType) {
    return ScoreMatch(ParseQuery(query), symbol, name, assetType);
}

inline int RankMatch(const QueryInfo& query,
                     std::string_view symbol,
                     std::string_view name,
                     std::string_view assetType,
                     size_t providerIndex) {
    const int relevance = ScoreMatch(query, symbol, name, assetType);
    if (relevance <= 0)
        return 0;
    const int providerTieBreak =
        40 - static_cast<int>(std::min<size_t>(providerIndex, 40));
    return 100'000 + relevance + providerTieBreak;
}

inline int RankSymbolSearchResult(std::string_view query,
                                  std::string_view symbol,
                                  std::string_view name,
                                  std::string_view assetType,
                                  size_t providerIndex) {
    return RankMatch(ParseQuery(query), symbol, name, assetType, providerIndex);
}

inline void RemoveAmbiguousPlaceholderMatches(
    std::string_view query,
    std::vector<std::pair<std::string, std::string>>& results) {
    const std::string queryKey = NormalizeSymbolSearchKey(query);
    if (queryKey.empty())
        return;
    std::erase_if(results, [&](const auto& item) {
        const std::string symbolKey = NormalizeSymbolSearchKey(item.first);
        if (symbolKey != queryKey)
            return false;
        const std::string descriptionKey = NormalizeSymbolSearchKey(item.second);
        return descriptionKey.empty() || descriptionKey == symbolKey;
    });
}

inline void PersonalizeSymbolSearchResults(
    std::string_view query,
    std::vector<std::pair<std::string, std::string>>& results,
    const std::vector<std::string>& recentSymbols,
    const std::map<std::string, std::string>& recentSymbolNames,
    const std::vector<std::string>& watchlistSymbols) {
    const QueryInfo parsed = ParseQuery(query);

    std::unordered_set<std::string> resultKeys;
    resultKeys.reserve(results.size() + recentSymbols.size() + watchlistSymbols.size());
    for (const auto& item : results)
        resultKeys.insert(NormalizeSymbolSearchKey(item.first));

    std::vector<std::string> recentKeys;
    recentKeys.reserve(recentSymbols.size());
    std::unordered_map<std::string, size_t> recentRank;
    recentRank.reserve(recentSymbols.size());
    for (size_t index = 0; index < recentSymbols.size(); ++index) {
        recentKeys.push_back(NormalizeSymbolSearchKey(recentSymbols[index]));
        recentRank.try_emplace(recentKeys.back(), index);
    }

    std::vector<std::string> watchlistKeysInOrder;
    watchlistKeysInOrder.reserve(watchlistSymbols.size());
    std::unordered_set<std::string> watchlistKeys;
    watchlistKeys.reserve(watchlistSymbols.size());
    for (const std::string& symbol : watchlistSymbols) {
        watchlistKeysInOrder.push_back(NormalizeSymbolSearchKey(symbol));
        watchlistKeys.insert(watchlistKeysInOrder.back());
    }

    const auto appendKnownSymbol = [&](const std::string& symbol,
                                       const std::string& symbolKey) {
        if (resultKeys.contains(symbolKey))
            return;
        const auto nameIt = recentSymbolNames.find(symbol);
        const std::string& name =
            nameIt != recentSymbolNames.end() ? nameIt->second : symbol;
        if (ScoreMatch(parsed, symbol, name, {}) <= 0)
            return;
        results.emplace_back(symbol, name.empty() ? symbol : name);
        resultKeys.insert(symbolKey);
    };

    for (size_t index = 0; index < recentSymbols.size(); ++index)
        appendKnownSymbol(recentSymbols[index], recentKeys[index]);
    for (size_t index = 0; index < watchlistSymbols.size(); ++index)
        appendKnownSymbol(watchlistSymbols[index], watchlistKeysInOrder[index]);

    RemoveAmbiguousPlaceholderMatches(query, results);

    struct PersonalizedRank {
        size_t originalIndex = 0;
        int score = 0;
    };
    std::vector<PersonalizedRank> order;
    order.reserve(results.size());

    for (size_t index = 0; index < results.size(); ++index) {
        const auto& [symbol, description] = results[index];
        const int baseRelevance = ScoreMatch(parsed, symbol, description, {});
        if (baseRelevance <= 0)
            continue;

        int score = baseRelevance * 10;
        const std::string symbolKey = NormalizeSymbolSearchKey(symbol);
        if (const auto recent = recentRank.find(symbolKey); recent != recentRank.end())
            score += std::max(320, 920 - static_cast<int>(recent->second) * 85);
        if (watchlistKeys.contains(symbolKey))
            score += 360;
        order.push_back({index, score});
    }

    // Preserve provider order as a small tie-break among retained candidates.
    for (size_t index = 0; index < order.size(); ++index) {
        order[index].score +=
            static_cast<int>(std::min<size_t>(order.size() - index, 20));
    }

    const size_t keep = std::min<size_t>(10, order.size());
    const auto better = [](const PersonalizedRank& a, const PersonalizedRank& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.originalIndex < b.originalIndex;
    };
    std::partial_sort(order.begin(),
                      order.begin() + static_cast<std::ptrdiff_t>(keep),
                      order.end(),
                      better);

    std::vector<std::pair<std::string, std::string>> reordered;
    reordered.reserve(keep);
    for (size_t i = 0; i < keep; ++i)
        reordered.push_back(std::move(results[order[i].originalIndex]));
    results = std::move(reordered);
}

} // namespace squarestar::search
