// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/calculator.h"

#include <string>
#include "testing/gtest/include/gtest/gtest.h"
namespace fly {
namespace {
TEST(AddressCalculatorTest, RetainsArithmeticAndHumanInputContracts) {
  for (const auto& [input, answer] : {std::pair{"2+3*4", 14.0},
                                      {"(2+3)*4", 20.0},
                                      {"2^3^2", 512.0},
                                      {"2**3", 8.0},
                                      {"50% of 200", 100.0},
                                      {"6 × 7", 42.0},
                                      {"12 ÷ 3", 4.0},
                                      {"2 x 3", 6.0},
                                      {"2x3", 6.0},
                                      {"1,234 + 1", 1235.0},
                                      {"0.1+0.2", 0.3},
                                      {"-2^2", 4.0},
                                      {"sqrt(81)", 9.0},
                                      {"log2(8)", 3.0},
                                      {"round(-1.5)", -1.0},
                                      {"cos(0)", 1.0}}) {
    const auto result = EvaluateAddressExpression(input);
    ASSERT_TRUE(result) << input;
    EXPECT_DOUBLE_EQ(answer, *result) << input;
  }
}
TEST(AddressCalculatorTest, RejectsSearchNavigationInvalidAndNonFiniteInput) {
  for (const auto* input : {"", "https://example.com", "ws work", "!g 2+2",
                            "Math.random()", "unknown(1)", "1/0", "sqrt(-1)",
                            "1..2", "2+", "(2+3", "2 3", "10^10000"})
    EXPECT_FALSE(EvaluateAddressExpression(input)) << input;
}
TEST(AddressCalculatorTest, BoundsHostileDepthAndInputWithoutExecutingCode) {
  EXPECT_FALSE(EvaluateAddressExpression(std::string(5000, '1')));
  EXPECT_FALSE(EvaluateAddressExpression(std::string(1000, '-') + "1"));
  EXPECT_FALSE(EvaluateAddressExpression(std::string(1000, '(') + "1" +
                                         std::string(1000, ')')));
  EXPECT_FALSE(EvaluateAddressExpression("1;fetch('https://example.com')"));
}
}  // namespace
}  // namespace fly
