#include <basalt/hoist/core/Version.hpp>

#include <charconv>

namespace basalt::hoist::core {
namespace {

std::optional<int> parseInt(std::string_view text) {
    int value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<Version> parseVersion(std::string_view text) {
    if (text.starts_with('v')) {
        text.remove_prefix(1);
    }
    Version version;
    if (auto dash = text.find("-n"); dash != std::string_view::npos) {
        auto nightly = parseInt(text.substr(dash + 2));
        if (!nightly) {
            return std::nullopt;
        }
        version.nightly = nightly;
        text = text.substr(0, dash);
    }
    int fields[3];
    for (int i = 0; i < 3; ++i) {
        auto dot = text.find('.');
        if ((i < 2) != (dot != std::string_view::npos)) {
            return std::nullopt;
        }
        auto value = parseInt(text.substr(0, dot));
        if (!value) {
            return std::nullopt;
        }
        fields[i] = *value;
        if (dot != std::string_view::npos) {
            text.remove_prefix(dot + 1);
        }
    }
    version.major = fields[0];
    version.minor = fields[1];
    version.build = fields[2];
    return version;
}

std::string toString(const Version& version) {
    std::string text =
        std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.build);
    if (version.nightly) {
        text += "-n" + std::to_string(*version.nightly);
    }
    return text;
}

std::strong_ordering compare(const Version& a, const Version& b) {
    if (auto order = a.major <=> b.major; order != 0) {
        return order;
    }
    if (auto order = a.minor <=> b.minor; order != 0) {
        return order;
    }
    if (auto order = a.build <=> b.build; order != 0) {
        return order;
    }
    return a.nightly.value_or(-1) <=> b.nightly.value_or(-1);
}

Jump classify(const Version& from, const Version& to) {
    auto order = compare(to, from);
    if (order == 0) {
        return Jump::None;
    }
    if (order < 0) {
        return Jump::Downgrade;
    }
    if (to.major != from.major) {
        return Jump::Major;
    }
    if (to.minor != from.minor) {
        return Jump::Minor;
    }
    return Jump::Build;
}

bool allows(Scope scope, Jump jump) {
    switch (jump) {
    case Jump::None:
    case Jump::Downgrade:
        return false;
    case Jump::Build:
        return true;
    case Jump::Minor:
        return scope != Scope::Build;
    case Jump::Major:
        return scope == Scope::Major;
    }
    return false;
}

Verdict judge(Channel channel, Scope scope, const Installed& installed, const Candidate& candidate) {
    bool newer = compare(candidate.version, installed.version) > 0;
    if (channel == Channel::Nightly && installed.publishedAt > 0 && candidate.publishedAt > 0) {
        // The nightly tag moves, so the publication time says whether it is new.
        newer = candidate.publishedAt > installed.publishedAt && compare(candidate.version, installed.version) >= 0;
    }
    if (!newer) {
        return Verdict::UpToDate;
    }
    return allows(scope, classify(installed.version, candidate.version)) ? Verdict::Allowed : Verdict::OutOfScope;
}

std::optional<Channel> parseChannel(std::string_view text) {
    if (text == "release") {
        return Channel::Release;
    }
    if (text == "nightly") {
        return Channel::Nightly;
    }
    return std::nullopt;
}

std::optional<Scope> parseScope(std::string_view text) {
    if (text == "build") {
        return Scope::Build;
    }
    if (text == "minor") {
        return Scope::Minor;
    }
    if (text == "major") {
        return Scope::Major;
    }
    return std::nullopt;
}

std::string_view nameOf(Channel channel) { return channel == Channel::Release ? "release" : "nightly"; }

std::string_view nameOf(Scope scope) {
    switch (scope) {
    case Scope::Build:
        return "build";
    case Scope::Minor:
        return "minor";
    case Scope::Major:
        return "major";
    }
    return "build";
}

std::string_view nameOf(Jump jump) {
    switch (jump) {
    case Jump::None:
        return "none";
    case Jump::Build:
        return "build";
    case Jump::Minor:
        return "minor";
    case Jump::Major:
        return "major";
    case Jump::Downgrade:
        return "downgrade";
    }
    return "none";
}

} // namespace basalt::hoist::core
