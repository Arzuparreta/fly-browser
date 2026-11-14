// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.

#ifndef FLY_UI_NAVIGATION_DISPOSITION_H_
#define FLY_UI_NAVIGATION_DISPOSITION_H_

#include "ui/base/window_open_disposition.h"

namespace fly {

// Chromium moved this enum to Mojo and renamed its members. Select the
// available semantic name at compile time, without version numbers or casts
// from integers, so the same sources and browser tests work on both pins.
template <typename Disposition = WindowOpenDisposition>
constexpr Disposition CurrentTabDisposition() {
  if constexpr (requires { Disposition::kCurrentTab; }) {
    return Disposition::kCurrentTab;
  } else {
    return Disposition::CURRENT_TAB;
  }
}

template <typename Disposition = WindowOpenDisposition>
constexpr Disposition NewForegroundTabDisposition() {
  if constexpr (requires { Disposition::kNewForegroundTab; }) {
    return Disposition::kNewForegroundTab;
  } else {
    return Disposition::NEW_FOREGROUND_TAB;
  }
}

template <typename Disposition = WindowOpenDisposition>
constexpr Disposition NewBackgroundTabDisposition() {
  if constexpr (requires { Disposition::kNewBackgroundTab; }) {
    return Disposition::kNewBackgroundTab;
  } else {
    return Disposition::NEW_BACKGROUND_TAB;
  }
}

template <typename Disposition = WindowOpenDisposition>
constexpr Disposition NewWindowDisposition() {
  if constexpr (requires { Disposition::kNewWindow; }) {
    return Disposition::kNewWindow;
  } else {
    return Disposition::NEW_WINDOW;
  }
}

}  // namespace fly

#endif  // FLY_UI_NAVIGATION_DISPOSITION_H_
