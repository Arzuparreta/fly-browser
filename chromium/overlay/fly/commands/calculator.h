// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_CALCULATOR_H_
#define FLY_COMMANDS_CALCULATOR_H_
#include <optional>
#include <string_view>
namespace fly {
// Bounded arithmetic parser for address input. No JavaScript execution or page
// context; invalid input and non-finite results remain ordinary search input.
std::optional<double> EvaluateAddressExpression(std::string_view expression);
}  // namespace fly
#endif
