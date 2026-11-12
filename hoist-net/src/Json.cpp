#include <basalt/hoist/net/Json.hpp>

#include <charconv>

namespace basalt::hoist::net {
namespace {

constexpr int kMaxDepth = 64;

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    std::expected<Json, std::string> run() {
        auto value = parseValue(0);
        if (!value) {
            return value;
        }
        skip();
        if (pos_ != text_.size()) {
            return fail("unexpected input after the document");
        }
        return value;
    }

private:
    using Result = std::expected<Json, std::string>;

    std::unexpected<std::string> fail(std::string message) const {
        return std::unexpected(message + " at byte " + std::to_string(pos_));
    }

    void skip() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    Result parseValue(int depth) {
        if (depth > kMaxDepth) {
            return fail("document nests too deeply");
        }
        skip();
        if (pos_ >= text_.size()) {
            return fail("the document ends too soon");
        }
        const char c = text_[pos_];
        if (c == '{') {
            return parseObject(depth);
        }
        if (c == '[') {
            return parseArray(depth);
        }
        if (c == '"') {
            auto text = parseString();
            if (!text) {
                return std::unexpected(text.error());
            }
            Json json;
            json.kind = Json::Kind::Text;
            json.text = std::move(*text);
            return json;
        }
        for (auto [word, kind, flag] : {std::tuple{"true", Json::Kind::Bool, true},
                                        {"false", Json::Kind::Bool, false},
                                        {"null", Json::Kind::Null, false}}) {
            if (text_.substr(pos_).starts_with(word)) {
                pos_ += std::string_view(word).size();
                Json json;
                json.kind = kind;
                json.boolean = flag;
                return json;
            }
        }
        return parseNumber();
    }

    Result parseNumber() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && std::string_view("+-.eE0123456789").find(text_[pos_]) != std::string_view::npos) {
            ++pos_;
        }
        Json json;
        json.kind = Json::Kind::Number;
        auto [end, error] = std::from_chars(text_.data() + start, text_.data() + pos_, json.number);
        if (pos_ == start || error != std::errc{} || end != text_.data() + pos_) {
            return fail("not a JSON value");
        }
        return json;
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::expected<unsigned, std::string> hex4() {
        if (pos_ + 4 > text_.size()) {
            return fail("a \\u escape ends too soon");
        }
        unsigned value = 0;
        auto [end, error] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, value, 16);
        if (error != std::errc{} || end != text_.data() + pos_ + 4) {
            return fail("a \\u escape is not hexadecimal");
        }
        pos_ += 4;
        return value;
    }

    std::expected<std::string, std::string> parseString() {
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                break;
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            case 'r':
                out += '\r';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'u': {
                auto code = hex4();
                if (!code) {
                    return std::unexpected(code.error());
                }
                if (*code >= 0xD800 && *code < 0xDC00 && text_.substr(pos_).starts_with("\\u")) {
                    pos_ += 2;
                    auto low = hex4();
                    if (!low) {
                        return std::unexpected(low.error());
                    }
                    code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
                }
                appendUtf8(out, *code);
                break;
            }
            default:
                return fail("unknown escape");
            }
        }
        return fail("unterminated text");
    }

    Result parseArray(int depth) {
        ++pos_;
        Json json;
        json.kind = Json::Kind::Array;
        skip();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return json;
        }
        while (true) {
            auto item = parseValue(depth + 1);
            if (!item) {
                return item;
            }
            json.values.push_back(std::move(*item));
            skip();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return json;
            }
            return fail("expected , or ]");
        }
    }

    Result parseObject(int depth) {
        ++pos_;
        Json json;
        json.kind = Json::Kind::Object;
        skip();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return json;
        }
        while (true) {
            skip();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected a key");
            }
            auto key = parseString();
            if (!key) {
                return std::unexpected(key.error());
            }
            skip();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return fail("expected :");
            }
            ++pos_;
            auto value = parseValue(depth + 1);
            if (!value) {
                return value;
            }
            json.keys.push_back(std::move(*key));
            json.values.push_back(std::move(*value));
            skip();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return json;
            }
            return fail("expected , or }");
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

const Json* Json::find(std::string_view key) const {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) {
            return &values[i];
        }
    }
    return nullptr;
}

std::string Json::stringOr(std::string_view key, std::string fallback) const {
    const auto* value = find(key);
    return value && value->kind == Kind::Text ? value->text : fallback;
}

bool Json::boolOr(std::string_view key, bool fallback) const {
    const auto* value = find(key);
    return value && value->kind == Kind::Bool ? value->boolean : fallback;
}

double Json::numberOr(std::string_view key, double fallback) const {
    const auto* value = find(key);
    return value && value->kind == Kind::Number ? value->number : fallback;
}

std::expected<Json, std::string> parseJson(std::string_view text) { return JsonParser(text).run(); }

} // namespace basalt::hoist::net
