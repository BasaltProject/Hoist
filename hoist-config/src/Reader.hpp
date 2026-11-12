#pragma once

#include <basalt/hoist/config/Config.hpp>

#include <optional>
#include <set>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

namespace basalt::hoist::config {

class Reader {
public:
    Reader(const toml::table& table, std::string file, std::string prefix, ConfigError& error,
           std::vector<std::string>& warnings)
        : table_(&table), file_(std::move(file)), prefix_(std::move(prefix)), error_(&error), warnings_(&warnings) {}

    bool failed() const { return !error_->message.empty(); }

    void fail(std::string_view key, std::string message) const;

    std::string text(std::string_view key, std::string fallback) const;
    bool flag(std::string_view key, bool fallback) const;
    int integer(std::string_view key, int fallback, int min, int max) const;
    std::vector<std::string> list(std::string_view key) const;
    std::optional<Reader> section(std::string_view key) const;
    std::vector<Reader> tables(std::string_view key) const;
    void finish() const;

private:
    std::string path(std::string_view key) const {
        return prefix_.empty() ? std::string(key) : prefix_ + "." + std::string(key);
    }

    const toml::node* find(std::string_view key) const;

    const toml::table* table_;
    std::string file_;
    std::string prefix_;
    ConfigError* error_;
    std::vector<std::string>* warnings_;
    mutable std::set<std::string, std::less<>> used_;
};

std::expected<toml::table, ConfigError> parseToml(std::string_view text, const std::string& file);

} // namespace basalt::hoist::config
