#include "services/symbol_search_service.hpp"

#include "services/api_key_store.hpp"
#include "services/http_client.hpp"
#include "services/network_runtime.hpp"

#include <string>

namespace squarestar::search {
namespace {

SymbolSearchService& RuntimeService() {
    static SymbolSearchService service({
        [] { return squarestar::secrets::GetFinnhubApiKey(); },
        [](std::string_view value) {
            return squarestar::http::UrlEncode(std::string(value));
        },
        [](std::string url) {
            // Route HTTP through the shared lane; the search worker only parses and ranks.
            const squarestar::http::HttpResponse response =
                QueueRealtimeHttpGet(std::move(url)).get();
            return SymbolSearchHttpResponse{
                response.body, response.statusCode, response.IsSuccess()};
        },
    });
    return service;
}

} // namespace

SymbolSearchService::Results LookupSymbols(std::string_view query) {
    return RuntimeService().Lookup(query, false);
}

void ClearSymbolSearchCache() {
    RuntimeService().ClearCache();
}

} // namespace squarestar::search
