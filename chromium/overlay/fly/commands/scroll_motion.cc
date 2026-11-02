// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
// Algorithm adapted from Vimium, Copyright (c) 2010 Phil Crosby, Ilya Sukhar.
// See fly/resources/licenses/vimium-MIT.txt for the MIT license.
#include "fly/commands/scroll_motion.h"

#include <algorithm>
#include <cmath>

namespace fly {
namespace {
// Vimium adjusts its calibration once per animation frame at 60 Hz.
constexpr double kVimiumFrame = 1.0 / 60;
// Below this, a released motion has covered what it owes.
constexpr double kEpsilon = 1e-3;
}  // namespace

ScrollMotion::ScrollMotion(double amount)
    : amount_(std::max(1.0, amount)),
      duration_(std::max(0.1, 0.02 * std::log(amount_))),
      owed_(amount_) {}

void ScrollMotion::Extend() {
  owed_ = std::max(owed_, moved_) + amount_;
}

bool ScrollMotion::Finished() const {
  return moved_ >= owed_ - kEpsilon;
}

double ScrollMotion::Advance(double elapsed, double held) {
  if (elapsed <= 0)
    return 0;
  const bool down = held >= 0;
  if (!down && Finished())
    return 0;
  // Vimium speeds up short steps and slows down long ones while a key is
  // held, so a held key scrolls about kCalibrationBoundary pixels per step.
  if (down && held >= kCalibrationDelay) {
    const double frames = elapsed / kVimiumFrame;
    if (1.05 * calibration_ * amount_ < kCalibrationBoundary)
      calibration_ =
          std::min(kMaxCalibration, calibration_ * std::pow(1.05, frames));
    else if (kCalibrationBoundary < 0.95 * calibration_ * amount_)
      calibration_ =
          std::max(kMinCalibration, calibration_ * std::pow(0.95, frames));
  }
  double speed = amount_ / duration_;
  if (down) {
    speed *= calibration_;
  } else {
    // Each queued press is one of Vimium's concurrent animations.
    speed *= std::max(1.0, (owed_ - moved_) / amount_);
  }
  double delta = speed * elapsed;
  if (!down)
    delta = std::min(delta, owed_ - moved_);
  delta = std::max(0.0, delta);
  moved_ += delta;
  return delta;
}

}  // namespace fly
