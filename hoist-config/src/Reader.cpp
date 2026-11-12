#include "Reader.hpp"

namespace basalt::hoist::config {

std::expected<toml::table, ConfigError> parseToml(std::string_view text, const std::string& file) {
    try {
        return toml::parse(text, file);
    } catch (const toml::parse_error& parseError) {
        const auto& where = parseError.source().begin;
        return std::unexpected(ConfigError{file,
                                           {},
                                           std::string(parseError.description()) + " (line " +
                                               std::to_string(where.line) + ", column " + std::to_string(where.column) +
                                               ")"});
    }
}

void Reader::fail(std::string_view key, std::string message) const {
    if (!failed()) {
        *error_ = ConfigError{file_, path(key), std::move(message)};
    }
}

const toml::node* Reader::find(std::string_view key) const {
    used_.insert(std::string(key));
    return table_->get(key);
}

std::string Reader::text(std::string_view key, std::string fallback) const {
    const auto* node = find(key);
    if (!node) {
        return fallback;
    }
    if (auto value = node->value<std::string>()) {
        return *value;
    }
    fail(key, "must be text");
    return fallback;
}

bool Reader::flag(std::string_view key, bool fallback) const {
    const auto* node = find(key);
    if (!node) {
        return fallback;
    }
    if (auto value = node->value<bool>()) {
        return *value;
    }
    fail(key, "must be true or false");
    return fallback;
}

int Reader::integer(std::string_view key, int fallback, int min, int max) const {
    const auto* node = find(key);
    if (!node) {
        return fallback;
    }
    auto value = node->value<std::int64_t>();
    if (!value) {
        fail(key, "must be a whole number");
        return fallback;
    }
    if (*value < min || *value > max) {
        fail(key, "must be between " + std::to_string(min) + " and " + std::to_string(max));
        return fallback;
    }
    return static_cast<int>(*value);
}

std::vector<std::string> Reader::list(std::string_view key) const {
    std::vector<std::string> out;
    const auto* node = find(key);
    if (!node) {
        return out;
    }
    const auto* array = node->as_array();
    if (!array) {
        fail(key, "must be a list of text");
        return out;
    }
    for (const auto& item : *array) {
        auto value = item.value<std::string>();
        if (!value) {
            fail(key, "must be a list of text");
            return out;
        }
        out.push_back(*value);
    }
    return out;
}

std::optional<Reader> Reader::section(std::string_view key) const {
    const auto* node = find(key);
    if (!node) {
        return std::nullopt;
    }
    if (const auto* sub = node->as_table()) {
        return Reader(*sub, file_, path(key), *error_, *warnings_);
    }
    fail(key, "must be a table");
    return std::nullopt;
}

std::vector<Reader> Reader::tables(std::string_view key) const {
    std::vector<Reader> out;
    const auto* node = find(key);
    if (!node) {
        return out;
    }
    const auto* array = node->as_array();
    if (!array) {
        fail(key, "must be a list of [[" + std::string(key) + "]] tables");
        return out;
    }
    int index = 0;
    for (const auto& item : *array) {
        const auto* sub = item.as_table();
        if (!sub) {
            fail(key, "must be a list of [[" + std::string(key) + "]] tables");
            return out;
        }
        out.emplace_back(*sub, file_, path(key) + "[" + std::to_string(index++) + "]", *error_, *warnings_);
    }
    return out;
}

void Reader::finish() const {
    for (const auto& [key, node] : *table_) {
        if (!used_.contains(std::string(key.str()))) {
            warnings_->push_back(file_ + ": unknown key '" + path(key.str()) + "' ignored");
        }
    }
}

} // namespace basalt::hoist::config
