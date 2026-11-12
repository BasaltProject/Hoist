#include <basalt/hoist/net/Hash.hpp>

#include <array>
#include <fstream>
#include <mbedtls/sha256.h>

namespace basalt::hoist::net {
namespace {

std::string toHex(const unsigned char* bytes, std::size_t size) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += kDigits[bytes[i] >> 4];
        out += kDigits[bytes[i] & 15];
    }
    return out;
}

} // namespace

std::string sha256Hex(std::string_view data) {
    std::array<unsigned char, 32> digest{};
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data(), 0);
    return toHex(digest.data(), digest.size());
}

std::expected<std::string, std::string> sha256File(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::unexpected("cannot read " + file.string());
    }
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    mbedtls_sha256_starts(&context, 0);
    std::array<char, 65536> buffer;
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
        mbedtls_sha256_update(&context, reinterpret_cast<const unsigned char*>(buffer.data()),
                              static_cast<std::size_t>(in.gcount()));
    }
    std::array<unsigned char, 32> digest{};
    mbedtls_sha256_finish(&context, digest.data());
    mbedtls_sha256_free(&context);
    return toHex(digest.data(), digest.size());
}

} // namespace basalt::hoist::net
