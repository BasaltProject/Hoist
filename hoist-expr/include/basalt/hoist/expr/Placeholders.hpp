#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace basalt::hoist::expr {

using Value = std::variant<double, std::string, bool>;

std::string toText(const Value& value);

// The values a %name% can stand for. Names carry no prefix: Hoist takes no third-party placeholders.
class Placeholders {
public:
    void set(std::string name, Value value);
    void unset(std::string_view name);
    std::optional<Value> get(std::string_view name) const;

    // Replaces every %name% it knows; the names it does not know stay as written and are listed in `missing`.
    std::string format(std::string_view text, std::vector<std::string>* missing = nullptr) const;

private:
    std::map<std::string, Value, std::less<>> values_;
};

} // namespace basalt::hoist::expr
