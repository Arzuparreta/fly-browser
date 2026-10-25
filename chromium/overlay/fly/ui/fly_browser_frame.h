// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_BROWSER_FRAME_H_
#define FLY_UI_FLY_BROWSER_FRAME_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"

class BrowserView;
class BrowserWidget;
namespace views {
class Widget;
}

namespace fly {
class FlyWindowController;

// The frame of a Fly browser window. The window manager draws the title bar;
// this frame has no caption of its own. It reserves the sidebar slot on the
// left and the footer below the page, and lays out the BrowserView (the
// client view) in the rest. The sidebar host and the footer are children of
// this frame, installed by the window's FlyWindowController.
class FlyBrowserFrame : public BrowserFrameView {
  METADATA_HEADER(FlyBrowserFrame, BrowserFrameView)

 public:
  FlyBrowserFrame(BrowserWidget* widget, BrowserView* view);
  ~FlyBrowserFrame() override;

  static FlyBrowserFrame* FromWidget(views::Widget* widget);
  void SetController(base::WeakPtr<FlyWindowController> controller);

  // BrowserFrameView:
  int GetTopInset(bool restored) const override;
  void UpdateThrobber(bool running) override {}
  bool CaptionButtonsOnLeadingEdge() const override;
  bool CaptionButtonsOnTrailingEdge() const override;
  BrowserLayoutParams GetBrowserLayoutParams() const override;
  void OnFullscreenStateChanged() override;
  gfx::Rect GetBoundsForClientView() const override;
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& bounds) const override;
  int NonClientHitTest(const gfx::Point& point) override;
  gfx::Size GetMinimumSize() const override;
  void Layout(PassKey) override;
  void ChildPreferredSizeChanged(views::View* child) override;
  void ChildVisibilityChanged(views::View* child) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void AddedToWidget() override;

 private:
  base::WeakPtr<FlyWindowController> controller_;
};

// Creates the frame for a decorated browser window.
std::unique_ptr<BrowserFrameView> CreateFlyBrowserFrame(BrowserWidget* widget,
                                                        BrowserView* view);
}  // namespace fly

#endif  // FLY_UI_FLY_BROWSER_FRAME_H_
