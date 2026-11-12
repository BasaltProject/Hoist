#pragma once

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/net/Http.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace basalt::hoist::instance {

struct Status {
    enum class Source { None, Api, Ping } source = Source::None;
    std::optional<int> playersOnline;
    std::optional<int> playersMax;
    std::optional<double> tps;
    std::optional<std::int64_t> uptimeSeconds;
};

std::optional<Status> parseApiStatus(std::string_view json);
std::optional<Status> parsePingResponse(std::string_view json);

std::string buildPingRequest(std::string_view host, int port);
std::optional<std::string> extractPingJson(std::string_view bytes);

class Observer {
public:
    using Notify = std::function<void(const std::string&)>;

    Observer(config::Target target, config::StatusConfig settings, std::filesystem::path workingDirectory,
             net::HttpClient& http, Notify notify)
        : target_(target), settings_(std::move(settings)), directory_(std::move(workingDirectory)), http_(&http),
          notify_(std::move(notify)) {}

    Status query();

private:
    std::optional<Status> queryApi();
    std::optional<Status> queryPing();
    void problem(const std::string& key, const std::string& message);

    config::Target target_;
    config::StatusConfig settings_;
    std::filesystem::path directory_;
    net::HttpClient* http_;
    Notify notify_;
    std::string lastApiProblem_;
    std::string lastPingProblem_;
};

} // namespace basalt::hoist::instance
