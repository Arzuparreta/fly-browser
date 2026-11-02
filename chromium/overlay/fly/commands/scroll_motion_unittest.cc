// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/scroll_motion.h"

#include <cmath>

#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
constexpr double kFrame = 1.0 / 144;

// Runs frames until the motion stops, releasing the key after `hold` seconds.
// Returns the time it took.
double RunMotion(ScrollMotion& motion, double hold) {
  double time = 0;
  for (int frame = 0; frame < 100000; ++frame) {
    time += kFrame;
    const double held = time <= hold ? time : -1;
    if (held < 0 && motion.Finished())
      return time - kFrame;
    motion.Advance(kFrame, held);
  }
  ADD_FAILURE() << "The motion never stopped";
  return time;
}
}  // namespace

TEST(ScrollMotionTest, ATapMovesOneStepAtVimiumSpeed) {
  ScrollMotion motion(60);
  // max(100 ms, 20 ms * ln 60 = 82 ms).
  EXPECT_DOUBLE_EQ(0.1, motion.duration());
  // The first frame already moves: no ease-in.
  EXPECT_NEAR(600 * kFrame, motion.Advance(kFrame, kFrame), 1e-9);
  const double time = RunMotion(motion, 0.01);
  EXPECT_DOUBLE_EQ(60, motion.moved());
  EXPECT_NEAR(0.1, time, 2 * kFrame);
}

TEST(ScrollMotionTest, AHeldKeyKeepsMovingAndSpeedsUpShortSteps) {
  ScrollMotion motion(60);
  RunMotion(motion, 1.0);
  EXPECT_DOUBLE_EQ(ScrollMotion::kMaxCalibration, motion.calibration());
  // 75 ms at 600 px/s, then a ramp of about 160 ms up to 960 px/s.
  EXPECT_GT(motion.moved(), 800);
  EXPECT_LT(motion.moved(), 960);
  // The release stops at once: the step was covered long ago.
  const double moved = motion.moved();
  EXPECT_EQ(0, motion.Advance(kFrame, -1));
  EXPECT_DOUBLE_EQ(moved, motion.moved());
}

TEST(ScrollMotionTest, SpeedDoesNotDependOnTheFrameRate) {
  ScrollMotion fast(60);
  ScrollMotion slow(60);
  for (int i = 0; i < 144; ++i)
    fast.Advance(1.0 / 144, (i + 1) / 144.0);
  for (int i = 0; i < 60; ++i)
    slow.Advance(1.0 / 60, (i + 1) / 60.0);
  EXPECT_NEAR(fast.moved(), slow.moved(), 15);
}

TEST(ScrollMotionTest, LongStepsSlowDownWhileHeld) {
  ScrollMotion motion(540);
  EXPECT_NEAR(0.02 * std::log(540), motion.duration(), 1e-12);
  RunMotion(motion, 1.0);
  EXPECT_DOUBLE_EQ(ScrollMotion::kMinCalibration, motion.calibration());
}

TEST(ScrollMotionTest, RepeatedTapsAddUpAndCatchUp) {
  ScrollMotion motion(60);
  motion.Advance(kFrame, kFrame);
  motion.Extend();
  motion.Extend();
  EXPECT_DOUBLE_EQ(180, motion.owed());
  const double time = RunMotion(motion, 0);
  EXPECT_DOUBLE_EQ(180, motion.moved());
  // Three overlapping steps take less than three steps one after another.
  EXPECT_LT(time, 0.3);
}

TEST(ScrollMotionTest, APressDuringAHeldMotionOwesOneMoreStep) {
  ScrollMotion motion(60);
  RunMotion(motion, 0.5);
  const double moved = motion.moved();
  motion.Extend();
  EXPECT_DOUBLE_EQ(moved + 60, motion.owed());
  RunMotion(motion, 0);
  EXPECT_DOUBLE_EQ(moved + 60, motion.moved());
}

TEST(ScrollMotionTest, NothingMovesWithoutTime) {
  ScrollMotion motion(60);
  EXPECT_EQ(0, motion.Advance(0, 0));
  EXPECT_EQ(0, motion.Advance(-1, 0));
  EXPECT_EQ(0, motion.moved());
}

}  // namespace fly
