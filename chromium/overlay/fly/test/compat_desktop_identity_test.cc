// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.

// Browser-level regression for Fly's installed desktop identity. It runs in
// fly_browser_tests on the fully patched engine (patches 0041 and 0045), where
// both chrome::GetDesktopName and version_info::nix::GetDesktopName exist on
// every supported Chromium pin. Newer Chromium moved the implementation into
// //base and calls it directly from the camera portal and the Linux media
// controls, so both entry points must agree.

#include "build/build_config.h"

#include "build/branding_buildflags.h"

#if BUILDFLAG(IS_LINUX) && !BUILDFLAG(GOOGLE_CHROME_BRANDING)
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/environment.h"
#include "base/strings/cstring_view.h"
#include "base/version_info/nix/version_extra_utils.h"
#include "chrome/common/channel_info.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {

constexpr char kFlyDesktopName[] = "fly-browser.desktop";

// An in-memory environment, so that the running browser's process
// environment is never mutated while its threads may read it.
class FakeEnvironment : public base::Environment {
 public:
  void Set(const std::string& name, const std::string& value) {
    values_[name] = value;
  }
  void Unset(const std::string& name) { values_.erase(name); }

  std::optional<std::string> GetVar(base::cstring_view name) override {
    auto it = values_.find(std::string(name.c_str()));
    if (it == values_.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  bool SetVar(base::cstring_view name, const std::string& value) override {
    Set(std::string(name.c_str()), value);
    return true;
  }
  bool UnSetVar(base::cstring_view name) override {
    Unset(std::string(name.c_str()));
    return true;
  }

 private:
  std::map<std::string, std::string> values_;
};

using FlyDesktopIdentityBrowserTest = InProcessBrowserTest;

// Electron/Chromium parents (for example T3) export their own CHROME_DESKTOP,
// and CHROME_VERSION_EXTRA/SNAP_NAME select channel-specific names upstream.
// None of them may rename Fly's Wayland app ID, X11 identity, default-browser
// registration, notification/download integration, camera portal or media
// controls.
IN_PROC_BROWSER_TEST_F(FlyDesktopIdentityBrowserTest,
                       InheritedVariablesKeepInstalledIdentity) {
  for (const char* channel :
       {"", "stable", "extended", "beta", "unstable", "canary", "other"}) {
    for (const std::optional<std::string>& desktop :
         {std::optional<std::string>(), std::optional<std::string>(""),
          std::optional<std::string>("chromium-browser.desktop"),
          std::optional<std::string>("t3-code.desktop")}) {
      FakeEnvironment env;
      env.Set(version_info::nix::kChromeVersionExtra, channel);
      env.Set("SNAP_NAME", "google-chrome");
      if (desktop) {
        env.Set("CHROME_DESKTOP", *desktop);
      }
      SCOPED_TRACE(testing::Message()
                   << "CHROME_VERSION_EXTRA=" << channel << " CHROME_DESKTOP="
                   << desktop.value_or("<unset>"));
      EXPECT_EQ(kFlyDesktopName, chrome::GetDesktopName(&env));
      EXPECT_EQ(kFlyDesktopName, version_info::nix::GetDesktopName(env));
    }
  }
}

// The browser under test resolves the same identity from its real process
// environment through both the //chrome and the //base entry points.
IN_PROC_BROWSER_TEST_F(FlyDesktopIdentityBrowserTest,
                       RunningBrowserUsesInstalledIdentity) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  EXPECT_EQ(kFlyDesktopName, chrome::GetDesktopName(env.get()));
  EXPECT_EQ(kFlyDesktopName, version_info::nix::GetDesktopName(*env));
}

}  // namespace
}  // namespace fly
#endif  // BUILDFLAG(IS_LINUX) && !BUILDFLAG(GOOGLE_CHROME_BRANDING)
