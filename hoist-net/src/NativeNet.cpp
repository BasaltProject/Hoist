#include <basalt/hoist/net/Http.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mutex>
#include <psa/crypto.h>

namespace basalt::hoist::net {
namespace {

using Clock = std::chrono::steady_clock;

std::string errorText(int code) {
    std::array<char, 160> buffer{};
    mbedtls_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}

void ignoreBrokenPipes() {
#ifndef _WIN32
    static const bool once = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
#endif
}

class Tcp final : public TcpConnection {
public:
    Tcp() { mbedtls_net_init(&context_); }

    ~Tcp() override { mbedtls_net_free(&context_); }

    Tcp(const Tcp&) = delete;
    Tcp& operator=(const Tcp&) = delete;

    std::expected<void, std::string> connect(const std::string& host, int port) {
        ignoreBrokenPipes();
        const int result =
            mbedtls_net_connect(&context_, host.c_str(), std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP);
        if (result != 0) {
            return std::unexpected("cannot connect to " + host + ":" + std::to_string(port) + " (" + errorText(result) +
                                   ")");
        }
        return {};
    }

    bool send(std::string_view data) override {
        while (!data.empty()) {
            const int sent =
                mbedtls_net_send(&context_, reinterpret_cast<const unsigned char*>(data.data()), data.size());
            if (sent <= 0) {
                return false;
            }
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
        return true;
    }

    std::optional<std::string> receive(std::size_t maxBytes, int timeoutMs) override {
        std::string buffer(maxBytes, '\0');
        const int got = mbedtls_net_recv_timeout(&context_, reinterpret_cast<unsigned char*>(buffer.data()),
                                                 buffer.size(), static_cast<std::uint32_t>(timeoutMs));
        if (got < 0) {
            return std::nullopt;
        }
        buffer.resize(static_cast<std::size_t>(got));
        return buffer;
    }

    mbedtls_net_context* raw() { return &context_; }

private:
    mbedtls_net_context context_;
};

class TlsSettings {
public:
    explicit TlsSettings(std::string caFile) : caFile_(std::move(caFile)) {
        mbedtls_ssl_config_init(&config_);
        mbedtls_x509_crt_init(&roots_);
        mbedtls_entropy_init(&entropy_);
        mbedtls_ctr_drbg_init(&random_);
    }

    ~TlsSettings() {
        mbedtls_ssl_config_free(&config_);
        mbedtls_x509_crt_free(&roots_);
        mbedtls_ctr_drbg_free(&random_);
        mbedtls_entropy_free(&entropy_);
    }

    TlsSettings(const TlsSettings&) = delete;
    TlsSettings& operator=(const TlsSettings&) = delete;

    std::expected<mbedtls_ssl_config*, std::string> prepare(int idleMs) {
        if (!ready_) {
            if (auto result = initialise(); !result) {
                return std::unexpected(result.error());
            }
            ready_ = true;
        }
        mbedtls_ssl_conf_read_timeout(&config_, static_cast<std::uint32_t>(idleMs));
        return &config_;
    }

private:
    std::expected<void, std::string> initialise() {
        if (psa_crypto_init() != PSA_SUCCESS) {
            return std::unexpected("cannot start the crypto library");
        }
        if (int r = mbedtls_ctr_drbg_seed(&random_, mbedtls_entropy_func, &entropy_, nullptr, 0); r != 0) {
            return std::unexpected("cannot seed the random generator (" + errorText(r) + ")");
        }
        if (int r = mbedtls_ssl_config_defaults(&config_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                MBEDTLS_SSL_PRESET_DEFAULT);
            r != 0) {
            return std::unexpected("cannot configure TLS (" + errorText(r) + ")");
        }
        int loaded = 0;
        if (!caFile_.empty()) {
            if (int r = mbedtls_x509_crt_parse_file(&roots_, caFile_.c_str()); r < 0) {
                return std::unexpected("cannot read the certificates in " + caFile_ + " (" + errorText(r) + ")");
            }
            loaded = 1;
        } else {
            for (const char* path :
                 {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem",
                  "/etc/ssl/cert.pem", "/usr/local/share/certs/ca-root-nss.crt", "/etc/ssl/certs"}) {
                const bool directory = std::string_view(path) == "/etc/ssl/certs";
                if (loaded > 0 && directory) {
                    continue;
                }
                const int r =
                    directory ? mbedtls_x509_crt_parse_path(&roots_, path) : mbedtls_x509_crt_parse_file(&roots_, path);
                if (r >= 0 && roots_.raw.p != nullptr) {
                    ++loaded;
                    if (!directory) {
                        break;
                    }
                }
            }
        }
        if (loaded == 0) {
            return std::unexpected("no trusted certificates found on this host; set ca-file in updates.toml");
        }
        mbedtls_ssl_conf_authmode(&config_, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config_, &roots_, nullptr);
        mbedtls_ssl_conf_rng(&config_, mbedtls_ctr_drbg_random, &random_);
        return {};
    }

    std::string caFile_;
    mbedtls_ssl_config config_;
    mbedtls_x509_crt roots_;
    mbedtls_entropy_context entropy_;
    mbedtls_ctr_drbg_context random_;
    bool ready_ = false;
};

class Session {
public:
    ~Session() { mbedtls_ssl_free(&ssl_); }

    Session() { mbedtls_ssl_init(&ssl_); }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::expected<void, std::string> secure(Tcp& tcp, mbedtls_ssl_config* config, const std::string& host) {
        if (int r = mbedtls_ssl_setup(&ssl_, config); r != 0) {
            return std::unexpected("cannot start TLS (" + errorText(r) + ")");
        }
        if (int r = mbedtls_ssl_set_hostname(&ssl_, host.c_str()); r != 0) {
            return std::unexpected("cannot set the host name (" + errorText(r) + ")");
        }
        mbedtls_ssl_set_bio(&ssl_, tcp.raw(), mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
        int r = 0;
        while ((r = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return std::unexpected("TLS handshake with " + host + " failed (" + errorText(r) + ")");
            }
        }
        tls_ = true;
        return {};
    }

    bool tls() const { return tls_; }

    mbedtls_ssl_context* ssl() { return &ssl_; }

private:
    mbedtls_ssl_context ssl_;
    bool tls_ = false;
};

struct Channel {
    Tcp& tcp;
    Session& session;

    bool write(std::string_view data) {
        if (!session.tls()) {
            return tcp.send(data);
        }
        while (!data.empty()) {
            const int sent =
                mbedtls_ssl_write(session.ssl(), reinterpret_cast<const unsigned char*>(data.data()), data.size());
            if (sent == MBEDTLS_ERR_SSL_WANT_READ || sent == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (sent <= 0) {
                return false;
            }
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
        return true;
    }

    // Bytes read, 0 at the end of the stream, -1 on an error or a timeout.
    int read(unsigned char* buffer, std::size_t size, int idleMs) {
        if (!session.tls()) {
            const int got = mbedtls_net_recv_timeout(tcp.raw(), buffer, size, static_cast<std::uint32_t>(idleMs));
            return got < 0 ? -1 : got;
        }
        while (true) {
            const int got = mbedtls_ssl_read(session.ssl(), buffer, size);
            if (got == MBEDTLS_ERR_SSL_WANT_READ || got == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (got == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || got == 0) {
                return 0;
            }
            return got < 0 ? -1 : got;
        }
    }
};

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

class ResponseReader {
public:
    ResponseReader(Channel& channel, int idleMs, std::optional<Clock::time_point> deadline)
        : channel_(channel), idleMs_(idleMs), deadline_(deadline) {}

    enum class Fill { Data, End, Failed };

    Fill fill() {
        if (deadline_ && Clock::now() > *deadline_) {
            return Fill::Failed;
        }
        std::array<unsigned char, 16384> chunk;
        const int got = channel_.read(chunk.data(), chunk.size(), idleMs_);
        if (got < 0) {
            return Fill::Failed;
        }
        if (got == 0) {
            return Fill::End;
        }
        buffer_.append(reinterpret_cast<const char*>(chunk.data()), static_cast<std::size_t>(got));
        return Fill::Data;
    }

    std::string& buffer() { return buffer_; }

    std::optional<std::string> line() {
        while (true) {
            if (const auto end = buffer_.find("\r\n"); end != std::string::npos) {
                std::string out = buffer_.substr(0, end);
                buffer_.erase(0, end + 2);
                return out;
            }
            if (buffer_.size() > 65536 || fill() != Fill::Data) {
                return std::nullopt;
            }
        }
    }

    bool take(std::size_t count, const std::function<bool(std::string_view)>& deliver) {
        while (count > 0) {
            if (buffer_.empty() && fill() != Fill::Data) {
                return false;
            }
            const std::size_t part = std::min(count, buffer_.size());
            if (!deliver(std::string_view(buffer_).substr(0, part))) {
                return false;
            }
            buffer_.erase(0, part);
            count -= part;
        }
        return true;
    }

private:
    Channel& channel_;
    int idleMs_;
    std::optional<Clock::time_point> deadline_;
    std::string buffer_;
};

class NativeHttpClient final : public HttpClient {
public:
    explicit NativeHttpClient(std::string caFile) : tls_(std::move(caFile)) {}

    std::expected<HttpResponse, std::string> request(const HttpRequest& request, const BodySink& sink) override {
        std::lock_guard lock(mutex_);
        const auto url = parseUrl(request.url);
        if (!url) {
            return std::unexpected("not an http or https address: " + request.url);
        }
        const int idleMs = request.idleTimeoutSeconds * 1000;
        std::optional<Clock::time_point> deadline;
        if (request.totalTimeoutSeconds > 0) {
            deadline = Clock::now() + std::chrono::seconds(request.totalTimeoutSeconds);
        }
        Tcp tcp;
        if (auto connected = tcp.connect(url->host, url->port); !connected) {
            return std::unexpected(connected.error());
        }
        Session session;
        if (url->scheme == "https") {
            auto config = tls_.prepare(idleMs);
            if (!config) {
                return std::unexpected(config.error());
            }
            if (auto secured = session.secure(tcp, *config, url->host); !secured) {
                return std::unexpected(secured.error());
            }
        }
        Channel channel{tcp, session};
        const bool defaultPort =
            (url->scheme == "https" && url->port == 443) || (url->scheme == "http" && url->port == 80);
        const std::string hostName = url->host.find(':') != std::string::npos ? "[" + url->host + "]" : url->host;
        std::string text =
            "GET " + url->target + " HTTP/1.1\r\nHost: " + hostName +
            (defaultPort ? "" : ":" + std::to_string(url->port)) +
            "\r\nUser-Agent: BasaltHoist\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
        for (const auto& [name, value] : request.headers) {
            text += name + ": " + value + "\r\n";
        }
        text += "\r\n";
        if (!channel.write(text)) {
            return std::unexpected("cannot send the request to " + url->host);
        }
        ResponseReader reader(channel, idleMs, deadline);
        return readResponse(reader, sink);
    }

private:
    static std::expected<HttpResponse, std::string> readResponse(ResponseReader& reader, const BodySink& sink) {
        HttpResponse response;
        auto status = reader.line();
        if (!status || !status->starts_with("HTTP/") || status->size() < 12) {
            return std::unexpected("no valid answer from the server");
        }
        std::from_chars(status->data() + 9, status->data() + 12, response.status);
        while (true) {
            auto header = reader.line();
            if (!header) {
                return std::unexpected("the answer ended inside its headers");
            }
            if (header->empty()) {
                break;
            }
            if (const auto colon = header->find(':'); colon != std::string::npos) {
                response.headers[lower(header->substr(0, colon))] =
                    std::string(trim(std::string_view(*header).substr(colon + 1)));
            }
        }
        const bool success = response.status >= 200 && response.status < 300;
        bool declined = false;
        const auto deliver = [&](std::string_view chunk) {
            if (success) {
                if (!sink(chunk)) {
                    declined = true;
                    return false;
                }
            } else if (response.body.size() < 65536) {
                response.body.append(chunk.substr(0, 65536 - response.body.size()));
            }
            return true;
        };
        if (response.status == 204 || response.status == 304 || (response.status >= 100 && response.status < 200)) {
            return response;
        }
        const auto encoding = response.headers.find("transfer-encoding");
        const auto length = response.headers.find("content-length");
        bool complete = true;
        if (encoding != response.headers.end() && lower(encoding->second).find("chunked") != std::string::npos) {
            complete = readChunked(reader, deliver);
        } else if (length != response.headers.end()) {
            std::size_t size = 0;
            const auto& value = length->second;
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), size);
            if (error != std::errc{}) {
                return std::unexpected("the server sent an invalid Content-Length");
            }
            complete = reader.take(size, deliver);
        } else {
            while (true) {
                if (!deliver(reader.buffer())) {
                    break;
                }
                reader.buffer().clear();
                const auto fill = reader.fill();
                if (fill == ResponseReader::Fill::End) {
                    break;
                }
                if (fill == ResponseReader::Fill::Failed) {
                    complete = false;
                    break;
                }
            }
        }
        if (declined) {
            return std::unexpected("the transfer was stopped because it exceeded its limit");
        }
        if (!complete && success) {
            return std::unexpected("the connection ended or stalled before the answer was complete");
        }
        return response;
    }

    static bool readChunked(ResponseReader& reader, const std::function<bool(std::string_view)>& deliver) {
        while (true) {
            auto sizeLine = reader.line();
            if (!sizeLine) {
                return false;
            }
            std::size_t size = 0;
            const std::string_view digits = trim(std::string_view(*sizeLine).substr(0, sizeLine->find(';')));
            auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), size, 16);
            if (error != std::errc{}) {
                return false;
            }
            if (size == 0) {
                while (auto trailer = reader.line()) {
                    if (trailer->empty()) {
                        break;
                    }
                }
                return true;
            }
            if (!reader.take(size, deliver)) {
                return false;
            }
            auto after = reader.line();
            if (!after || !after->empty()) {
                return false;
            }
        }
    }

    std::mutex mutex_;
    TlsSettings tls_;
};

} // namespace

std::unique_ptr<HttpClient> makeNativeHttpClient(std::string caFile) {
    return std::make_unique<NativeHttpClient>(std::move(caFile));
}

std::unique_ptr<TcpConnection> connectTcp(const std::string& host, int port) {
    auto tcp = std::make_unique<Tcp>();
    if (!tcp->connect(host, port)) {
        return nullptr;
    }
    return tcp;
}

} // namespace basalt::hoist::net
