// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_NEW_TAB_UI_H_
#define FLY_UI_NEW_TAB_UI_H_

#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace fly {
// Fly's new tab page (chrome://fly-new-tab/): clock, date, bookmarks,
// frequent sites and the live memory use of Fly, served from
// <out>/fly_resources/new_tab.
inline constexpr char kFlyNewTabHost[] = "fly-new-tab";
inline constexpr char kFlyNewTabURL[] = "chrome://fly-new-tab/";

// New tabs and blank pages are both "new tab" in Fly's interface.
bool IsFlyNewTabPage(const GURL& url);
// BrowserURLHandler rewrite: chrome://newtab (Chromium's new tab page, also
// the start page) becomes Fly's page in every profile.
bool RewriteNewTabURL(GURL* url, content::BrowserContext* browser_context);
// Adds the page to Chromium's WebUI registry; safe to call more than once.
void RegisterFlyNewTabPage();
}  // namespace fly

#endif  // FLY_UI_NEW_TAB_UI_H_
