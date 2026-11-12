#include <basalt/hoist/instance/Observer.hpp>

#include <fstream>
#include <sstream>

namespace basalt::hoist::instance {
namespace {

constexpr int kServerApiPort = 25580;

std::string readToken(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    std::string token = text.str();
    while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' ')) {
        token.pop_back();
    }
    return token;
}

} // namespace

void Observer::problem(const std::string& key, const std::string& message) {
    std::string& last = key == "api" ? lastApiProblem_ : lastPingProblem_;
    if (last != message) {
        last = message;
        notify_(message);
    }
}

std::optional<Status> Observer::queryApi() {
    if (!settings_.api.enabled) {
        return std::nullopt;
    }
    int port = settings_.api.port;
    std::filesystem::path tokenFile = settings_.api.tokenFile;
    if (port == 0 && target_ == config::Target::Server) {
        port = kServerApiPort;
    }
    if (tokenFile.empty()) {
        tokenFile = target_ == config::Target::Server ? ".basalt/remote-token" : ".basalt/api.token";
    }
    if (tokenFile.is_relative()) {
        tokenFile = directory_ / tokenFile;
    }
    const std::string unconfigured =
        "The product's API is not configured (status.toml [api]); the player count falls back to the server list ping, "
        "and placeholders such as %tps% have no value until it is.";
    if (port == 0) {
        problem("api", unconfigured + " Set its port.");
        return std::nullopt;
    }
    const std::string token = readToken(tokenFile);
    if (token.empty()) {
        problem("api", unconfigured + " No token found at " + tokenFile.string() + ".");
        return std::nullopt;
    }
    net::HttpRequest request;
    request.url = "http://" + settings_.api.host + ":" + std::to_string(port) + "/v1/status";
    request.headers = {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}};
    request.idleTimeoutSeconds = settings_.timeoutSeconds;
    request.totalTimeoutSeconds = settings_.timeoutSeconds * 2;
    request.maxBodyBytes = 1u << 20;
    auto response = net::httpGet(*http_, request);
    if (!response) {
        problem("api", unconfigured + " It did not answer: " + response.error());
        return std::nullopt;
    }
    if (response->status == 401 || response->status == 403) {
        problem("api", unconfigured + " It refused the token.");
        return std::nullopt;
    }
    if (response->status != 200) {
        problem("api", unconfigured + " It answered " + std::to_string(response->status) + ".");
        return std::nullopt;
    }
    auto status = parseApiStatus(response->body);
    if (!status) {
        problem("api", unconfigured + " Its answer was not understood.");
        return std::nullopt;
    }
    lastApiProblem_.clear();
    return status;
}

std::optional<Status> Observer::queryPing() {
    if (!settings_.ping.enabled) {
        return std::nullopt;
    }
    auto connection = net::connectTcp(settings_.ping.host, settings_.ping.port);
    if (!connection || !connection->send(buildPingRequest(settings_.ping.host, settings_.ping.port))) {
        problem("ping", "The server list ping to " + settings_.ping.host + ":" + std::to_string(settings_.ping.port) +
                            " got no answer; the player count has no value.");
        return std::nullopt;
    }
    std::string received;
    for (int rounds = 0; rounds < 64; ++rounds) {
        if (auto json = extractPingJson(received)) {
            if (auto status = parsePingResponse(*json)) {
                lastPingProblem_.clear();
                return status;
            }
            break;
        }
        auto chunk = connection->receive(4096, settings_.timeoutSeconds * 1000);
        if (!chunk || chunk->empty()) {
            break;
        }
        received += *chunk;
    }
    problem("ping", "The server list ping answered something Hoist cannot read; the player count has no value.");
    return std::nullopt;
}

Status Observer::query() {
    if (auto status = queryApi()) {
        return *status;
    }
    if (auto status = queryPing()) {
        return *status;
    }
    return {};
}

} // namespace basalt::hoist::instance
