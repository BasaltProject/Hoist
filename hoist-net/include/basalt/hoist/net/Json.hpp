#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace basalt::hoist::net {

struct Json {
    enum class Kind { Null, Bool, Number, Text, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<std::string> keys;
    std::vector<Json> values; // the items of an Array, or the values of an Object (values[i] belongs to keys[i])

    const Json* find(std::string_view key) const;
    std::string stringOr(std::string_view key, std::string fallback = {}) const;
    bool boolOr(std::string_view key, bool fallback = false) const;
    double numberOr(std::string_view key, double fallback = 0) const;
};

std::expected<Json, std::string> parseJson(std::string_view text);

} // namespace basalt::hoist::net
