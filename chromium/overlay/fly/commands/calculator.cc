// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/calculator.h"

#include <cmath>
#include <numbers>
#include <string>
#include <vector>
#include "base/memory/raw_ref.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace fly {
namespace {
struct Token {
  enum Kind { kNumber, kIdentifier, kOperator } kind;
  std::string text;
  double number = 0;
};
std::optional<std::vector<Token>> Tokenize(std::string_view expression) {
  if (expression.empty() || expression.size() > 4096)
    return std::nullopt;
  std::string input = base::ToLowerASCII(expression);
  base::ReplaceSubstringsAfterOffset(&input, 0, "×", "*");
  base::ReplaceSubstringsAfterOffset(&input, 0, "÷", "/");
  base::ReplaceSubstringsAfterOffset(&input, 0, "**", "^");
  std::vector<Token> result;
  for (size_t i = 0; i < input.size();) {
    const auto c = input[i];
    if (base::IsAsciiWhitespace(c)) {
      ++i;
      continue;
    }
    if (base::IsAsciiDigit(c) || c == '.') {
      const size_t start = i;
      std::string number;
      while (i < input.size()) {
        const auto next = input[i];
        if (base::IsAsciiDigit(next) || next == '.') {
          number += next;
          ++i;
        } else if (next == ',' && i > start && i + 1 < input.size() &&
                   base::IsAsciiDigit(input[i - 1]) &&
                   base::IsAsciiDigit(input[i + 1])) {
          ++i;
        } else {
          break;
        }
      }
      double value;
      if (!base::StringToDouble(number, &value) || !std::isfinite(value))
        return std::nullopt;
      result.push_back({Token::kNumber, number, value});
      if (i < input.size() && input[i] == '%') {
        result.back().number /= 100;
        ++i;
      }
    } else if (c == 'x' && !result.empty() &&
               result.back().kind == Token::kNumber && i + 1 < input.size() &&
               base::IsAsciiDigit(input[i + 1])) {
      result.push_back({Token::kOperator, "*"});
      ++i;
    } else if (base::IsAsciiAlpha(c)) {
      const size_t start = i++;
      while (i < input.size() && base::IsAsciiAlphaNumeric(input[i]))
        ++i;
      const auto name = input.substr(start, i - start);
      const bool multiply = name == "of" || name == "x";
      result.push_back({multiply ? Token::kOperator : Token::kIdentifier,
                        multiply ? "*" : name});
    } else if (std::string_view("+-*/%^()").find(c) != std::string_view::npos) {
      result.push_back({Token::kOperator, std::string(1, c)});
      ++i;
    } else {
      return std::nullopt;
    }
    if (result.size() > 1024)
      return std::nullopt;
  }
  return result;
}
class Parser {
 public:
  explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}
  std::optional<double> Run() {
    const double result = Expression(0);
    if (!valid_ || position_ != tokens_->size() || !std::isfinite(result))
      return std::nullopt;
    // Match the retained calculator's ten-decimal precision cleanup without
    // overflowing on otherwise valid large finite answers.
    return std::abs(result) < 1e20 ? std::round(result * 1e10) / 1e10 : result;
  }

 private:
  bool Is(std::string_view text) const {
    return position_ < tokens_->size() && (*tokens_)[position_].text == text;
  }
  double Expression(int depth) {
    double value = Term(depth);
    while (valid_ && (Is("+") || Is("-"))) {
      const bool add = Is("+");
      ++position_;
      const auto right = Term(depth);
      value = add ? value + right : value - right;
    }
    return value;
  }
  double Term(int depth) {
    double value = Power(depth);
    while (valid_ && (Is("*") || Is("/") || Is("%"))) {
      const auto op = (*tokens_)[position_++].text;
      const auto right = Power(depth);
      if (op != "*" && right == 0) {
        valid_ = false;
        return 0;
      }
      value = op == "*"   ? value * right
              : op == "/" ? value / right
                          : std::fmod(value, right);
    }
    return value;
  }
  double Power(int depth) {
    double value = Factor(depth + 1);
    if (valid_ && Is("^")) {
      ++position_;
      value = std::pow(value, Power(depth + 1));
    }
    return value;
  }
  double Factor(int depth) {
    if (depth > 64 || position_ >= tokens_->size()) {
      valid_ = false;
      return 0;
    }
    if (Is("-") || Is("+")) {
      const bool negative = Is("-");
      ++position_;
      const auto value = Factor(depth + 1);
      return negative ? -value : value;
    }
    const auto token = (*tokens_)[position_++];
    if (token.kind == Token::kNumber)
      return token.number;
    if (token.kind == Token::kIdentifier) {
      if (token.text == "pi")
        return std::numbers::pi;
      if (token.text == "e")
        return std::numbers::e;
    }
    if (token.text == "(" || (token.kind == Token::kIdentifier && Is("("))) {
      if (token.text != "(")
        ++position_;
      const auto argument = Expression(depth + 1);
      if (!Is(")")) {
        valid_ = false;
        return 0;
      }
      ++position_;
      const auto& name = token.text;
      if (name == "(")
        return argument;
      if (name == "sqrt")
        return std::sqrt(argument);
      if (name == "cbrt")
        return std::cbrt(argument);
      if (name == "abs")
        return std::abs(argument);
      if (name == "sin")
        return std::sin(argument);
      if (name == "cos")
        return std::cos(argument);
      if (name == "tan")
        return std::tan(argument);
      if (name == "round")
        return std::floor(argument + 0.5);
      if (name == "floor")
        return std::floor(argument);
      if (name == "ceil")
        return std::ceil(argument);
      if (name == "log")
        return std::log(argument);
      if (name == "log2")
        return std::log2(argument);
      if (name == "log10")
        return std::log10(argument);
    }
    valid_ = false;
    return 0;
  }
  const raw_ref<const std::vector<Token>> tokens_;
  size_t position_ = 0;
  bool valid_ = true;
};
}  // namespace
std::optional<double> EvaluateAddressExpression(std::string_view expression) {
  const auto tokens = Tokenize(expression);
  if (!tokens || tokens->empty())
    return std::nullopt;
  return Parser(*tokens).Run();
}
}  // namespace fly
