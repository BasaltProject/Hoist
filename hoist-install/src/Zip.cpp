#include "Zip.hpp"

#include <array>
#include <ctime>
#include <memory>
#include <zlib.h>

namespace basalt::hoist::install::zip {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kChunk = std::size_t{64} * 1024;
constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::uint32_t kEnd64Signature = 0x06064b50;
constexpr std::uint32_t kLocator64Signature = 0x07064b50;
constexpr std::uint32_t kDescriptorSignature = 0x08074b50;
constexpr std::uint16_t kFlags = 0x0808;
constexpr std::uint32_t kMax32 = 0xFFFFFFFF;

void le16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>(value >> 8));
}

void le32(std::string& out, std::uint32_t value) {
    le16(out, static_cast<std::uint16_t>(value & 0xFFFF));
    le16(out, static_cast<std::uint16_t>(value >> 16));
}

void le64(std::string& out, std::uint64_t value) {
    le32(out, static_cast<std::uint32_t>(value & kMax32));
    le32(out, static_cast<std::uint32_t>(value >> 32));
}

std::uint64_t read(const std::string& in, std::size_t at, int bytes) {
    std::uint64_t value = 0;
    for (int i = bytes - 1; i >= 0; --i) {
        value = (value << 8) | static_cast<unsigned char>(in[at + static_cast<std::size_t>(i)]);
    }
    return value;
}

struct DeflateGuard {
    z_stream stream{};

    ~DeflateGuard() { deflateEnd(&stream); }
};

struct InflateGuard {
    z_stream stream{};

    ~InflateGuard() { inflateEnd(&stream); }
};

bool safeName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        return false;
    }
    for (const auto& part : fs::path(name)) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

} // namespace

std::pair<std::uint16_t, std::uint16_t> dosStamp(std::int64_t epoch) {
    const auto raw = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif
    const int year = tm.tm_year + 1900 < 1980 ? 1980 : tm.tm_year + 1900;
    return {static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2)),
            static_cast<std::uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday)};
}

std::expected<Writer, std::string> Writer::create(const fs::path& file, std::uint16_t dosTime, std::uint16_t dosDate) {
    Writer writer;
    writer.out_.open(file, std::ios::binary | std::ios::trunc);
    if (!writer.out_) {
        return std::unexpected("cannot write " + file.string());
    }
    writer.time_ = dosTime;
    writer.date_ = dosDate;
    return writer;
}

void Writer::put(const std::string& bytes) {
    out_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    written_ += bytes.size();
}

std::expected<void, std::string> Writer::addFile(const std::string& name, const fs::path& source, std::uint32_t mode,
                                                 std::stop_token stop) {
    return add(name, &source, mode, std::move(stop));
}

std::expected<void, std::string> Writer::addDirectory(const std::string& name, std::uint32_t mode) {
    return add(name + "/", nullptr, mode, {});
}

std::expected<void, std::string> Writer::add(const std::string& name, const fs::path* source, std::uint32_t mode,
                                             std::stop_token stop) {
    std::ifstream in;
    if (source) {
        in.open(*source, std::ios::binary);
        if (!in) {
            return std::unexpected(Skipped::kMessage);
        }
    }
    Entry entry;
    entry.name = name;
    entry.mode = mode;
    entry.offset = written_;
    std::string header;
    le32(header, kLocalSignature);
    le16(header, 45);
    le16(header, kFlags);
    le16(header, 8);
    le16(header, time_);
    le16(header, date_);
    le32(header, 0);
    le32(header, kMax32);
    le32(header, kMax32);
    le16(header, static_cast<std::uint16_t>(name.size()));
    le16(header, 20);
    header += name;
    le16(header, 1);
    le16(header, 16);
    le64(header, 0);
    le64(header, 0);
    put(header);

    DeflateGuard deflater;
    if (deflateInit2(&deflater.stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return std::unexpected("cannot start compressing");
    }
    std::array<char, kChunk> input;
    std::array<char, kChunk> output;
    std::uint32_t crc = static_cast<std::uint32_t>(crc32(0, nullptr, 0));
    bool last = false;
    while (!last) {
        if (stop.stop_requested()) {
            return std::unexpected("the backup was cancelled");
        }
        std::size_t got = 0;
        if (source) {
            in.read(input.data(), static_cast<std::streamsize>(input.size()));
            got = static_cast<std::size_t>(in.gcount());
            if (in.bad()) {
                return std::unexpected("cannot read " + source->string());
            }
            last = in.eof();
        } else {
            last = true;
        }
        entry.size += got;
        crc = static_cast<std::uint32_t>(
            crc32(crc, reinterpret_cast<const Bytef*>(input.data()), static_cast<uInt>(got)));
        deflater.stream.next_in = reinterpret_cast<Bytef*>(input.data());
        deflater.stream.avail_in = static_cast<uInt>(got);
        do {
            deflater.stream.next_out = reinterpret_cast<Bytef*>(output.data());
            deflater.stream.avail_out = static_cast<uInt>(output.size());
            deflate(&deflater.stream, last ? Z_FINISH : Z_NO_FLUSH);
            const std::size_t produced = output.size() - deflater.stream.avail_out;
            out_.write(output.data(), static_cast<std::streamsize>(produced));
            written_ += produced;
            entry.compressed += produced;
        } while (deflater.stream.avail_out == 0);
    }
    entry.crc = crc;
    std::string descriptor;
    le32(descriptor, kDescriptorSignature);
    le32(descriptor, crc);
    le64(descriptor, entry.compressed);
    le64(descriptor, entry.size);
    put(descriptor);
    if (!out_) {
        return std::unexpected("cannot write the backup file");
    }
    entries_.push_back(std::move(entry));
    return {};
}

std::expected<void, std::string> Writer::finish() {
    const std::uint64_t directoryOffset = written_;
    for (const auto& entry : entries_) {
        std::string record;
        le32(record, kCentralSignature);
        le16(record, (3 << 8) | 45);
        le16(record, 45);
        le16(record, kFlags);
        le16(record, 8);
        le16(record, time_);
        le16(record, date_);
        le32(record, entry.crc);
        le32(record, kMax32);
        le32(record, kMax32);
        le16(record, static_cast<std::uint16_t>(entry.name.size()));
        le16(record, 28);
        le16(record, 0);
        le16(record, 0);
        le16(record, 0);
        le32(record, ((entry.name.back() == '/' ? 0x4000u : 0x8000u) | entry.mode) << 16);
        le32(record, kMax32);
        record += entry.name;
        le16(record, 1);
        le16(record, 24);
        le64(record, entry.size);
        le64(record, entry.compressed);
        le64(record, entry.offset);
        put(record);
    }
    const std::uint64_t directorySize = written_ - directoryOffset;
    const std::uint64_t end64Offset = written_;
    std::string tail;
    le32(tail, kEnd64Signature);
    le64(tail, 44);
    le16(tail, 45);
    le16(tail, 45);
    le32(tail, 0);
    le32(tail, 0);
    le64(tail, entries_.size());
    le64(tail, entries_.size());
    le64(tail, directorySize);
    le64(tail, directoryOffset);
    le32(tail, kLocator64Signature);
    le32(tail, 0);
    le64(tail, end64Offset);
    le32(tail, 1);
    le32(tail, kEndSignature);
    le16(tail, 0);
    le16(tail, 0);
    le16(tail, 0xFFFF);
    le16(tail, 0xFFFF);
    le32(tail, kMax32);
    le32(tail, kMax32);
    le16(tail, 0);
    put(tail);
    out_.flush();
    if (!out_) {
        return std::unexpected("cannot write the backup file");
    }
    out_.close();
    return {};
}

std::expected<void, std::string> extract(const fs::path& file, const fs::path& destination) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::unexpected("cannot read " + file.string());
    }
    in.seekg(0, std::ios::end);
    const auto total = static_cast<std::uint64_t>(in.tellg());
    const std::uint64_t window = std::min<std::uint64_t>(total, 22 + 20 + 65535);
    std::string tail(static_cast<std::size_t>(window), '\0');
    in.seekg(static_cast<std::streamoff>(total - window));
    in.read(tail.data(), static_cast<std::streamsize>(window));
    std::size_t end = std::string::npos;
    for (std::size_t i = tail.size() >= 22 ? tail.size() - 22 + 1 : 0; i-- > 0;) {
        if (read(tail, i, 4) == kEndSignature) {
            end = i;
            break;
        }
    }
    if (end == std::string::npos) {
        return std::unexpected(file.string() + " is not a readable zip file");
    }
    std::uint64_t count = read(tail, end + 10, 2);
    std::uint64_t directoryOffset = read(tail, end + 16, 4);
    std::uint64_t directorySize = read(tail, end + 12, 4);
    if ((count == 0xFFFF || directoryOffset == kMax32) && end >= 20 && read(tail, end - 20, 4) == kLocator64Signature) {
        const std::uint64_t end64 = read(tail, end - 20 + 8, 8);
        std::string record(56, '\0');
        in.seekg(static_cast<std::streamoff>(end64));
        in.read(record.data(), 56);
        if (!in || read(record, 0, 4) != kEnd64Signature) {
            return std::unexpected(file.string() + " has a damaged zip directory");
        }
        count = read(record, 32, 8);
        directorySize = read(record, 40, 8);
        directoryOffset = read(record, 48, 8);
    }
    if (directoryOffset + directorySize > total) {
        return std::unexpected(file.string() + " has a damaged zip directory");
    }
    std::string directory(static_cast<std::size_t>(directorySize), '\0');
    in.clear();
    in.seekg(static_cast<std::streamoff>(directoryOffset));
    in.read(directory.data(), static_cast<std::streamsize>(directorySize));
    if (!in) {
        return std::unexpected(file.string() + " has a damaged zip directory");
    }
    std::error_code ec;
    fs::create_directories(destination, ec);
    std::size_t at = 0;
    std::array<char, kChunk> input;
    std::array<char, kChunk> output;
    for (std::uint64_t index = 0; index < count; ++index) {
        if (at + 46 > directory.size() || read(directory, at, 4) != kCentralSignature) {
            return std::unexpected(file.string() + " has a damaged zip directory");
        }
        const auto method = static_cast<std::uint16_t>(read(directory, at + 10, 2));
        const auto expectedCrc = static_cast<std::uint32_t>(read(directory, at + 16, 4));
        const auto madeBy = static_cast<std::uint16_t>(read(directory, at + 4, 2));
        const auto attributes = static_cast<std::uint32_t>(read(directory, at + 38, 4));
        std::uint64_t compressed = read(directory, at + 20, 4);
        std::uint64_t size = read(directory, at + 24, 4);
        const std::size_t nameLength = read(directory, at + 28, 2);
        const std::size_t extraLength = read(directory, at + 30, 2);
        const std::size_t commentLength = read(directory, at + 32, 2);
        std::uint64_t offset = read(directory, at + 42, 4);
        if (at + 46 + nameLength + extraLength + commentLength > directory.size()) {
            return std::unexpected(file.string() + " has a damaged zip directory");
        }
        const std::string name = directory.substr(at + 46, nameLength);
        std::size_t extra = at + 46 + nameLength;
        const std::size_t extraEnd = extra + extraLength;
        while (extra + 4 <= extraEnd) {
            const auto id = read(directory, extra, 2);
            const std::size_t length = read(directory, extra + 2, 2);
            if (id == 1) {
                std::size_t field = extra + 4;
                if (size == kMax32 && field + 8 <= extraEnd) {
                    size = read(directory, field, 8);
                    field += 8;
                }
                if (compressed == kMax32 && field + 8 <= extraEnd) {
                    compressed = read(directory, field, 8);
                    field += 8;
                }
                if (offset == kMax32 && field + 8 <= extraEnd) {
                    offset = read(directory, field, 8);
                }
            }
            extra += 4 + length;
        }
        at = extraEnd + commentLength;
        if (!safeName(name)) {
            return std::unexpected(file.string() + " holds an unsafe path: " + name);
        }
        const fs::path target = destination / fs::path(name);
        const auto applyMode = [&] {
            const auto mode = static_cast<fs::perms>((attributes >> 16) & 0x1FFu);
            if ((madeBy >> 8) == 3 && mode != fs::perms::none) {
                fs::permissions(target, mode, ec);
            }
        };
        if (name.back() == '/') {
            fs::create_directories(target, ec);
            applyMode();
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::string local(30, '\0');
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(local.data(), 30);
        if (!in || read(local, 0, 4) != kLocalSignature) {
            return std::unexpected(file.string() + " has a damaged entry: " + name);
        }
        in.seekg(static_cast<std::streamoff>(offset + 30 + read(local, 26, 2) + read(local, 28, 2)));
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected("cannot write " + target.string());
        }
        std::uint32_t crc = static_cast<std::uint32_t>(crc32(0, nullptr, 0));
        std::uint64_t left = compressed;
        InflateGuard inflater;
        if (method == 8 && inflateInit2(&inflater.stream, -15) != Z_OK) {
            return std::unexpected("cannot start decompressing");
        }
        if (method != 8 && method != 0) {
            return std::unexpected(file.string() + " uses a compression this Hoist cannot read");
        }
        bool finished = method == 0;
        while (left > 0 || !finished) {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(left, input.size()));
            in.read(input.data(), static_cast<std::streamsize>(want));
            if (static_cast<std::size_t>(in.gcount()) != want) {
                return std::unexpected(file.string() + " is cut short at " + name);
            }
            left -= want;
            if (method == 0) {
                crc = static_cast<std::uint32_t>(
                    crc32(crc, reinterpret_cast<const Bytef*>(input.data()), static_cast<uInt>(want)));
                out.write(input.data(), static_cast<std::streamsize>(want));
                continue;
            }
            inflater.stream.next_in = reinterpret_cast<Bytef*>(input.data());
            inflater.stream.avail_in = static_cast<uInt>(want);
            do {
                inflater.stream.next_out = reinterpret_cast<Bytef*>(output.data());
                inflater.stream.avail_out = static_cast<uInt>(output.size());
                const int status = inflate(&inflater.stream, Z_NO_FLUSH);
                if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
                    return std::unexpected(file.string() + " is damaged at " + name);
                }
                const std::size_t produced = output.size() - inflater.stream.avail_out;
                crc = static_cast<std::uint32_t>(
                    crc32(crc, reinterpret_cast<const Bytef*>(output.data()), static_cast<uInt>(produced)));
                out.write(output.data(), static_cast<std::streamsize>(produced));
                if (status == Z_STREAM_END) {
                    finished = true;
                    break;
                }
                if (status == Z_BUF_ERROR && inflater.stream.avail_in == 0) {
                    break;
                }
            } while (inflater.stream.avail_out == 0 || inflater.stream.avail_in > 0);
            if (left == 0 && !finished) {
                return std::unexpected(file.string() + " is cut short at " + name);
            }
        }
        out.close();
        if (!out || crc != expectedCrc) {
            return std::unexpected(file.string() + " failed its checksum at " + name);
        }
        applyMode();
    }
    return {};
}

} // namespace basalt::hoist::install::zip
