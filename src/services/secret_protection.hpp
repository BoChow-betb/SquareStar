#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace squarestar::secrets {

void SecureClear(std::string& value) noexcept;

class ScopedSecureClear {
  public:
    explicit ScopedSecureClear(std::string& value) noexcept : value_(&value) {}
    ScopedSecureClear(const ScopedSecureClear&) = delete;
    ScopedSecureClear& operator=(const ScopedSecureClear&) = delete;
    ~ScopedSecureClear() {
        if (value_)
            SecureClear(*value_);
    }

  private:
    std::string* value_ = nullptr;
};

std::optional<std::string> ProtectApiKey(std::string_view key);
std::optional<std::string> UnprotectApiKey(std::string_view protectedKey);


std::optional<std::string> ProtectLocalState(std::string_view plaintext);
std::optional<std::string> UnprotectLocalState(std::string_view protectedState);

}
