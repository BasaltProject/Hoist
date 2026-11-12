#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/expr/Expression.hpp>

#include <doctest/doctest.h>

using namespace basalt::hoist::expr;

namespace {

std::expected<bool, EvalError> run(std::string_view text, const Placeholders& values) {
    auto expression = Expression::parse(text);
    REQUIRE(expression.has_value());
    return expression->evaluate(values);
}

} // namespace

TEST_CASE("a condition mixes a window that crosses midnight, a player count and text") {
    Placeholders values;
    values.set("time", 30.0);
    values.set("players_online", 2.0);
    values.set("channel", std::string("release"));
    CHECK(run("(%time% >= 23:00 || %time% < 06:00) && %players_online% < 5", values).value());
    CHECK_FALSE(run("%time% >= 03:00 && %players_online% < 5", values).value());
    CHECK(run("%channel% == 'release' && !(%players_online% >= 5)", values).value());
    CHECK(run("%players_online% * 2 + 1 == 5", values).value());
}

TEST_CASE("a placeholder without a value is an error and never a true condition") {
    Placeholders values;
    values.set("hour", 4.0);
    auto result = run("!(%players_online% > 5) || %hour% == 4", values);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == EvalError::Kind::MissingPlaceholder);
    CHECK(result.error().detail == "players_online");
    CHECK_FALSE(Expression::parse("%hour% >=").has_value());
    CHECK(values.format("up at %hour%, %nope%, 100%%") == "up at 4, %nope%, 100%");
}
