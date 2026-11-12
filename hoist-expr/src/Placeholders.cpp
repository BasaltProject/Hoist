#include <basalt/hoist/expr/Placeholders.hpp>

#include <charconv>
#include <cmath>

namespace basalt::hoist::expr {
namespace {

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

std::string toText(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    const double number = std::get<double>(value);
    char buffer[64];
    auto result = number == std::trunc(number) && std::fabs(number) < 1e15
                      ? std::to_chars(buffer, buffer + sizeof buffer, static_cast<long long>(number))
                      : std::to_chars(buffer, buffer + sizeof buffer, number);
    return std::string(buffer, result.ptr);
}

void Placeholders::set(std::string name, Value value) { values_.insert_or_assign(std::move(name), std::move(value)); }

void Placeholders::unset(std::string_view name) {
    if (auto it = values_.find(name); it != values_.end()) {
        values_.erase(it);
    }
}

std::optional<Value> Placeholders::get(std::string_view name) const {
    if (auto it = values_.find(name); it != values_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string Placeholders::format(std::string_view text, std::vector<std::string>* missing) const {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out += text[i];
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        while (end < text.size() && isNameChar(text[end])) {
            ++end;
        }
        if (end == i + 1 || end >= text.size() || text[end] != '%') {
            out += '%';
            continue;
        }
        const auto name = text.substr(i + 1, end - i - 1);
        if (auto value = get(name)) {
            out += toText(*value);
        } else {
            out += text.substr(i, end - i + 1);
            if (missing) {
                missing->emplace_back(name);
            }
        }
        i = end;
    }
    return out;
}

} // namespace basalt::hoist::expr
