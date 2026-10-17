// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_EMBEDDED_BROWSER_FRAME_H_
#define FLY_UI_EMBEDDED_BROWSER_FRAME_H_

#include <memory>

class BrowserFrameView;
class BrowserNativeWidget;
class BrowserWidget;
class BrowserView;
namespace views {
class FrameView;
}
namespace fly {
BrowserNativeWidget* CreateEmbeddedBrowserNativeWidget(BrowserWidget* widget,
                                                       BrowserView* view);
std::unique_ptr<BrowserFrameView> CreateEmbeddedBrowserFrame(
    BrowserWidget* widget,
    BrowserView* view);
}  // namespace fly
#endif  // FLY_UI_EMBEDDED_BROWSER_FRAME_H_
