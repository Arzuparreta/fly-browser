// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.

#include "build/branding_buildflags.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_LINUX) && !BUILDFLAG(GOOGLE_CHROME_BRANDING)
#include "base/environment.h"
#include "base/scoped_environment_variable_override.h"
#include "base/version_info/nix/version_extra_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {

TEST(FlyDesktopIdentityCompatTest, SharedIdentityIgnoresParentAndChannel) {
  auto env = base::Environment::Create();
  const base::ScopedEnvironmentVariableOverride snap("SNAP_NAME", "google-chrome");
  for (const char* channel : {"", "stable", "extended", "beta", "unstable",
                              "canary"}) {
    const base::ScopedEnvironmentVariableOverride channel_override(
        "CHROME_VERSION_EXTRA", channel);
    {
      const base::ScopedEnvironmentVariableOverride absent("CHROME_DESKTOP");
      EXPECT_EQ("fly-browser.desktop", version_info::nix::GetDesktopName(*env));
    }
    for (const char* desktop : {"", "chromium-browser.desktop",
                                "parent-application.desktop"}) {
      const base::ScopedEnvironmentVariableOverride desktop_override(
          "CHROME_DESKTOP", desktop);
      EXPECT_EQ("fly-browser.desktop", version_info::nix::GetDesktopName(*env));
    }
  }
}

}  // namespace
}  // namespace fly
#endif  // BUILDFLAG(IS_LINUX) && !BUILDFLAG(GOOGLE_CHROME_BRANDING)
