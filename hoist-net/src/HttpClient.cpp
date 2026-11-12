#include <basalt/hoist/net/Http.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <thread>

namespace basalt::hoist::net {

std::string Url::origin() const { return scheme + "://" + host + ":" + std::to_string(port); }

std::optional<Url> parseUrl(std::string_view text) {
    Url url;
    if (text.starts_with("https://")) {
        url.scheme = "https";
        url.port = 443;
        text.remove_prefix(8);
    } else if (text.starts_with("http://")) {
        url.scheme = "http";
        url.port = 80;
        text.remove_prefix(7);
    } else {
        return std::nullopt;
    }
    const auto slash = text.find_first_of("/?");
    std::string_view authority = text.substr(0, slash);
    url.target = slash == std::string_view::npos ? "/" : std::string(text.substr(slash));
    if (url.target.front() == '?') {
        url.target.insert(0, "/");
    }
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view portText;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        url.host = std::string(authority.substr(1, close - 1));
        if (authority.size() > close + 1) {
            if (authority[close + 1] != ':') {
                return std::nullopt;
            }
            portText = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        url.host = std::string(authority.substr(0, colon));
        if (colon != std::string_view::npos) {
            portText = authority.substr(colon + 1);
        }
    }
    if (!portText.empty()) {
        auto [end, error] = std::from_chars(portText.data(), portText.data() + portText.size(), url.port);
        if (error != std::errc{} || end != portText.data() + portText.size() || url.port < 1 || url.port > 65535) {
            return std::nullopt;
        }
    }
    return url.host.empty() ? std::nullopt : std::optional(url);
}

std::optional<std::string> resolveLocation(const Url& base, std::string_view location) {
    if (location.starts_with("http://") || location.starts_with("https://")) {
        return std::string(location);
    }
    const bool defaultPort = (base.scheme == "https" && base.port == 443) || (base.scheme == "http" && base.port == 80);
    const std::string host = base.host.find(':') != std::string::npos ? "[" + base.host + "]" : base.host;
    const std::string origin = base.scheme + "://" + host + (defaultPort ? "" : ":" + std::to_string(base.port));
    if (location.starts_with('/')) {
        return origin + std::string(location);
    }
    if (location.empty()) {
        return std::nullopt;
    }
    std::string directory = base.target.substr(0, base.target.find('?'));
    directory.erase(directory.rfind('/') + 1);
    return origin + directory + std::string(location);
}

namespace {

bool isRedirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::expected<HttpResponse, std::string> follow(HttpClient& client, HttpRequest request, const BodySink& sink) {
    for (int hops = 0; hops <= 5; ++hops) {
        auto response = client.request(request, sink);
        if (!response || !isRedirect(response->status)) {
            return response;
        }
        const auto location = response->headers.find("location");
        const auto from = parseUrl(request.url);
        if (location == response->headers.end() || !from) {
            return std::unexpected("a redirect came without a usable Location");
        }
        auto next = resolveLocation(*from, location->second);
        const auto to = next ? parseUrl(*next) : std::nullopt;
        if (!to) {
            return std::unexpected("a redirect pointed at an address Hoist cannot follow");
        }
        if (to->host != from->host || to->scheme != from->scheme || to->port != from->port) {
            std::erase_if(request.headers, [](const auto& header) {
                std::string name = header.first;
                std::ranges::transform(name, name.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return name == "authorization";
            });
        }
        request.url = *next;
    }
    return std::unexpected("too many redirects");
}

} // namespace

std::expected<HttpResponse, std::string> httpGet(HttpClient& client, HttpRequest request) {
    std::string body;
    const std::size_t limit = request.maxBodyBytes;
    auto response = follow(client, std::move(request), [&](std::string_view chunk) {
        body.append(chunk);
        return body.size() <= limit;
    });
    if (response && response->status >= 200 && response->status < 300) {
        if (body.size() > limit) {
            return std::unexpected("the answer is larger than the limit");
        }
        response->body = std::move(body);
    }
    return response;
}

std::expected<HttpResponse, std::string> httpDownload(HttpClient& client, HttpRequest request,
                                                      const std::filesystem::path& file, bool resume) {
    std::error_code ec;
    const std::size_t have = resume ? static_cast<std::size_t>(std::filesystem::file_size(file, ec)) : 0;
    const bool resuming = resume && !ec && have > 0;
    std::ofstream out(file, resuming ? std::ios::binary | std::ios::app : std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected("cannot write " + file.string());
    }
    if (resuming) {
        request.headers.emplace_back("Range", "bytes=" + std::to_string(have) + "-");
    }
    std::size_t written = have;
    std::size_t received = 0;
    const auto started = std::chrono::steady_clock::now();
    const std::size_t rate = request.bytesPerSecond;
    const std::size_t limit = request.maxBodyBytes;
    auto response = follow(client, std::move(request), [&](std::string_view chunk) {
        out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        written += chunk.size();
        received += chunk.size();
        if (rate > 0) {
            const auto due =
                started + std::chrono::duration<double>(static_cast<double>(received) / static_cast<double>(rate));
            std::this_thread::sleep_until(due);
        }
        return static_cast<bool>(out) && written <= limit;
    });
    out.close();
    if (resuming && response && response->status == 200) {
        return std::unexpected("the server ignored the request to resume");
    }
    if (response && response->status >= 200 && response->status < 300 && (!out || written > limit)) {
        return std::unexpected(written > limit ? "the download is larger than the limit"
                                               : "cannot write " + file.string());
    }
    return response;
}

} // namespace basalt::hoist::net
