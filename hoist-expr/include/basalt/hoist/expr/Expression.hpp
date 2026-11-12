#pragma once

#include <basalt/hoist/expr/Placeholders.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::hoist::expr {

struct ParseError {
    std::string message;
    std::size_t column = 0;
};

struct EvalError {
    enum class Kind { MissingPlaceholder, TypeMismatch, DivisionByZero } kind = Kind::TypeMismatch;
    std::string detail;
};

// Syntax is documented in the README: placeholders, numbers, 'text', HH:MM, && || ! and comparisons.
class Expression {
public:
    struct Node;

    static std::expected<Expression, ParseError> parse(std::string_view text);

    // A placeholder with no value makes the whole expression an error, never true.
    std::expected<bool, EvalError> evaluate(const Placeholders& placeholders) const;

    const std::vector<std::string>& placeholders() const { return names_; }

private:
    std::shared_ptr<const Node> root_;
    std::vector<std::string> names_;
};

} // namespace basalt::hoist::expr
