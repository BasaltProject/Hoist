#include <basalt/hoist/instance/Observer.hpp>
#include <basalt/hoist/net/Json.hpp>

namespace basalt::hoist::instance {
namespace {

void putVarInt(std::string& out, std::uint32_t value) {
    while (value >= 0x80) {
        out += static_cast<char>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    out += static_cast<char>(value);
}

std::size_t getVarInt(std::string_view data, std::uint32_t& value) {
    value = 0;
    for (std::size_t i = 0; i < data.size() && i < 5; ++i) {
        value |= static_cast<std::uint32_t>(data[i] & 0x7F) << (7 * i);
        if ((data[i] & 0x80) == 0) {
            return i + 1;
        }
    }
    return 0;
}

std::string packet(const std::string& body) {
    std::string out;
    putVarInt(out, static_cast<std::uint32_t>(body.size()));
    return out + body;
}

std::optional<int> wholeNumber(const net::Json* value) {
    if (value && value->kind == net::Json::Kind::Number && value->number >= 0 && value->number < 1e9) {
        return static_cast<int>(value->number);
    }
    return std::nullopt;
}

} // namespace

std::string buildPingRequest(std::string_view host, int port) {
    std::string handshake;
    putVarInt(handshake, 0);
    putVarInt(handshake, 0xFFFFFFFFu);
    putVarInt(handshake, static_cast<std::uint32_t>(host.size()));
    handshake += host;
    handshake += static_cast<char>((port >> 8) & 0xFF);
    handshake += static_cast<char>(port & 0xFF);
    putVarInt(handshake, 1);
    return packet(handshake) + packet(std::string(1, '\0'));
}

std::optional<std::string> extractPingJson(std::string_view bytes) {
    std::uint32_t length = 0;
    const std::size_t lengthBytes = getVarInt(bytes, length);
    if (lengthBytes == 0 || length > (1u << 20) || bytes.size() < lengthBytes + length) {
        return std::nullopt;
    }
    std::string_view body = bytes.substr(lengthBytes, length);
    std::uint32_t id = 0;
    const std::size_t idBytes = getVarInt(body, id);
    if (idBytes == 0 || id != 0) {
        return std::nullopt;
    }
    body.remove_prefix(idBytes);
    std::uint32_t textLength = 0;
    const std::size_t textBytes = getVarInt(body, textLength);
    if (textBytes == 0 || body.size() < textBytes + textLength) {
        return std::nullopt;
    }
    return std::string(body.substr(textBytes, textLength));
}

std::optional<Status> parsePingResponse(std::string_view json) {
    auto document = net::parseJson(json);
    if (!document || document->kind != net::Json::Kind::Object) {
        return std::nullopt;
    }
    const auto* players = document->find("players");
    if (!players || players->kind != net::Json::Kind::Object) {
        return std::nullopt;
    }
    Status status;
    status.source = Status::Source::Ping;
    status.playersOnline = wholeNumber(players->find("online"));
    status.playersMax = wholeNumber(players->find("max"));
    if (!status.playersOnline) {
        return std::nullopt;
    }
    return status;
}

std::optional<Status> parseApiStatus(std::string_view json) {
    auto document = net::parseJson(json);
    if (!document || document->kind != net::Json::Kind::Object) {
        return std::nullopt;
    }
    Status status;
    status.source = Status::Source::Api;
    if (const auto* players = document->find("players"); players && players->kind == net::Json::Kind::Object) {
        status.playersOnline = wholeNumber(players->find("online"));
        status.playersMax = wholeNumber(players->find("max"));
    } else {
        status.playersOnline = wholeNumber(document->find("online"));
        status.playersMax = wholeNumber(document->find("max_players"));
    }
    if (const auto* tps = document->find("tps"); tps && tps->kind == net::Json::Kind::Number) {
        status.tps = tps->number;
    }
    if (const auto* uptime = document->find("uptime_seconds"); uptime && uptime->kind == net::Json::Kind::Number) {
        status.uptimeSeconds = static_cast<std::int64_t>(uptime->number);
    }
    if (!status.playersOnline) {
        return std::nullopt;
    }
    return status;
}

} // namespace basalt::hoist::instance
