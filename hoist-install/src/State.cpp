#include <basalt/hoist/install/Install.hpp>

#include <fstream>
#include <sstream>
#include <toml++/toml.hpp>

namespace basalt::hoist::install {
namespace {

toml::table toTable(const Record& record) {
    return toml::table{{"path", record.path}, {"version", record.version}, {"published-at", record.publishedAt}};
}

Record fromTable(const toml::table& table) {
    return {table["path"].value_or(std::string{}), table["version"].value_or(std::string{}),
            table["published-at"].value_or(std::int64_t{0})};
}

} // namespace

std::expected<State, std::string> loadState(const std::filesystem::path& file) {
    State state;
    if (!std::filesystem::exists(file)) {
        return state;
    }
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    try {
        const auto table = toml::parse(text.str(), file.string());
        if (const auto* installed = table["installed"].as_table()) {
            state.installed = fromTable(*installed);
        }
        if (const auto* rejected = table["rejected"].as_table()) {
            state.rejected = fromTable(*rejected);
        }
        if (const auto* previous = table["previous"].as_array()) {
            for (const auto& item : *previous) {
                if (const auto* entry = item.as_table()) {
                    state.previous.push_back(fromTable(*entry));
                }
            }
        }
        state.etag = table["etag"].value_or(std::string{});
        state.lastCheck = table["last-check"].value_or(std::int64_t{0});
    } catch (const toml::parse_error& error) {
        return std::unexpected(file.string() + " is unreadable: " + std::string(error.description()));
    }
    return state;
}

std::expected<void, std::string> saveState(const std::filesystem::path& file, const State& state) {
    toml::table table;
    if (state.installed) {
        table.insert("installed", toTable(*state.installed));
    }
    if (state.rejected) {
        table.insert("rejected", toTable(*state.rejected));
    }
    toml::array previous;
    for (const auto& record : state.previous) {
        previous.push_back(toTable(record));
    }
    table.insert("previous", std::move(previous));
    table.insert("etag", state.etag);
    table.insert("last-check", state.lastCheck);

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto temporary = std::filesystem::path(file).concat(".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << "# Written by Hoist. Not configuration: edit config/hoist/ instead.\n" << table << "\n";
        if (!out) {
            return std::unexpected("cannot write " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        return std::unexpected("cannot replace " + file.string() + ": " + ec.message());
    }
    return {};
}

} // namespace basalt::hoist::install
