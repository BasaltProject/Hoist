#include <basalt/hoist/expr/Expression.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace basalt::hoist::expr {

struct Expression::Node {
    enum class Kind { Literal, Placeholder, Not, Negate, Binary } kind = Kind::Literal;
    Value literal = 0.0;
    std::string name;
    std::string op;
    std::shared_ptr<const Node> left;
    std::shared_ptr<const Node> right;
};

namespace {

using Node = Expression::Node;
using NodePtr = std::shared_ptr<const Node>;

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

struct Token {
    enum class Kind { Number, String, Placeholder, Word, Op, End } kind = Kind::End;
    std::string text;
    double number = 0;
    std::size_t column = 0;
};

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    std::expected<std::vector<Token>, ParseError> run() {
        std::vector<Token> tokens;
        while (true) {
            while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) {
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                tokens.push_back({Token::Kind::End, {}, 0, pos_});
                return tokens;
            }
            auto token = next();
            if (!token) {
                return std::unexpected(token.error());
            }
            tokens.push_back(std::move(*token));
        }
    }

private:
    std::expected<Token, ParseError> next() {
        const std::size_t start = pos_;
        const char c = text_[pos_];
        if (c == '%') {
            std::size_t end = pos_ + 1;
            while (end < text_.size() && isNameChar(text_[end])) {
                ++end;
            }
            if (end == pos_ + 1 || end >= text_.size() || text_[end] != '%') {
                return fail("a placeholder must look like %name%", start);
            }
            Token token{Token::Kind::Placeholder, std::string(text_.substr(pos_ + 1, end - pos_ - 1)), 0, start};
            pos_ = end + 1;
            return token;
        }
        if (isDigit(c)) {
            return number(start);
        }
        if (c == '\'' || c == '"') {
            const auto close = text_.find(c, pos_ + 1);
            if (close == std::string_view::npos) {
                return fail("unterminated text", start);
            }
            Token token{Token::Kind::String, std::string(text_.substr(pos_ + 1, close - pos_ - 1)), 0, start};
            pos_ = close + 1;
            return token;
        }
        if (isNameChar(c)) {
            std::size_t end = pos_;
            while (end < text_.size() && isNameChar(text_[end])) {
                ++end;
            }
            Token token{Token::Kind::Word, std::string(text_.substr(pos_, end - pos_)), 0, start};
            pos_ = end;
            return token;
        }
        for (std::string_view op : {"||", "&&", "==", "!=", "<=", ">=", "<", ">", "!", "+", "-", "*", "/", "(", ")"}) {
            if (text_.substr(pos_).starts_with(op)) {
                pos_ += op.size();
                return Token{Token::Kind::Op, std::string(op), 0, start};
            }
        }
        return fail(std::string("unexpected '") + c + "'", start);
    }

    std::expected<Token, ParseError> number(std::size_t start) {
        std::size_t end = pos_;
        while (end < text_.size() && isDigit(text_[end])) {
            ++end;
        }
        if (end < text_.size() && text_[end] == ':') {
            std::size_t minuteEnd = end + 1;
            while (minuteEnd < text_.size() && isDigit(text_[minuteEnd])) {
                ++minuteEnd;
            }
            int hour = 0;
            int minute = 0;
            std::from_chars(text_.data() + pos_, text_.data() + end, hour);
            std::from_chars(text_.data() + end + 1, text_.data() + minuteEnd, minute);
            if (minuteEnd == end + 1 || hour > 23 || minute > 59) {
                return fail("a clock time must look like HH:MM", start);
            }
            pos_ = minuteEnd;
            return Token{Token::Kind::Number, {}, hour * 60.0 + minute, start};
        }
        if (end < text_.size() && text_[end] == '.') {
            ++end;
            while (end < text_.size() && isDigit(text_[end])) {
                ++end;
            }
        }
        double value = 0;
        std::from_chars(text_.data() + pos_, text_.data() + end, value);
        pos_ = end;
        return Token{Token::Kind::Number, {}, value, start};
    }

    static std::unexpected<ParseError> fail(std::string message, std::size_t column) {
        return std::unexpected(ParseError{std::move(message), column});
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

NodePtr binary(std::string op, NodePtr left, NodePtr right) {
    auto node = std::make_shared<Node>();
    node->kind = Node::Kind::Binary;
    node->op = std::move(op);
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::vector<std::string>& names) : tokens_(std::move(tokens)), names_(names) {}

    std::expected<NodePtr, ParseError> run() {
        auto root = orExpr();
        if (!root) {
            return root;
        }
        if (peek().kind != Token::Kind::End) {
            return fail("unexpected input after the expression");
        }
        return root;
    }

private:
    using Result = std::expected<NodePtr, ParseError>;

    const Token& peek() const { return tokens_[index_]; }

    bool isOp(std::string_view op) const { return peek().kind == Token::Kind::Op && peek().text == op; }

    std::unexpected<ParseError> fail(std::string message) const {
        return std::unexpected(ParseError{std::move(message), peek().column});
    }

    template <typename Next, std::size_t N>
    Result level(Next next, const std::string_view (&ops)[N]) {
        auto left = (this->*next)();
        if (!left) {
            return left;
        }
        while (true) {
            std::string matched;
            for (auto op : ops) {
                if (isOp(op)) {
                    matched = op;
                }
            }
            if (matched.empty()) {
                return left;
            }
            ++index_;
            auto right = (this->*next)();
            if (!right) {
                return right;
            }
            left = binary(matched, std::move(*left), std::move(*right));
        }
    }

    Result orExpr() {
        static constexpr std::string_view ops[] = {"||"};
        return level(&Parser::andExpr, ops);
    }

    Result andExpr() {
        static constexpr std::string_view ops[] = {"&&"};
        return level(&Parser::equality, ops);
    }

    Result equality() {
        static constexpr std::string_view ops[] = {"==", "!="};
        return level(&Parser::relational, ops);
    }

    Result relational() {
        static constexpr std::string_view ops[] = {"<=", ">=", "<", ">"};
        return level(&Parser::additive, ops);
    }

    Result additive() {
        static constexpr std::string_view ops[] = {"+", "-"};
        return level(&Parser::multiplicative, ops);
    }

    Result multiplicative() {
        static constexpr std::string_view ops[] = {"*", "/"};
        return level(&Parser::unary, ops);
    }

    Result unary() {
        if (isOp("!") || isOp("-")) {
            const bool negate = peek().text == "-";
            ++index_;
            auto operand = unary();
            if (!operand) {
                return operand;
            }
            auto node = std::make_shared<Node>();
            node->kind = negate ? Node::Kind::Negate : Node::Kind::Not;
            node->left = std::move(*operand);
            return NodePtr(node);
        }
        return primary();
    }

    Result primary() {
        const Token token = peek();
        auto node = std::make_shared<Node>();
        switch (token.kind) {
        case Token::Kind::Number:
            node->literal = token.number;
            break;
        case Token::Kind::String:
            node->literal = token.text;
            break;
        case Token::Kind::Placeholder:
            node->kind = Node::Kind::Placeholder;
            node->name = token.text;
            if (std::ranges::find(names_, token.text) == names_.end()) {
                names_.push_back(token.text);
            }
            break;
        case Token::Kind::Word:
            if (token.text != "true" && token.text != "false") {
                return fail("unknown word '" + token.text + "', text needs quotes");
            }
            node->literal = token.text == "true";
            break;
        case Token::Kind::Op:
            if (token.text == "(") {
                ++index_;
                auto inner = orExpr();
                if (!inner) {
                    return inner;
                }
                if (!isOp(")")) {
                    return fail("missing )");
                }
                ++index_;
                return inner;
            }
            return fail("unexpected '" + token.text + "'");
        case Token::Kind::End:
            return fail("the expression ends too soon");
        }
        ++index_;
        return NodePtr(node);
    }

    std::vector<Token> tokens_;
    std::vector<std::string>& names_;
    std::size_t index_ = 0;
};

using Evaluated = std::expected<Value, EvalError>;

std::unexpected<EvalError> mismatch(std::string detail) {
    return std::unexpected(EvalError{EvalError::Kind::TypeMismatch, std::move(detail)});
}

Evaluated evaluate(const Node& node, const Placeholders& placeholders);

Evaluated evaluateBinary(const Node& node, const Placeholders& placeholders) {
    auto left = evaluate(*node.left, placeholders);
    if (!left) {
        return left;
    }
    const std::string& op = node.op;
    if (op == "||" || op == "&&") {
        const auto* lhs = std::get_if<bool>(&*left);
        if (!lhs) {
            return mismatch("'" + op + "' needs true or false on both sides");
        }
        if ((op == "||") == *lhs) {
            return *lhs;
        }
        auto right = evaluate(*node.right, placeholders);
        if (!right) {
            return right;
        }
        if (!std::holds_alternative<bool>(*right)) {
            return mismatch("'" + op + "' needs true or false on both sides");
        }
        return right;
    }
    auto right = evaluate(*node.right, placeholders);
    if (!right) {
        return right;
    }
    if (left->index() != right->index()) {
        return mismatch("'" + op + "' compares values of different kinds");
    }
    if (const auto* a = std::get_if<double>(&*left)) {
        const double b = std::get<double>(*right);
        if (op == "+")
            return *a + b;
        if (op == "-")
            return *a - b;
        if (op == "*")
            return *a * b;
        if (op == "/") {
            if (b == 0) {
                return std::unexpected(EvalError{EvalError::Kind::DivisionByZero, "division by zero"});
            }
            return *a / b;
        }
        if (op == "==")
            return *a == b;
        if (op == "!=")
            return *a != b;
        if (op == "<")
            return *a < b;
        if (op == "<=")
            return *a <= b;
        if (op == ">")
            return *a > b;
        return *a >= b;
    }
    if (op == "==")
        return *left == *right;
    if (op == "!=")
        return *left != *right;
    if (const auto* a = std::get_if<std::string>(&*left)) {
        const auto& b = std::get<std::string>(*right);
        if (op == "<")
            return *a < b;
        if (op == "<=")
            return *a <= b;
        if (op == ">")
            return *a > b;
        if (op == ">=")
            return *a >= b;
    }
    return mismatch("'" + op + "' does not apply to these values");
}

Evaluated evaluate(const Node& node, const Placeholders& placeholders) {
    switch (node.kind) {
    case Node::Kind::Literal:
        return node.literal;
    case Node::Kind::Placeholder:
        if (auto value = placeholders.get(node.name)) {
            return *value;
        }
        return std::unexpected(EvalError{EvalError::Kind::MissingPlaceholder, node.name});
    case Node::Kind::Not: {
        auto value = evaluate(*node.left, placeholders);
        if (!value) {
            return value;
        }
        const auto* flag = std::get_if<bool>(&*value);
        return flag ? Evaluated(!*flag) : mismatch("'!' needs true or false");
    }
    case Node::Kind::Negate: {
        auto value = evaluate(*node.left, placeholders);
        if (!value) {
            return value;
        }
        const auto* number = std::get_if<double>(&*value);
        return number ? Evaluated(-*number) : mismatch("'-' needs a number");
    }
    case Node::Kind::Binary:
        return evaluateBinary(node, placeholders);
    }
    return mismatch("unknown expression");
}

} // namespace

std::expected<Expression, ParseError> Expression::parse(std::string_view text) {
    auto tokens = Lexer(text).run();
    if (!tokens) {
        return std::unexpected(tokens.error());
    }
    Expression expression;
    auto root = Parser(std::move(*tokens), expression.names_).run();
    if (!root) {
        return std::unexpected(root.error());
    }
    expression.root_ = std::move(*root);
    return expression;
}

std::expected<bool, EvalError> Expression::evaluate(const Placeholders& placeholders) const {
    for (const auto& name : names_) {
        if (!placeholders.get(name)) {
            return std::unexpected(EvalError{EvalError::Kind::MissingPlaceholder, name});
        }
    }
    auto value = basalt::hoist::expr::evaluate(*root_, placeholders);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (const auto* flag = std::get_if<bool>(&*value)) {
        return *flag;
    }
    return std::unexpected(EvalError{EvalError::Kind::TypeMismatch, "the condition must be true or false"});
}

} // namespace basalt::hoist::expr
