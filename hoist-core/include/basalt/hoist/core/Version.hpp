#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace basalt::hoist::core {

struct Version {
    int major = 0;
    int minor = 0;
    int build = 0;
    std::optional<int> nightly;

    friend bool operator==(const Version&, const Version&) = default;
};

enum class Channel { Release, Nightly };
enum class Scope { Build, Minor, Major };
enum class Jump { None, Build, Minor, Major, Downgrade };
enum class Verdict { UpToDate, Allowed, OutOfScope };

struct Installed {
    Version version;
    std::int64_t publishedAt = 0;
};

struct Candidate {
    Version version;
    std::int64_t publishedAt = 0;
};

std::optional<Version> parseVersion(std::string_view text);
std::string toString(const Version& version);
std::strong_ordering compare(const Version& a, const Version& b);
Jump classify(const Version& from, const Version& to);
bool allows(Scope scope, Jump jump);
Verdict judge(Channel channel, Scope scope, const Installed& installed, const Candidate& candidate);

std::optional<Channel> parseChannel(std::string_view text);
std::optional<Scope> parseScope(std::string_view text);
std::string_view nameOf(Channel channel);
std::string_view nameOf(Scope scope);
std::string_view nameOf(Jump jump);

} // namespace basalt::hoist::core
