#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

enum class TokenType {
  kUnknown,
  kLeftParen,
  kRightParen,
  kInt,
  kFloat,
  kString,
  kUnclosedString,
  kDot,
  kNil,
  kT,
  kQuote,
  kSymbol,
  kErrorObject,  // Internal-only token used by eval.
};

struct Token {
  std::string value;
  TokenType type{};
  int starting_column{};
  int ending_column{};
  int starting_row{};
};

using TokensVector = std::vector<Token>;

// ---------------- Scanner ----------------

class Scanner {
 public:
  Scanner(std::string_view input, int logical_row)
      : input_(input), logical_row_(logical_row) {}

  std::optional<Token> GetToken() {
    SkipWhitespace();
    if (IsAtEnd()) {
      return std::nullopt;
    }

    const std::size_t starting_index = current_index_;
    const char first_character = Advance();

    switch (first_character) {
      case '(':
        return MakeToken("(", TokenType::kLeftParen, starting_index);
      case ')':
        return MakeToken(")", TokenType::kRightParen, starting_index);
      case '\'':
        return MakeToken("'", TokenType::kQuote, starting_index);
      case '"':
        return ScanString(starting_index);
      default:
        break;
    }

    const auto scan_unquoted_token = [this](char initial_character) {
      std::string value(1, initial_character);
      while (!IsAtEnd() && !IsSeparator(Peek())) {
        value.push_back(Advance());
      }
      return value;
    };

    const std::string value = scan_unquoted_token(first_character);
    return MakeToken(value, ClassifyToken(value), starting_index);
  }

 private:
  static bool IsSeparator(char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0 ||
           character == '(' || character == ')' || character == '\'' ||
           character == '"' || character == ';';
  }

  static TokenType ClassifyToken(std::string_view value) {
    if (value.empty()) {
      return TokenType::kUnknown;
    }
    if (value == ".") {
      return TokenType::kDot;
    }
    if (value == "nil" || value == "#f") {
      return TokenType::kNil;
    }
    if (value == "t" || value == "#t") {
      return TokenType::kT;
    }

    const auto is_digit = [](char character) {
      return std::isdigit(static_cast<unsigned char>(character)) != 0;
    };
    const auto consists_of_digits = [&is_digit](std::string_view text) {
      return std::ranges::all_of(text, is_digit);
    };
    const auto remove_optional_sign = [](std::string_view& text) {
      if (text.size() >= 2 && (text.front() == '+' || text.front() == '-')) {
        text.remove_prefix(1);
      }
    };
    const auto is_float = [&consists_of_digits](std::string_view number) {
      const std::size_t dot_position = number.find('.');
      if (dot_position == std::string_view::npos ||
          number.find('.', dot_position + 1) != std::string_view::npos) {
        return false;
      }

      const std::string_view before_dot = number.substr(0, dot_position);
      const std::string_view after_dot = number.substr(dot_position + 1);
      const bool has_digit = !before_dot.empty() || !after_dot.empty();
      return has_digit &&
             (before_dot.empty() || consists_of_digits(before_dot)) &&
             (after_dot.empty() || consists_of_digits(after_dot));
    };

    remove_optional_sign(value);
    if (is_float(value)) {
      return TokenType::kFloat;
    }
    if (!value.empty() && consists_of_digits(value)) {
      return TokenType::kInt;
    }

    return TokenType::kSymbol;
  }

  Token ScanString(std::size_t starting_index) {
    std::string value(1, '"');

    const auto append_escape_sequence = [this, &value]() {
      switch (Peek()) {
        case 'n':
          Advance();
          value.push_back('\n');
          break;
        case 't':
          Advance();
          value.push_back('\t');
          break;
        case '"':
          Advance();
          value.push_back('"');
          break;
        case '\\':
          Advance();
          value.push_back('\\');
          break;
        default:
          value.push_back('\\');
          break;
      }
    };

    while (!IsAtEnd()) {
      const char character = Advance();
      if (character == '"') {
        value.push_back('"');
        return MakeToken(value, TokenType::kString, starting_index);
      }

      if (character == '\\' && !IsAtEnd()) {
        append_escape_sequence();
      } else {
        value.push_back(character);
      }
    }

    return MakeToken(value, TokenType::kUnclosedString, starting_index);
  }

  Token MakeToken(std::string value, TokenType type,
                  std::size_t starting_index) const {
    return Token{
        .value = std::move(value),
        .type = type,
        .starting_column = static_cast<int>(starting_index) + 1,
        .ending_column = static_cast<int>(current_index_),
        .starting_row = logical_row_,
    };
  }

  void SkipWhitespace() {
    while (!IsAtEnd()) {
      if (std::isspace(static_cast<unsigned char>(Peek())) != 0) {
        Advance();
        continue;
      }
      if (Peek() == ';') {
        current_index_ = input_.size();
      }
      break;
    }
  }

  bool IsAtEnd() const { return current_index_ >= input_.size(); }
  char Peek() const { return IsAtEnd() ? '\0' : input_[current_index_]; }
  char Advance() { return input_[current_index_++]; }

  std::string_view input_;
  std::size_t current_index_ = 0;
  int logical_row_ = 1;
};

// ---------------- Syntax tree ----------------

struct AstNode;
using NodePtr = std::unique_ptr<AstNode>;

struct AtomNode {
  explicit AtomNode(Token token) : token_(std::move(token)) {}

  const Token& GetToken() const { return token_; }

 private:
  Token token_;
};

struct ConsNode {
  ConsNode(NodePtr car, NodePtr cdr)
      : car_(std::move(car)), cdr_(std::move(cdr)) {}

  const AstNode* Car() const { return car_.get(); }
  const AstNode* Cdr() const { return cdr_.get(); }

 private:
  NodePtr car_;
  NodePtr cdr_;
};

struct AstNode {
  using Data = std::variant<AtomNode, ConsNode>;

  explicit AstNode(AtomNode atom) : data(std::move(atom)) {}
  explicit AstNode(ConsNode cons) : data(std::move(cons)) {}

  Data data;
};

const AtomNode* GetAtom(const AstNode* node) {
  return node == nullptr ? nullptr : std::get_if<AtomNode>(&node->data);
}

const ConsNode* GetCons(const AstNode* node) {
  return node == nullptr ? nullptr : std::get_if<ConsNode>(&node->data);
}

NodePtr MakeAtom(Token token) {
  return std::make_unique<AstNode>(AtomNode(std::move(token)));
}

NodePtr MakeCons(NodePtr car, NodePtr cdr) {
  return std::make_unique<AstNode>(ConsNode(std::move(car), std::move(cdr)));
}

NodePtr MakeNilSyntax() {
  return MakeAtom(Token{.value = "nil", .type = TokenType::kNil});
}

bool IsNilSyntax(const AstNode* node) {
  const auto* atom = GetAtom(node);
  return atom != nullptr && atom->GetToken().type == TokenType::kNil;
}

// ---------------- Parser errors ----------------

enum class ParseErrorType {
  kAtomOrLeftParenExpected,
  kRightParenExpected,
  kNoClosingQuote,
};

class ParseError final : public std::runtime_error {
 public:
  ParseError(ParseErrorType type, Token token)
      : std::runtime_error("OurScheme parse error"),
        type_(type),
        token_(std::move(token)) {}

  ParseErrorType GetType() const { return type_; }
  const Token& GetToken() const { return token_; }

 private:
  ParseErrorType type_;
  Token token_;
};

class IncompleteInputError final : public std::runtime_error {
 public:
  IncompleteInputError() : std::runtime_error("Incomplete S-expression") {}
};

std::string FormatParseError(const ParseError& error) {
  const Token& token = error.GetToken();

  switch (error.GetType()) {
    case ParseErrorType::kAtomOrLeftParenExpected:
      return std::format(
          "ERROR (unexpected token) : atom or '(' expected when token at "
          "Line {} Column {} is >>{}<<",
          token.starting_row, token.starting_column, token.value);
    case ParseErrorType::kRightParenExpected:
      return std::format(
          "ERROR (unexpected token) : ')' expected when token at "
          "Line {} Column {} is >>{}<<",
          token.starting_row, token.starting_column, token.value);
    case ParseErrorType::kNoClosingQuote:
      return std::format(
          "ERROR (no closing quote) : END-OF-LINE encountered at "
          "Line {} Column {}",
          token.starting_row, token.ending_column + 1);
  }

  return "ERROR (unknown parse error)";
}

void PrintParseError(const ParseError& error) {
  std::println("{}", FormatParseError(error));
}

// ---------------- Parser ----------------

class Parser {
 public:
  explicit Parser(const TokensVector& tokens) : tokens_(tokens) {}

  NodePtr ParseOneExpression() {
    if (IsAtEnd()) {
      throw IncompleteInputError();
    }
    return ParseExpression();
  }

  std::size_t ConsumedTokenCount() const { return current_index_; }

 private:
  NodePtr ParseExpression() {
    if (IsAtEnd()) {
      throw IncompleteInputError();
    }

    switch (const Token token = Consume(); token.type) {
      case TokenType::kLeftParen:
        return ParseList();
      case TokenType::kQuote:
        return ParseQuote();
      case TokenType::kRightParen:
      case TokenType::kDot:
      case TokenType::kUnknown:
        throw ParseError(ParseErrorType::kAtomOrLeftParenExpected, token);
      case TokenType::kUnclosedString:
        throw ParseError(ParseErrorType::kNoClosingQuote, token);
      default:
        return MakeAtom(token);
    }
  }

  NodePtr ParseQuote() {
    NodePtr quoted_expression = ParseExpression();
    NodePtr quote_symbol =
        MakeAtom(Token{.value = "quote", .type = TokenType::kSymbol});
    return MakeCons(std::move(quote_symbol),
                    MakeCons(std::move(quoted_expression), MakeNilSyntax()));
  }

  NodePtr ParseList() {
    std::vector<NodePtr> elements;
    NodePtr tail = MakeNilSyntax();

    const auto prepend_elements = [&elements](NodePtr list_tail) {
      for (auto& element : std::ranges::reverse_view(elements)) {
        list_tail = MakeCons(std::move(element), std::move(list_tail));
      }
      return list_tail;
    };

    while (true) {
      if (IsAtEnd()) {
        throw IncompleteInputError();
      }
      if (Match(TokenType::kRightParen)) {
        Consume();
        return prepend_elements(std::move(tail));
      }
      if (!Match(TokenType::kDot)) {
        elements.push_back(ParseExpression());
        continue;
      }

      if (elements.empty()) {
        throw ParseError(ParseErrorType::kAtomOrLeftParenExpected, Peek());
      }

      Consume();
      tail = ParseExpression();
      if (IsAtEnd()) {
        throw IncompleteInputError();
      }
      if (!Match(TokenType::kRightParen)) {
        throw ParseError(ParseErrorType::kRightParenExpected, Peek());
      }
      Consume();
      return prepend_elements(std::move(tail));
    }
  }

  bool IsAtEnd() const { return current_index_ >= tokens_.size(); }
  bool Match(TokenType type) const { return !IsAtEnd() && Peek().type == type; }
  const Token& Peek() const { return tokens_[current_index_]; }
  Token Consume() { return tokens_[current_index_++]; }

  const TokensVector& tokens_;
  std::size_t current_index_ = 0;
};

// ---------------- Runtime values ----------------

struct PairValue;
struct ClosureValue;
struct Environment;

struct IntegerValue {
  std::int64_t value = 0;
};

struct FloatValue {
  double value = 0.0;
};

struct StringValue {
  std::string text;
};

struct ErrorObjectValue {
  std::string text;
};

struct SymbolValue {
  std::string text;
};

struct NilValue {};
struct TrueValue {};

struct ProcedureValue {
  std::string name;
};

struct MessageValue {
  std::string text;
};

struct NoReturnValue {};
struct UnspecifiedValue {};

using PairPtr = std::shared_ptr<PairValue>;
using ClosurePtr = std::shared_ptr<ClosureValue>;

struct Value {
  using Data =
      std::variant<IntegerValue, FloatValue, StringValue, ErrorObjectValue,
                   SymbolValue, NilValue, TrueValue, PairPtr, ProcedureValue,
                   ClosurePtr, MessageValue, NoReturnValue, UnspecifiedValue>;

  Data data = NilValue{};
  std::uint64_t identity = 0;
};

struct PairValue {
  Value car;
  Value cdr;
};

struct Environment {
  explicit Environment(Environment* parent_environment = nullptr)
      : parent(parent_environment) {}

  std::unordered_map<std::string, Value> bindings;
  Environment* parent = nullptr;
};

struct ClosureValue {
  std::vector<std::string> parameters;
  std::vector<std::shared_ptr<const AstNode>> body;
  std::string display_name;
};

std::uint64_t NextValueIdentity() {
  static std::uint64_t next_identity = 1;
  return next_identity++;
}

template <typename Data>
Value MakeValue(Data data) {
  return Value{
      .data = std::move(data),
      .identity = NextValueIdentity(),
  };
}

Value MakeNil() { return MakeValue(NilValue{}); }

Value MakeTrue() { return MakeValue(TrueValue{}); }

Value MakeBoolean(bool condition) { return condition ? MakeTrue() : MakeNil(); }

Value MakeInteger(std::int64_t integer) {
  return MakeValue(IntegerValue{.value = integer});
}

Value MakeFloat(double real) { return MakeValue(FloatValue{.value = real}); }

template <typename TextValue>
Value MakeTextValue(std::string text) {
  return MakeValue(TextValue{.text = std::move(text)});
}

Value MakeString(std::string text) {
  return MakeTextValue<StringValue>(std::move(text));
}

Value MakeErrorObject(std::string text) {
  return MakeTextValue<ErrorObjectValue>(std::move(text));
}

Value MakeSymbol(std::string text) {
  return MakeTextValue<SymbolValue>(std::move(text));
}

Value MakeProcedure(std::string name) {
  return MakeValue(ProcedureValue{.name = std::move(name)});
}

Value MakeMessage(std::string text) {
  return MakeTextValue<MessageValue>(std::move(text));
}

Value MakeNoReturn() { return MakeValue(NoReturnValue{}); }

Value MakeUnspecified() { return MakeValue(UnspecifiedValue{}); }

Value MakePair(Value car, Value cdr) {
  return MakeValue(std::make_shared<PairValue>(
      PairValue{.car = std::move(car), .cdr = std::move(cdr)}));
}

Value MakeClosure(std::vector<std::string> parameters,
                  std::vector<std::shared_ptr<const AstNode>> body,
                  std::string display_name) {
  return MakeValue(std::make_shared<ClosureValue>(ClosureValue{
      .parameters = std::move(parameters),
      .body = std::move(body),
      .display_name = std::move(display_name),
  }));
}

std::string QuoteText(std::string_view text) {
  return std::format("\"{}\"", text);
}

std::string_view UnquoteText(std::string_view text) {
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    text.remove_prefix(1);
    text.remove_suffix(1);
  }
  return text;
}

template <typename Data>
bool IsValue(const Value& value) {
  return std::holds_alternative<Data>(value.data);
}

const PairValue& GetPair(const Value& value) {
  return *std::get<PairPtr>(value.data);
}

const ClosureValue& GetClosure(const Value& value) {
  return *std::get<ClosurePtr>(value.data);
}

std::int64_t GetInteger(const Value& value) {
  return std::get<IntegerValue>(value.data).value;
}

double GetFloat(const Value& value) {
  return std::get<FloatValue>(value.data).value;
}

const std::string& GetText(const Value& value) {
  return std::visit(
      []<typename T>(const T& data) -> const std::string& {
        using Data = std::decay_t<T>;
        if constexpr (std::is_same_v<Data, StringValue> ||
                      std::is_same_v<Data, ErrorObjectValue> ||
                      std::is_same_v<Data, SymbolValue> ||
                      std::is_same_v<Data, MessageValue>) {
          return data.text;
        } else if constexpr (std::is_same_v<Data, ProcedureValue>) {
          return data.name;
        } else {
          throw std::logic_error("Value does not contain text");
        }
      },
      value.data);
}

bool IsStringLike(const Value& value) {
  return IsValue<StringValue>(value) || IsValue<ErrorObjectValue>(value);
}

bool IsNil(const Value& value) { return IsValue<NilValue>(value); }

bool IsPair(const Value& value) { return IsValue<PairPtr>(value); }

bool IsTruthy(const Value& value) { return !IsNil(value); }

bool HasBinding(const Value& value) {
  return !IsValue<NoReturnValue>(value) && !IsValue<UnspecifiedValue>(value);
}

bool ShouldPrint(const Value& value) {
  return !IsValue<UnspecifiedValue>(value);
}

bool IsNumber(const Value& value) {
  return IsValue<IntegerValue>(value) || IsValue<FloatValue>(value);
}

double AsDouble(const Value& value) {
  if (const auto* integer = std::get_if<IntegerValue>(&value.data)) {
    return static_cast<double>(integer->value);
  }
  return std::get<FloatValue>(value.data).value;
}

bool IsProperList(Value value) {
  while (IsPair(value)) {
    value = GetPair(value).cdr;
  }
  return IsNil(value);
}

NodePtr CloneSyntax(const AstNode* node) {
  if (const auto* atom = GetAtom(node)) {
    return MakeAtom(atom->GetToken());
  }

  const auto* cons = GetCons(node);
  return MakeCons(CloneSyntax(cons->Car()), CloneSyntax(cons->Cdr()));
}

std::shared_ptr<const AstNode> ShareSyntax(const AstNode* node) {
  return std::shared_ptr<const AstNode>(CloneSyntax(node).release());
}

// ---------------- Value printer ----------------

constexpr int kIndentWidth = 2;

void PrintValue(const Value& value, int indentation = 0,
                bool should_indent = true, bool end_with_newline = true);

void PrintIndentation(int indentation) {
  for (int count = 0; count < indentation; ++count) {
    std::print(" ");
  }
}

void PrintAtomicValue(const Value& value, bool end_with_newline) {
  const auto print_text = [end_with_newline](std::string_view text) {
    if (end_with_newline) {
      std::println("{}", text);
    } else {
      std::print("{}", text);
    }
  };

  std::visit(
      [&print_text]<typename T>(const T& data) {
        using Data = std::decay_t<T>;
        if constexpr (std::is_same_v<Data, IntegerValue>) {
          print_text(std::format("{}", data.value));
        } else if constexpr (std::is_same_v<Data, FloatValue>) {
          print_text(std::format("{:.3f}", data.value));
        } else if constexpr (std::is_same_v<Data, StringValue> ||
                             std::is_same_v<Data, ErrorObjectValue> ||
                             std::is_same_v<Data, SymbolValue> ||
                             std::is_same_v<Data, MessageValue>) {
          print_text(data.text);
        } else if constexpr (std::is_same_v<Data, NilValue>) {
          print_text("nil");
        } else if constexpr (std::is_same_v<Data, TrueValue>) {
          print_text("#t");
        } else if constexpr (std::is_same_v<Data, ProcedureValue>) {
          print_text(std::format("#<procedure {}>", data.name));
        } else if constexpr (std::is_same_v<Data, ClosurePtr>) {
          print_text(std::format("#<procedure {}>", data->display_name));
        }
      },
      value.data);
}

void PrintValue(const Value& value, int indentation, bool should_indent,
                bool end_with_newline) {
  if (!IsPair(value)) {
    if (should_indent) {
      PrintIndentation(indentation);
    }
    PrintAtomicValue(value, end_with_newline);
    return;
  }

  if (should_indent) {
    PrintIndentation(indentation);
  }
  std::print("( ");

  const Value* current = &value;
  bool is_first_element = true;
  while (IsPair(*current)) {
    const auto& [car, cdr] = GetPair(*current);
    PrintValue(car, indentation + kIndentWidth, !is_first_element, true);
    current = &cdr;
    is_first_element = false;
  }

  if (!IsNil(*current)) {
    PrintIndentation(indentation + kIndentWidth);
    std::println(".");
    PrintValue(*current, indentation + kIndentWidth, true, true);
  }

  PrintIndentation(indentation);
  if (end_with_newline) {
    std::println(")");
  } else {
    std::print(")");
  }
}

// ---------------- Evaluation errors ----------------

enum class EvalErrorType {
  kUnboundSymbol,
  kNonList,
  kIncorrectArgumentCount,
  kIncorrectArgumentType,
  kAttemptToApplyNonFunction,
  kNoReturnValue,
  kUnboundParameter,
  kUnboundTestCondition,
  kUnboundCondition,
  kDivisionByZero,
  kDefineFormat,
  kCondFormat,
  kLambdaFormat,
  kLetFormat,
  kSetFormat,
  kLevelOfDefine,
  kLevelOfCleanEnvironment,
  kLevelOfExit,
};

class EvalError final : public std::runtime_error {
 public:
  explicit EvalError(EvalErrorType type, std::string name = {},
                     std::optional<Value> value = std::nullopt)
      : std::runtime_error("OurScheme evaluation error"),
        type_(type),
        name_(std::move(name)),
        value_(std::move(value)) {}

  EvalErrorType GetType() const { return type_; }
  const std::string& GetName() const { return name_; }
  const Value& GetValue() const { return value_.value(); }

 private:
  EvalErrorType type_;
  std::string name_;
  std::optional<Value> value_;
};

void PrintEvalError(const EvalError& error) {
  const auto print_value_error = [&error](std::string_view header) {
    std::print("{}", header);
    PrintValue(error.GetValue(), 0, false);
  };

  switch (error.GetType()) {
    case EvalErrorType::kUnboundSymbol:
      std::println("ERROR (unbound symbol) : {}", error.GetName());
      break;
    case EvalErrorType::kNonList:
      print_value_error("ERROR (non-list) : ");
      break;
    case EvalErrorType::kIncorrectArgumentCount:
      std::println("ERROR (incorrect number of arguments) : {}",
                   error.GetName());
      break;
    case EvalErrorType::kIncorrectArgumentType:
      std::print("ERROR ({} with incorrect argument type) : ", error.GetName());
      PrintValue(error.GetValue(), 0, false);
      break;
    case EvalErrorType::kAttemptToApplyNonFunction:
      print_value_error("ERROR (attempt to apply non-function) : ");
      break;
    case EvalErrorType::kNoReturnValue:
      print_value_error("ERROR (no return value) : ");
      break;
    case EvalErrorType::kUnboundParameter:
      print_value_error("ERROR (unbound parameter) : ");
      break;
    case EvalErrorType::kUnboundTestCondition:
      print_value_error("ERROR (unbound test-condition) : ");
      break;
    case EvalErrorType::kUnboundCondition:
      print_value_error("ERROR (unbound condition) : ");
      break;
    case EvalErrorType::kDivisionByZero:
      std::println("ERROR (division by zero) : {}", error.GetName());
      break;
    case EvalErrorType::kDefineFormat:
      print_value_error("ERROR (DEFINE format) : ");
      break;
    case EvalErrorType::kCondFormat:
      print_value_error("ERROR (COND format) : ");
      break;
    case EvalErrorType::kLambdaFormat:
      print_value_error("ERROR (LAMBDA format) : ");
      break;
    case EvalErrorType::kLetFormat:
      print_value_error("ERROR (LET format) : ");
      break;
    case EvalErrorType::kSetFormat:
      print_value_error("ERROR (SET! format) : ");
      break;
    case EvalErrorType::kLevelOfDefine:
      std::println("ERROR (level of DEFINE)");
      break;
    case EvalErrorType::kLevelOfCleanEnvironment:
      std::println("ERROR (level of CLEAN-ENVIRONMENT)");
      break;
    case EvalErrorType::kLevelOfExit:
      std::println("ERROR (level of EXIT)");
      break;
  }
}

// ---------------- Evaluator ----------------

class Evaluator {
 public:
  using ReadCallback = std::function<Value()>;

  explicit Evaluator(ReadCallback read_callback)
      : read_callback_(std::move(read_callback)) {
    RegisterProcedures();
  }

  static Value ConvertSyntaxToValue(const AstNode* expression) {
    return QuoteSyntax(expression);
  }

  Value EvaluateTopLevel(const AstNode* expression) {
    Value result = Evaluate(expression, global_environment_, true);
    if (IsValue<NoReturnValue>(result)) {
      throw EvalError(EvalErrorType::kNoReturnValue, {},
                      QuoteSyntax(expression));
    }
    return result;
  }

 private:
  using SyntaxList = std::vector<const AstNode*>;
  using Values = std::vector<Value>;

  Value Evaluate(const AstNode* expression, Environment& environment,
                 bool is_top_level = false) {
    if (const auto* atom = GetAtom(expression)) {
      return EvaluateAtom(atom->GetToken(), environment);
    }
    return EvaluateList(expression, environment, is_top_level);
  }

  void RegisterProcedures() {
    constexpr std::array kProcedureNames = {
        "cons",
        "list",
        "car",
        "cdr",
        "atom?",
        "pair?",
        "list?",
        "null?",
        "integer?",
        "real?",
        "number?",
        "string?",
        "boolean?",
        "symbol?",
        "+",
        "-",
        "*",
        "/",
        "not",
        ">",
        ">=",
        "<",
        "<=",
        "=",
        "string-append",
        "string>?",
        "string<?",
        "string=?",
        "eqv?",
        "equal?",
        "create-error-object",
        "error-object?",
        "read",
        "write",
        "display-string",
        "newline",
        "symbol->string",
        "number->string",
        "eval",
        "verbose",
        "verbose?",
        "exit",
    };

    for (const auto& name : kProcedureNames) {
      procedures_.emplace(name, MakeProcedure(name));
      reserved_names_.insert(name);
    }

    for (const auto& name :
         {"quote", "define", "and", "or", "begin", "if", "cond", "lambda",
          "set!", "let", "clean-environment"}) {
      reserved_names_.insert(name);
    }
  }

  static std::optional<SyntaxList> CollectProperSyntaxList(
      const AstNode* node) {
    SyntaxList elements;
    const AstNode* current = node;

    while (const auto* pair = GetCons(current)) {
      elements.push_back(pair->Car());
      current = pair->Cdr();
    }

    if (!IsNilSyntax(current)) {
      return std::nullopt;
    }
    return elements;
  }

  static std::vector<std::shared_ptr<const AstNode>> ShareSyntaxList(
      const SyntaxList& syntax_list) {
    std::vector<std::shared_ptr<const AstNode>> result;
    result.reserve(syntax_list.size());
    for (const AstNode* syntax : syntax_list) {
      result.push_back(ShareSyntax(syntax));
    }
    return result;
  }

  static Value QuoteSyntax(const AstNode* expression) {
    if (const auto* atom = GetAtom(expression)) {
      switch (const Token& token = atom->GetToken(); token.type) {
        case TokenType::kInt:
          return MakeInteger(std::stoll(token.value));
        case TokenType::kFloat:
          return MakeFloat(std::stod(token.value));
        case TokenType::kString:
          return MakeString(token.value);
        case TokenType::kErrorObject:
          return MakeErrorObject(token.value);
        case TokenType::kNil:
          return MakeNil();
        case TokenType::kT:
          return MakeTrue();
        default:
          return MakeSymbol(token.value);
      }
    }

    const auto* pair = GetCons(expression);
    return MakePair(QuoteSyntax(pair->Car()), QuoteSyntax(pair->Cdr()));
  }

  Value EvaluateAtom(const Token& token, const Environment& environment) const {
    switch (token.type) {
      case TokenType::kInt:
        return MakeInteger(std::stoll(token.value));
      case TokenType::kFloat:
        return MakeFloat(std::stod(token.value));
      case TokenType::kString:
        return MakeString(token.value);
      case TokenType::kErrorObject:
        return MakeErrorObject(token.value);
      case TokenType::kNil:
        return MakeNil();
      case TokenType::kT:
        return MakeTrue();
      case TokenType::kSymbol:
        return ResolveSymbol(token.value, environment);
      default:
        return MakeSymbol(token.value);
    }
  }

  Value ResolveSymbol(const std::string& name,
                      const Environment& environment) const {
    const Environment* current_environment = &environment;
    while (current_environment != nullptr) {
      if (const auto binding = current_environment->bindings.find(name);
          binding != current_environment->bindings.end()) {
        return binding->second;
      }
      current_environment = current_environment->parent;
    }

    if (const auto binding = global_environment_.bindings.find(name);
        binding != global_environment_.bindings.end()) {
      return binding->second;
    }

    if (const auto procedure = procedures_.find(name);
        procedure != procedures_.end()) {
      return procedure->second;
    }
    throw EvalError(EvalErrorType::kUnboundSymbol, name);
  }

  Value EvaluateList(const AstNode* expression, Environment& environment,
                     bool is_top_level) {
    const std::optional<SyntaxList> elements =
        CollectProperSyntaxList(expression);
    if (!elements.has_value()) {
      throw EvalError(EvalErrorType::kNonList, {}, QuoteSyntax(expression));
    }

    const auto get_symbol_name =
        [](const AstNode* node) -> std::optional<std::string> {
      const auto* atom = GetAtom(node);
      if (atom == nullptr || atom->GetToken().type != TokenType::kSymbol) {
        return std::nullopt;
      }
      return atom->GetToken().value;
    };

    const std::optional<std::string> operator_name =
        get_symbol_name(elements->front());
    const SyntaxList arguments(elements->begin() + 1, elements->end());

    if (operator_name.has_value()) {
      if (*operator_name == "quote") {
        return EvaluateQuote(arguments);
      }
      if (*operator_name == "define") {
        return EvaluateDefine(arguments, expression, environment, is_top_level);
      }
      if (*operator_name == "and") {
        return EvaluateAnd(arguments, environment);
      }
      if (*operator_name == "or") {
        return EvaluateOr(arguments, environment);
      }
      if (*operator_name == "if") {
        return EvaluateIf(arguments, expression, environment);
      }
      if (*operator_name == "cond") {
        return EvaluateCond(arguments, expression, environment);
      }
      if (*operator_name == "begin") {
        return EvaluateSequenceForm("begin", arguments, environment);
      }
      if (*operator_name == "lambda") {
        return EvaluateLambda(arguments, expression, environment);
      }
      if (*operator_name == "let") {
        return EvaluateLet(arguments, expression, environment);
      }
      if (*operator_name == "set!") {
        return EvaluateSet(arguments, expression, environment);
      }
      if (*operator_name == "clean-environment") {
        return EvaluateCleanEnvironment(arguments, is_top_level);
      }
      if (*operator_name == "exit" && !is_top_level) {
        throw EvalError(EvalErrorType::kLevelOfExit);
      }
    }

    Value procedure = Evaluate(elements->front(), environment);
    if (!HasBinding(procedure)) {
      throw EvalError(EvalErrorType::kNoReturnValue, {},
                      QuoteSyntax(elements->front()));
    }
    const auto* primitive = std::get_if<ProcedureValue>(&procedure.data);
    if (const auto* closure = std::get_if<ClosurePtr>(&procedure.data);
        primitive == nullptr && closure == nullptr) {
      throw EvalError(EvalErrorType::kAttemptToApplyNonFunction, {}, procedure);
    }

    if (primitive != nullptr) {
      ValidateProcedureArgumentCount(primitive->name, arguments.size());
    } else {
      ValidateClosureArgumentCount(procedure, arguments.size(),
                                   operator_name.has_value());
    }

    Values evaluated_arguments;
    evaluated_arguments.reserve(arguments.size());
    for (const AstNode* argument : arguments) {
      Value result = Evaluate(argument, environment);
      if (!HasBinding(result)) {
        throw EvalError(EvalErrorType::kUnboundParameter, {},
                        QuoteSyntax(argument));
      }
      evaluated_arguments.push_back(std::move(result));
    }

    if (primitive != nullptr) {
      return ApplyProcedure(primitive->name, evaluated_arguments, environment);
    }
    return ApplyClosure(procedure, evaluated_arguments);
  }

  static Value EvaluateQuote(const SyntaxList& arguments) {
    RequireArgumentCount("quote", arguments.size(), 1);
    return QuoteSyntax(arguments.front());
  }

  Value EvaluateDefine(const SyntaxList& arguments,
                       const AstNode* original_expression,
                       Environment& environment, bool is_top_level) {
    if (!is_top_level) {
      throw EvalError(EvalErrorType::kLevelOfDefine);
    }

    const auto throw_format_error = [original_expression]() {
      throw EvalError(EvalErrorType::kDefineFormat, {},
                      QuoteSyntax(original_expression));
    };
    const auto define_binding = [this](const std::string& name, Value value) {
      global_environment_.bindings[name] = std::move(value);
      if (verbose_) {
        std::println("{} defined", name);
      }
      return MakeUnspecified();
    };

    if (arguments.empty()) {
      throw_format_error();
    }

    if (const auto* name_atom = GetAtom(arguments.front())) {
      if (arguments.size() != 2 ||
          name_atom->GetToken().type != TokenType::kSymbol ||
          reserved_names_.contains(name_atom->GetToken().value)) {
        throw_format_error();
      }

      Value value = Evaluate(arguments[1], environment);
      if (!HasBinding(value)) {
        throw EvalError(EvalErrorType::kNoReturnValue, {},
                        QuoteSyntax(arguments[1]));
      }
      return define_binding(name_atom->GetToken().value, std::move(value));
    }

    const std::optional<SyntaxList> signature =
        CollectProperSyntaxList(arguments.front());
    if (!signature.has_value() || signature->empty() || arguments.size() < 2) {
      throw_format_error();
    }

    const auto* name_atom = GetAtom(signature->front());
    if (name_atom == nullptr ||
        name_atom->GetToken().type != TokenType::kSymbol ||
        reserved_names_.contains(name_atom->GetToken().value)) {
      throw_format_error();
    }

    const std::optional<std::vector<std::string>> parameters =
        ParseParameterList(
            SyntaxList(signature->begin() + 1, signature->end()));
    if (!parameters.has_value()) {
      throw_format_error();
    }

    const SyntaxList body(arguments.begin() + 1, arguments.end());
    Value closure = MakeClosure(*parameters, ShareSyntaxList(body),
                                name_atom->GetToken().value);
    return define_binding(name_atom->GetToken().value, std::move(closure));
  }

  Value EvaluateAnd(const SyntaxList& arguments, Environment& environment) {
    RequireMinimumArgumentCount("and", arguments.size(), 2);
    Value result = MakeTrue();
    for (const AstNode* argument : arguments) {
      result = Evaluate(argument, environment);
      if (!HasBinding(result)) {
        throw EvalError(EvalErrorType::kUnboundCondition, {},
                        QuoteSyntax(argument));
      }
      if (!IsTruthy(result)) {
        return result;
      }
    }
    return result;
  }

  Value EvaluateOr(const SyntaxList& arguments, Environment& environment) {
    RequireMinimumArgumentCount("or", arguments.size(), 2);
    Value result = MakeNil();
    for (const AstNode* argument : arguments) {
      result = Evaluate(argument, environment);
      if (!HasBinding(result)) {
        throw EvalError(EvalErrorType::kUnboundCondition, {},
                        QuoteSyntax(argument));
      }
      if (IsTruthy(result)) {
        return result;
      }
    }
    return result;
  }

  Value EvaluateIf(const SyntaxList& arguments,
                   const AstNode* original_expression,
                   Environment& environment) {
    if (arguments.size() != 2 && arguments.size() != 3) {
      throw EvalError(EvalErrorType::kIncorrectArgumentCount, "if");
    }

    const Value condition = Evaluate(arguments[0], environment);
    if (!HasBinding(condition)) {
      throw EvalError(EvalErrorType::kUnboundTestCondition, {},
                      QuoteSyntax(arguments[0]));
    }
    if (IsTruthy(condition)) {
      return Evaluate(arguments[1], environment);
    }
    if (arguments.size() == 3) {
      return Evaluate(arguments[2], environment);
    }

    static_cast<void>(original_expression);
    return MakeNoReturn();
  }

  Value EvaluateCond(const SyntaxList& clauses,
                     const AstNode* original_expression,
                     Environment& environment) {
    const auto throw_format_error = [original_expression]() {
      throw EvalError(EvalErrorType::kCondFormat, {},
                      QuoteSyntax(original_expression));
    };

    if (clauses.empty()) {
      throw_format_error();
    }

    std::vector<SyntaxList> parsed_clauses;
    parsed_clauses.reserve(clauses.size());
    for (const AstNode* clause : clauses) {
      std::optional<SyntaxList> elements = CollectProperSyntaxList(clause);
      if (!elements.has_value() || elements->size() < 2) {
        throw_format_error();
      }
      parsed_clauses.push_back(std::move(*elements));
    }

    const auto is_last_else_clause = [](const SyntaxList& clause,
                                        bool is_last_clause) {
      if (!is_last_clause) {
        return false;
      }
      const auto* atom = GetAtom(clause.front());
      return atom != nullptr && atom->GetToken().type == TokenType::kSymbol &&
             atom->GetToken().value == "else";
    };

    for (std::size_t index = 0; index < parsed_clauses.size(); ++index) {
      const SyntaxList& clause = parsed_clauses[index];
      const bool is_else =
          is_last_else_clause(clause, index + 1 == parsed_clauses.size());
      bool matches = is_else;
      if (!is_else) {
        Value condition = Evaluate(clause.front(), environment);
        if (!HasBinding(condition)) {
          throw EvalError(EvalErrorType::kUnboundTestCondition, {},
                          QuoteSyntax(clause.front()));
        }
        matches = IsTruthy(condition);
      }
      if (matches) {
        return EvaluateSequence(SyntaxList(clause.begin() + 1, clause.end()),
                                environment);
      }
    }

    return MakeNoReturn();
  }

  Value EvaluateSequenceForm(const std::string& name,
                             const SyntaxList& arguments,
                             Environment& environment) {
    RequireMinimumArgumentCount(name, arguments.size(), 1);
    return EvaluateSequence(arguments, environment);
  }

  Value EvaluateSequence(const SyntaxList& expressions,
                         Environment& environment) {
    Value result = MakeNoReturn();
    for (const AstNode* expression : expressions) {
      result = Evaluate(expression, environment);
    }
    return result;
  }

  static Value EvaluateLambda(const SyntaxList& arguments,
                              const AstNode* original_expression,
                              const Environment& environment) {
    static_cast<void>(environment);
    const auto throw_format_error = [original_expression]() {
      throw EvalError(EvalErrorType::kLambdaFormat, {},
                      QuoteSyntax(original_expression));
    };

    if (arguments.size() < 2) {
      throw_format_error();
    }

    const std::optional<SyntaxList> parameter_syntax =
        CollectProperSyntaxList(arguments.front());
    if (!parameter_syntax.has_value()) {
      throw_format_error();
    }
    const std::optional<std::vector<std::string>> parameters =
        ParseParameterList(*parameter_syntax);
    if (!parameters.has_value()) {
      throw_format_error();
    }

    const SyntaxList body(arguments.begin() + 1, arguments.end());
    return MakeClosure(*parameters, ShareSyntaxList(body), "lambda");
  }

  Value EvaluateLet(const SyntaxList& arguments,
                    const AstNode* original_expression,
                    Environment& environment) {
    const auto throw_format_error = [original_expression]() {
      throw EvalError(EvalErrorType::kLetFormat, {},
                      QuoteSyntax(original_expression));
    };

    if (arguments.size() < 2) {
      throw_format_error();
    }

    const std::optional<SyntaxList> binding_syntax =
        CollectProperSyntaxList(arguments.front());
    if (!binding_syntax.has_value()) {
      throw_format_error();
    }

    std::vector<std::pair<std::string, const AstNode*>> bindings;
    std::unordered_set<std::string> local_names;
    bindings.reserve(binding_syntax->size());
    for (const AstNode* binding : *binding_syntax) {
      const std::optional<SyntaxList> elements =
          CollectProperSyntaxList(binding);
      if (!elements.has_value() || elements->size() != 2) {
        throw_format_error();
      }
      const auto* name_atom = GetAtom(elements->front());
      if (name_atom == nullptr ||
          name_atom->GetToken().type != TokenType::kSymbol ||
          reserved_names_.contains(name_atom->GetToken().value) ||
          !local_names.insert(name_atom->GetToken().value).second) {
        throw_format_error();
      }
      bindings.emplace_back(name_atom->GetToken().value, (*elements)[1]);
    }

    std::vector<Value> values;
    values.reserve(bindings.size());
    for (const auto& [name, initializer] : bindings) {
      static_cast<void>(name);
      Value value = Evaluate(initializer, environment);
      if (!HasBinding(value)) {
        throw EvalError(EvalErrorType::kNoReturnValue, {},
                        QuoteSyntax(initializer));
      }
      values.push_back(std::move(value));
    }

    Environment local_environment(&environment);
    for (std::size_t index = 0; index < bindings.size(); ++index) {
      local_environment.bindings[bindings[index].first] =
          std::move(values[index]);
    }

    return EvaluateSequence(SyntaxList(arguments.begin() + 1, arguments.end()),
                            local_environment);
  }

  Value EvaluateSet(const SyntaxList& arguments,
                    const AstNode* original_expression,
                    Environment& environment) {
    const auto throw_format_error = [original_expression]() {
      throw EvalError(EvalErrorType::kSetFormat, {},
                      QuoteSyntax(original_expression));
    };

    if (arguments.size() != 2) {
      throw_format_error();
    }

    const auto* name_atom = GetAtom(arguments.front());
    if (name_atom == nullptr ||
        name_atom->GetToken().type != TokenType::kSymbol ||
        reserved_names_.contains(name_atom->GetToken().value)) {
      throw_format_error();
    }

    Value value = Evaluate(arguments[1], environment);
    if (!HasBinding(value)) {
      throw EvalError(EvalErrorType::kNoReturnValue, {},
                      QuoteSyntax(arguments[1]));
    }

    AssignBinding(name_atom->GetToken().value, value, environment);
    return value;
  }

  void AssignBinding(const std::string& name, Value value,
                     Environment& environment) {
    Environment* current_environment = &environment;
    while (current_environment != nullptr) {
      if (const auto binding = current_environment->bindings.find(name);
          binding != current_environment->bindings.end()) {
        binding->second = std::move(value);
        return;
      }
      current_environment = current_environment->parent;
    }

    global_environment_.bindings[name] = std::move(value);
  }

  Value EvaluateCleanEnvironment(const SyntaxList& arguments,
                                 bool is_top_level) {
    if (!is_top_level) {
      throw EvalError(EvalErrorType::kLevelOfCleanEnvironment);
    }

    RequireArgumentCount("clean-environment", arguments.size(), 0);
    global_environment_.bindings.clear();
    if (verbose_) {
      std::println("environment cleaned");
    }
    return MakeUnspecified();
  }

  static std::optional<std::vector<std::string>> ParseParameterList(
      const SyntaxList& parameter_syntax) {
    std::vector<std::string> parameters;
    std::unordered_set<std::string> seen_names;
    parameters.reserve(parameter_syntax.size());

    for (const AstNode* parameter : parameter_syntax) {
      const auto* atom = GetAtom(parameter);
      if (atom == nullptr || atom->GetToken().type != TokenType::kSymbol ||
          !seen_names.insert(atom->GetToken().value).second) {
        return std::nullopt;
      }
      parameters.push_back(atom->GetToken().value);
    }
    return parameters;
  }

  static void ValidateClosureArgumentCount(const Value& closure,
                                           std::size_t argument_count,
                                           bool called_by_symbol) {
    if (GetClosure(closure).parameters.size() == argument_count) {
      return;
    }

    const std::string& display_name = GetClosure(closure).display_name;
    static_cast<void>(called_by_symbol);
    throw EvalError(EvalErrorType::kIncorrectArgumentCount, display_name);
  }

  Value ApplyClosure(const Value& closure, const Values& arguments) {
    Environment local_environment;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
      local_environment.bindings[GetClosure(closure).parameters[index]] =
          arguments[index];
    }

    Value result = MakeNoReturn();
    for (const auto& expression : GetClosure(closure).body) {
      result = Evaluate(expression.get(), local_environment);
    }
    return result;
  }

  static NodePtr ValueToSyntax(const Value& value) {
    if (const auto* integer = std::get_if<IntegerValue>(&value.data)) {
      return MakeAtom(Token{.value = std::format("{}", integer->value),
                            .type = TokenType::kInt});
    }
    if (const auto* real = std::get_if<FloatValue>(&value.data)) {
      return MakeAtom(Token{.value = std::format("{:.3f}", real->value),
                            .type = TokenType::kFloat});
    }
    if (IsValue<StringValue>(value)) {
      return MakeAtom(
          Token{.value = GetText(value), .type = TokenType::kString});
    }
    if (IsValue<ErrorObjectValue>(value)) {
      return MakeAtom(
          Token{.value = GetText(value), .type = TokenType::kErrorObject});
    }
    if (IsValue<SymbolValue>(value)) {
      return MakeAtom(
          Token{.value = GetText(value), .type = TokenType::kSymbol});
    }
    if (IsValue<NilValue>(value)) {
      return MakeNilSyntax();
    }
    if (IsValue<TrueValue>(value)) {
      return MakeAtom(Token{.value = "#t", .type = TokenType::kT});
    }
    if (IsValue<PairPtr>(value)) {
      const PairValue& pair = GetPair(value);
      NodePtr car = ValueToSyntax(pair.car);
      NodePtr cdr = ValueToSyntax(pair.cdr);
      if (!car || !cdr) {
        return nullptr;
      }
      return MakeCons(std::move(car), std::move(cdr));
    }
    if (const auto* procedure = std::get_if<ProcedureValue>(&value.data)) {
      return MakeAtom(
          Token{.value = procedure->name, .type = TokenType::kSymbol});
    }
    return nullptr;
  }

  Value EvaluateRuntimeValue(const Value& value, Environment& environment) {
    if (IsValue<ClosurePtr>(value) || IsValue<ProcedureValue>(value) ||
        IsValue<ErrorObjectValue>(value)) {
      return value;
    }

    const NodePtr syntax = ValueToSyntax(value);
    if (!syntax) {
      return value;
    }
    return Evaluate(syntax.get(), environment, true);
  }

  static void ValidateProcedureArgumentCount(const std::string& name,
                                             std::size_t argument_count) {
    const auto requires_exactly = [&name,
                                   argument_count](std::size_t expected) {
      RequireArgumentCount(name, argument_count, expected);
    };
    const auto requires_at_least = [&name,
                                    argument_count](std::size_t minimum) {
      RequireMinimumArgumentCount(name, argument_count, minimum);
    };

    if (name == "cons" || name == "eqv?" || name == "equal?") {
      requires_exactly(2);
      return;
    }

    if (name == "car" || name == "cdr" || name == "atom?" || name == "pair?" ||
        name == "list?" || name == "null?" || name == "integer?" ||
        name == "real?" || name == "number?" || name == "string?" ||
        name == "boolean?" || name == "symbol?" || name == "not" ||
        name == "verbose" || name == "create-error-object" ||
        name == "error-object?" || name == "write" ||
        name == "display-string" || name == "symbol->string" ||
        name == "number->string" || name == "eval") {
      requires_exactly(1);
      return;
    }

    if (name == "exit" || name == "verbose?" || name == "read" ||
        name == "newline") {
      requires_exactly(0);
      return;
    }

    if (name == "+" || name == "-" || name == "*" || name == "/" ||
        name == ">" || name == ">=" || name == "<" || name == "<=" ||
        name == "=" || name == "string-append" || name == "string>?" ||
        name == "string<?" || name == "string=?") {
      requires_at_least(2);
    }

    // list accepts any number of arguments.
  }

  Value ApplyProcedure(const std::string& name, const Values& arguments,
                       Environment& environment) {
    if (name == "cons") {
      RequireArgumentCount(name, arguments.size(), 2);
      return MakePair(arguments[0], arguments[1]);
    }
    if (name == "list") {
      Value result = MakeNil();
      for (const Value& argument : std::ranges::reverse_view(arguments)) {
        result = MakePair(argument, result);
      }
      return result;
    }
    if (name == "car" || name == "cdr") {
      RequireArgumentCount(name, arguments.size(), 1);
      RequireType(name, arguments[0], IsPair(arguments[0]));
      const auto& [car, cdr] = GetPair(arguments[0]);
      return name == "car" ? car : cdr;
    }
    if (name == "atom?" || name == "pair?" || name == "list?" ||
        name == "null?" || name == "integer?" || name == "real?" ||
        name == "number?" || name == "string?" || name == "boolean?" ||
        name == "symbol?") {
      RequireArgumentCount(name, arguments.size(), 1);
      return ApplyPredicate(name, arguments[0]);
    }
    if (name == "+" || name == "-" || name == "*" || name == "/") {
      return ApplyArithmetic(name, arguments);
    }
    if (name == "not") {
      RequireArgumentCount(name, arguments.size(), 1);
      return MakeBoolean(!IsTruthy(arguments[0]));
    }
    if (name == ">" || name == ">=" || name == "<" || name == "<=" ||
        name == "=") {
      return ApplyNumericComparison(name, arguments);
    }
    if (name == "string-append") {
      return ApplyStringAppend(arguments);
    }
    if (name == "string>?" || name == "string<?" || name == "string=?") {
      return ApplyStringComparison(name, arguments);
    }
    if (name == "eqv?" || name == "equal?") {
      RequireArgumentCount(name, arguments.size(), 2);
      return MakeBoolean(name == "eqv?" ? AreEqv(arguments[0], arguments[1])
                                        : AreEqual(arguments[0], arguments[1]));
    }
    if (name == "create-error-object") {
      RequireArgumentCount(name, arguments.size(), 1);
      RequireType(name, arguments[0], IsValue<StringValue>(arguments[0]));
      return MakeErrorObject(GetText(arguments[0]));
    }
    if (name == "error-object?") {
      RequireArgumentCount(name, arguments.size(), 1);
      return MakeBoolean(IsValue<ErrorObjectValue>(arguments[0]));
    }
    if (name == "read") {
      RequireArgumentCount(name, arguments.size(), 0);
      return read_callback_();
    }
    if (name == "write") {
      RequireArgumentCount(name, arguments.size(), 1);
      PrintValue(arguments[0], 0, false, false);
      return arguments[0];
    }
    if (name == "display-string") {
      RequireArgumentCount(name, arguments.size(), 1);
      RequireType(name, arguments[0], IsStringLike(arguments[0]));
      std::print("{}", UnquoteText(GetText(arguments[0])));
      return arguments[0];
    }
    if (name == "newline") {
      RequireArgumentCount(name, arguments.size(), 0);
      std::println();
      return MakeNil();
    }
    if (name == "symbol->string") {
      RequireArgumentCount(name, arguments.size(), 1);
      RequireType(name, arguments[0], IsValue<SymbolValue>(arguments[0]));
      return MakeString(QuoteText(GetText(arguments[0])));
    }
    if (name == "number->string") {
      RequireArgumentCount(name, arguments.size(), 1);
      RequireType(name, arguments[0], IsNumber(arguments[0]));
      if (IsValue<IntegerValue>(arguments[0])) {
        return MakeString(std::format("\"{}\"", GetInteger(arguments[0])));
      }
      return MakeString(std::format("\"{:.3f}\"", GetFloat(arguments[0])));
    }
    if (name == "eval") {
      RequireArgumentCount(name, arguments.size(), 1);
      return EvaluateRuntimeValue(arguments[0], environment);
    }
    if (name == "verbose") {
      RequireArgumentCount(name, arguments.size(), 1);
      verbose_ = IsTruthy(arguments[0]);
      return MakeBoolean(verbose_);
    }
    if (name == "verbose?") {
      RequireArgumentCount(name, arguments.size(), 0);
      return MakeBoolean(verbose_);
    }
    if (name == "exit") {
      RequireArgumentCount(name, arguments.size(), 0);
      return MakeNil();
    }

    throw EvalError(EvalErrorType::kUnboundSymbol, name);
  }

  static Value ApplyPredicate(const std::string& name, const Value& argument) {
    if (name == "atom?") {
      return MakeBoolean(!IsPair(argument));
    }
    if (name == "pair?") {
      return MakeBoolean(IsPair(argument));
    }
    if (name == "list?") {
      return MakeBoolean(IsProperList(argument));
    }
    if (name == "null?") {
      return MakeBoolean(IsNil(argument));
    }
    if (name == "integer?") {
      return MakeBoolean(IsValue<IntegerValue>(argument));
    }
    if (name == "real?" || name == "number?") {
      return MakeBoolean(IsNumber(argument));
    }
    if (name == "string?") {
      return MakeBoolean(IsStringLike(argument));
    }
    if (name == "boolean?") {
      return MakeBoolean(IsValue<NilValue>(argument) ||
                         IsValue<TrueValue>(argument));
    }
    return MakeBoolean(IsValue<SymbolValue>(argument));
  }

  static Value ApplyArithmetic(const std::string& name,
                               const Values& arguments) {
    RequireMinimumArgumentCount(name, arguments.size(), 2);
    for (const Value& argument : arguments) {
      RequireType(name, argument, IsNumber(argument));
    }

    const bool use_float = std::ranges::any_of(
        arguments,
        [](const Value& value) { return IsValue<FloatValue>(value); });

    if (name == "/") {
      for (std::size_t index = 1; index < arguments.size(); ++index) {
        if (AsDouble(arguments[index]) == 0.0) {
          throw EvalError(EvalErrorType::kDivisionByZero, name);
        }
      }
    }

    if (!use_float) {
      std::int64_t result = GetInteger(arguments.front());
      for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::int64_t operand = GetInteger(arguments[index]);
        if (name == "+") {
          result += operand;
        } else if (name == "-") {
          result -= operand;
        } else if (name == "*") {
          result *= operand;
        } else {
          result /= operand;
        }
      }
      return MakeInteger(result);
    }

    double result = AsDouble(arguments.front());
    for (std::size_t index = 1; index < arguments.size(); ++index) {
      const double operand = AsDouble(arguments[index]);
      if (name == "+") {
        result += operand;
      } else if (name == "-") {
        result -= operand;
      } else if (name == "*") {
        result *= operand;
      } else {
        result /= operand;
      }
    }
    return MakeFloat(result);
  }

  static Value ApplyNumericComparison(const std::string& name,
                                      const Values& arguments) {
    RequireMinimumArgumentCount(name, arguments.size(), 2);
    for (const Value& argument : arguments) {
      RequireType(name, argument, IsNumber(argument));
    }

    const auto compare = [&name](auto left, auto right) {
      if (name == ">") return left > right;
      if (name == ">=") return left >= right;
      if (name == "<") return left < right;
      if (name == "<=") return left <= right;
      return left == right;
    };

    for (std::size_t index = 1; index < arguments.size(); ++index) {
      if (!compare(AsDouble(arguments[index - 1]),
                   AsDouble(arguments[index]))) {
        return MakeNil();
      }
    }
    return MakeTrue();
  }

  static Value ApplyStringAppend(const Values& arguments) {
    RequireMinimumArgumentCount("string-append", arguments.size(), 2);
    std::string result = "\"";
    for (const Value& argument : arguments) {
      RequireType("string-append", argument, IsStringLike(argument));
      const std::string& text = GetText(argument);
      result += text.substr(1, text.size() - 2);
    }
    result += '"';
    return MakeString(result);
  }

  static Value ApplyStringComparison(const std::string& name,
                                     const Values& arguments) {
    RequireMinimumArgumentCount(name, arguments.size(), 2);
    for (const Value& argument : arguments) {
      RequireType(name, argument, IsStringLike(argument));
    }

    const auto inner_text = [](const Value& value) {
      const std::string& text = GetText(value);
      return text.substr(1, text.size() - 2);
    };
    const auto compare = [&name](const std::string& left,
                                 const std::string& right) {
      if (name == "string>?") return left > right;
      if (name == "string<?") return left < right;
      return left == right;
    };

    for (std::size_t index = 1; index < arguments.size(); ++index) {
      if (!compare(inner_text(arguments[index - 1]),
                   inner_text(arguments[index]))) {
        return MakeNil();
      }
    }
    return MakeTrue();
  }

  static bool AreEqv(const Value& left, const Value& right) {
    if (left.identity == right.identity) {
      return true;
    }
    if (IsPair(left) || IsPair(right) || IsStringLike(left) ||
        IsStringLike(right) || IsValue<ClosurePtr>(left) ||
        IsValue<ClosurePtr>(right)) {
      return false;
    }
    if (IsNumber(left) && IsNumber(right)) {
      return AsDouble(left) == AsDouble(right);
    }
    if (left.data.index() != right.data.index()) {
      return false;
    }
    if (IsValue<SymbolValue>(left) || IsValue<MessageValue>(left) ||
        IsValue<ProcedureValue>(left)) {
      return GetText(left) == GetText(right);
    }
    return true;
  }

  static bool AreEqual(const Value& left, const Value& right) {
    if (left.identity == right.identity) {
      return true;
    }
    if (IsNumber(left) && IsNumber(right)) {
      return AsDouble(left) == AsDouble(right);
    }
    if (IsStringLike(left) && IsStringLike(right)) {
      return GetText(left) == GetText(right);
    }
    if (left.data.index() != right.data.index()) {
      return false;
    }
    if (IsPair(left)) {
      const PairValue& left_pair = GetPair(left);
      const PairValue& right_pair = GetPair(right);
      return AreEqual(left_pair.car, right_pair.car) &&
             AreEqual(left_pair.cdr, right_pair.cdr);
    }
    if (IsValue<IntegerValue>(left)) {
      return GetInteger(left) == GetInteger(right);
    }
    if (IsValue<FloatValue>(left)) {
      return GetFloat(left) == GetFloat(right);
    }
    if (IsValue<ClosurePtr>(left)) {
      return false;
    }
    if (IsValue<SymbolValue>(left) || IsValue<MessageValue>(left) ||
        IsValue<ProcedureValue>(left)) {
      return GetText(left) == GetText(right);
    }
    return true;
  }

  static void RequireArgumentCount(const std::string& name, std::size_t actual,
                                   std::size_t expected) {
    if (actual != expected) {
      throw EvalError(EvalErrorType::kIncorrectArgumentCount, name);
    }
  }

  static void RequireMinimumArgumentCount(const std::string& name,
                                          std::size_t actual,
                                          std::size_t minimum) {
    if (actual < minimum) {
      throw EvalError(EvalErrorType::kIncorrectArgumentCount, name);
    }
  }

  static void RequireType(const std::string& name, const Value& value,
                          bool condition) {
    if (!condition) {
      throw EvalError(EvalErrorType::kIncorrectArgumentType, name, value);
    }
  }

  Environment global_environment_;
  ReadCallback read_callback_;
  std::unordered_map<std::string, Value> procedures_;
  std::unordered_set<std::string> reserved_names_;
  bool verbose_ = true;
};

// ---------------- Input handling ----------------

TokensVector ScanLine(std::string_view line, int logical_row) {
  TokensVector tokens;
  Scanner scanner(line, logical_row);
  while (const std::optional<Token> token = scanner.GetToken()) {
    tokens.push_back(*token);
  }
  return tokens;
}

std::optional<int> ReadAndAppendLine(TokensVector& pending_tokens,
                                     int logical_row) {
  std::string line;
  if (!std::getline(std::cin, line)) {
    return std::nullopt;
  }

  ++logical_row;
  TokensVector new_tokens = ScanLine(line, logical_row);
  pending_tokens.insert(pending_tokens.end(),
                        std::make_move_iterator(new_tokens.begin()),
                        std::make_move_iterator(new_tokens.end()));
  return logical_row;
}

void RebasePendingTokens(const Token& last_consumed_token,
                         TokensVector& pending_tokens) {
  const int consumed_row = last_consumed_token.starting_row;
  const int consumed_column = last_consumed_token.ending_column;

  for (Token& token : pending_tokens) {
    if (token.starting_row == consumed_row) {
      token.starting_row = 1;
      token.starting_column -= consumed_column;
      token.ending_column -= consumed_column;
    } else {
      token.starting_row -= consumed_row - 1;
    }
  }
}

int GetLastLogicalRow(const TokensVector& tokens) {
  if (tokens.empty()) {
    return 0;
  }
  return std::ranges::max(tokens |
                          std::views::transform([](const Token& token) {
                            return token.starting_row;
                          }));
}

bool IsExitExpression(const AstNode* expression) {
  const auto elements =
      [&expression]() -> std::optional<std::vector<const AstNode*>> {
    std::vector<const AstNode*> result;
    const AstNode* current = expression;
    while (const auto* pair = GetCons(current)) {
      result.push_back(pair->Car());
      current = pair->Cdr();
    }
    if (!IsNilSyntax(current)) {
      return std::nullopt;
    }
    return result;
  }();

  if (!elements.has_value() || elements->size() != 1) {
    return false;
  }

  const auto* atom = GetAtom(elements->front());
  return atom != nullptr && atom->GetToken().type == TokenType::kSymbol &&
         atom->GetToken().value == "exit";
}

constexpr std::string_view kEofExitMessage =
    "ERROR (no more input) : END-OF-FILE encountered\nThanks for using "
    "OurScheme!";

int main() {
  std::string test_case_number;
  std::getline(std::cin, test_case_number);

  std::println("Welcome to OurScheme!");
  std::println();

  TokensVector pending_tokens;
  int logical_row = 0;

  const auto read_next_line = [&pending_tokens, &logical_row]() {
    const std::optional<int> next_row =
        ReadAndAppendLine(pending_tokens, logical_row);
    if (!next_row.has_value()) {
      return false;
    }
    logical_row = *next_row;
    return true;
  };

  const auto discard_failed_input = [&pending_tokens, &logical_row]() {
    pending_tokens.clear();
    logical_row = 0;
  };

  const auto consume_expression = [&pending_tokens,
                                   &logical_row](std::size_t count) {
    const Token last_consumed_token = pending_tokens[count - 1];
    pending_tokens.erase(pending_tokens.begin(),
                         pending_tokens.begin() + count);
    RebasePendingTokens(last_consumed_token, pending_tokens);
    logical_row = GetLastLogicalRow(pending_tokens);
  };

  const auto parse_expression = [&pending_tokens]() {
    Parser parser(pending_tokens);
    NodePtr expression = parser.ParseOneExpression();
    const auto consumed_token_count = parser.ConsumedTokenCount();
    return std::pair(std::move(expression), consumed_token_count);
  };

  const auto read_value = [&pending_tokens, &read_next_line, &parse_expression,
                           &discard_failed_input,
                           &consume_expression]() -> Value {
    constexpr std::string_view kReadEofMessage =
        "ERROR : END-OF-FILE encountered when there should be more input";

    const auto make_read_error = [](std::string_view message) {
      return MakeErrorObject(QuoteText(message));
    };
    while (true) {
      if (pending_tokens.empty()) {
        if (!read_next_line()) {
          return make_read_error(kReadEofMessage);
        }
        if (pending_tokens.empty()) {
          continue;
        }
      }

      try {
        auto [expression, consumed_token_count] = parse_expression();
        consume_expression(consumed_token_count);
        return Evaluator::ConvertSyntaxToValue(expression.get());
      } catch (const IncompleteInputError&) {
        if (!read_next_line()) {
          return make_read_error(kReadEofMessage);
        }
      } catch (const ParseError& error) {
        const std::string message = FormatParseError(error);
        discard_failed_input();
        return make_read_error(message);
      }
    }
  };

  Evaluator evaluator(read_value);

  while (true) {
    std::print("> ");

    NodePtr expression;
    std::size_t consumed_token_count = 0;

    while (true) {
      if (pending_tokens.empty()) {
        if (!read_next_line()) {
          std::print("{}", kEofExitMessage);
          return 0;
        }
        if (pending_tokens.empty()) {
          continue;
        }
      }

      try {
        auto [exp, consumed_amount] = parse_expression();
        expression = std::move(exp);
        consumed_token_count = consumed_amount;
        break;
      } catch (const IncompleteInputError&) {
        if (!read_next_line()) {
          std::print("{}", kEofExitMessage);
          return 0;
        }
      } catch (const ParseError& error) {
        PrintParseError(error);
        std::println();
        discard_failed_input();
        break;
      }
    }

    if (!expression) {
      continue;
    }

    consume_expression(consumed_token_count);

    if (IsExitExpression(expression.get())) {
      std::println();
      std::print("Thanks for using OurScheme!");
      return 0;
    }

    try {
      if (const Value result = evaluator.EvaluateTopLevel(expression.get());
          ShouldPrint(result)) {
        PrintValue(result);
      }
    } catch (const EvalError& error) {
      PrintEvalError(error);
    }
    std::println();
  }
}
