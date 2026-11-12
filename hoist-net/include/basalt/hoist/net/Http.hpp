#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace basalt::hoist::net {

struct Url {
    std::string scheme;
    std::string host;
    int port = 0;
    std::string target; // path and query, always starting with /

    std::string origin() const;
};

std::optional<Url> parseUrl(std::string_view text);
std::optional<std::string> resolveLocation(const Url& base, std::string_view location);

struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    int idleTimeoutSeconds = 20;
    int totalTimeoutSeconds = 0; // 0 means no overall limit
    std::size_t maxBodyBytes = 16u << 20;
    std::size_t bytesPerSecond = 0; // downloads only; 0 means unlimited
};

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers; // lower-case names
    std::string body;                           // empty when the body went to a sink
};

using BodySink = std::function<bool(std::string_view)>;

class HttpClient {
public:
    virtual ~HttpClient() = default;
    // One request, no redirects. Answers that are not 2xx keep up to 64 KiB of body in the response.
    virtual std::expected<HttpResponse, std::string> request(const HttpRequest& request, const BodySink& sink) = 0;
};

// Follow up to five redirects and never send the Authorization header to another host.
std::expected<HttpResponse, std::string> httpGet(HttpClient& client, HttpRequest request);
// With `resume`, a file already there is continued with a Range request; a server that ignores it is an error.
std::expected<HttpResponse, std::string> httpDownload(HttpClient& client, HttpRequest request,
                                                      const std::filesystem::path& file, bool resume = false);

std::unique_ptr<HttpClient> makeNativeHttpClient(std::string caFile);

class TcpConnection {
public:
    virtual ~TcpConnection() = default;
    virtual bool send(std::string_view data) = 0;
    virtual std::optional<std::string> receive(std::size_t maxBytes, int timeoutMs) = 0;
};

std::unique_ptr<TcpConnection> connectTcp(const std::string& host, int port);

} // namespace basalt::hoist::net
