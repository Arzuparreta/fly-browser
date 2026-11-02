// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_SCROLL_MOTION_H_
#define FLY_COMMANDS_SCROLL_MOTION_H_

namespace fly {

// The motion of one keyboard scroll, following Vimium's CoreScroller
// (content_scripts/scroller.js, MIT licence):
//  - a press moves `amount` pixels at a steady speed, `amount` in
//    max(100 ms, 20 ms * ln(amount));
//  - while the key stays down the motion continues, and after 75 ms its speed
//    eases towards 150 pixels per step, within 0.5 to 1.6 times the starting
//    speed (Vimium adjusts by 5% per 60 Hz frame; this adjusts by time, so
//    the speed does not depend on the display's refresh rate);
//  - a release stops the motion once it has covered `amount`;
//  - presses during a motion add one `amount` each, and a backlog of several
//    steps moves proportionally faster, as Vimium's overlapping animations do.
// All distances are in pixels and all times in seconds.
class ScrollMotion {
 public:
  static constexpr double kCalibrationDelay = 0.075;
  static constexpr double kCalibrationBoundary = 150;
  static constexpr double kMinCalibration = 0.5;
  static constexpr double kMaxCalibration = 1.6;

  explicit ScrollMotion(double amount);

  // The distance to move during a frame of `elapsed` seconds that ends `held`
  // seconds after the key was pressed, or 0 once the motion is over. `held` is
  // negative once the key has been released.
  double Advance(double elapsed, double held);
  // Another press of the same scroll.
  void Extend();
  // True once a released motion has covered what it owes.
  bool Finished() const;

  double amount() const { return amount_; }
  double duration() const { return duration_; }
  double moved() const { return moved_; }
  double owed() const { return owed_; }
  double calibration() const { return calibration_; }

 private:
  double amount_;
  double duration_;
  double calibration_ = 1;
  double moved_ = 0;
  double owed_;
};

}  // namespace fly

#endif  // FLY_COMMANDS_SCROLL_MOTION_H_
