// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.

#ifndef FLY_UI_SIDEBAR_NATIVE_HOST_H_
#define FLY_UI_SIDEBAR_NATIVE_HOST_H_

namespace fly {

// Older Chromium can keep a sidebar in a native clipping window independent
// of Views' frame/client layer order. Newer Chromium limits that API to
// ChromeOS: its Views-managed host must instead be above the client view in
// the Views hierarchy. The caller owns that placement and hit testing.
template <typename Host>
bool ConfigureSidebarClipWindow(Host* host) {
  if constexpr (requires { host->SetLayerManagedByViews(false); }) {
    host->SetLayerManagedByViews(false);
    return true;
  } else {
    return false;
  }
}

}  // namespace fly

#endif  // FLY_UI_SIDEBAR_NATIVE_HOST_H_
