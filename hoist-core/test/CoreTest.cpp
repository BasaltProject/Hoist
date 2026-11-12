#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/core/Time.hpp>
#include <basalt/hoist/core/Version.hpp>

#include <doctest/doctest.h>

using namespace basalt::hoist::core;

namespace {

Version v(std::string_view text) { return *parseVersion(text); }

} // namespace

TEST_CASE("versions parse, order and classify the way the releases are named") {
    CHECK(toString(v("v26.3.4")) == "26.3.4");
    CHECK(v("26.3.3-n3").nightly == 3);
    CHECK_FALSE(parseVersion("26.3"));
    CHECK_FALSE(parseVersion("26.3.0-dev"));
    CHECK((compare(v("26.3.3-n1"), v("26.3.3")) > 0));
    CHECK(classify(v("26.3.3"), v("26.3.4")) == Jump::Build);
    CHECK(classify(v("26.3.3"), v("26.4.0")) == Jump::Minor);
    CHECK(classify(v("26.3.3"), v("27.0.0")) == Jump::Major);
    CHECK(classify(v("26.3.3"), v("26.3.2")) == Jump::Downgrade);
}

TEST_CASE("the scope is a hard limit and a nightly is judged by its publication time") {
    const Installed installed{v("26.3.3"), 100};
    CHECK(judge(Channel::Release, Scope::Build, installed, {v("26.3.4"), 200}) == Verdict::Allowed);
    CHECK(judge(Channel::Release, Scope::Build, installed, {v("26.4.0"), 200}) == Verdict::OutOfScope);
    CHECK(judge(Channel::Release, Scope::Minor, installed, {v("26.4.0"), 200}) == Verdict::Allowed);
    CHECK(judge(Channel::Release, Scope::Minor, installed, {v("27.0.0"), 200}) == Verdict::OutOfScope);
    CHECK(judge(Channel::Release, Scope::Major, installed, {v("27.0.0"), 200}) == Verdict::Allowed);
    CHECK(judge(Channel::Release, Scope::Major, installed, {v("26.3.2"), 200}) == Verdict::UpToDate);
    const Installed nightly{v("26.3.3-n3"), 100};
    CHECK(judge(Channel::Nightly, Scope::Build, nightly, {v("26.3.3-n3"), 100}) == Verdict::UpToDate);
    CHECK(judge(Channel::Nightly, Scope::Build, nightly, {v("26.3.3-n4"), 300}) == Verdict::Allowed);
}
