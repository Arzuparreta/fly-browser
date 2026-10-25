// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_browser_frame.h"

#include <algorithm>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_window_controller.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace fly {

FlyBrowserFrame::FlyBrowserFrame(BrowserWidget* widget, BrowserView* view)
    : BrowserFrameView(widget, view) {}

FlyBrowserFrame::~FlyBrowserFrame() {
  if (controller_)
    controller_->FrameDestroying(this);
}

// static
FlyBrowserFrame* FlyBrowserFrame::FromWidget(views::Widget* widget) {
  auto* client = widget ? widget->non_client_view() : nullptr;
  auto* frame = client ? client->frame_view() : nullptr;
  return frame ? views::AsViewClass<FlyBrowserFrame>(frame) : nullptr;
}

void FlyBrowserFrame::AddedToWidget() {
  BrowserFrameView::AddedToWidget();
  // A recreated frame takes the Fly interface over from its predecessor. The
  // first frame exists before the window's controller does.
  auto* view = GetBrowserView();
  if (view && view->browser()) {
    if (auto* controller = FlyWindowController::FromBrowser(view->browser()))
      controller->InstallInto(this);
  }
}

void FlyBrowserFrame::SetController(
    base::WeakPtr<FlyWindowController> controller) {
  controller_ = std::move(controller);
  InvalidateLayout();
}

int FlyBrowserFrame::GetTopInset(bool restored) const {
  return 0;
}

bool FlyBrowserFrame::CaptionButtonsOnLeadingEdge() const {
  return false;
}

bool FlyBrowserFrame::CaptionButtonsOnTrailingEdge() const {
  return false;
}

BrowserLayoutParams FlyBrowserFrame::GetBrowserLayoutParams() const {
  // The BrowserView fills its client bounds. Between a change of the reserved
  // areas and the next frame layout it still has its previous bounds, and its
  // layout must stay inside them.
  BrowserLayoutParams params;
  auto* view = GetBrowserView();
  params.visual_client_area =
      view && !view->bounds().IsEmpty() ? view->bounds()
                                        : GetBoundsForClientView();
  return params;
}

void FlyBrowserFrame::OnFullscreenStateChanged() {
  BrowserFrameView::OnFullscreenStateChanged();
  if (controller_)
    controller_->UpdateFullscreenChrome();
  InvalidateLayout();
}

gfx::Rect FlyBrowserFrame::GetBoundsForClientView() const {
  auto bounds = GetLocalBounds();
  if (!controller_)
    return bounds;
  const int slot = std::min(controller_->SidebarSlotWidth(), bounds.width());
  const int footer = std::min(controller_->FooterHeight(), bounds.height());
  bounds.Inset(gfx::Insets::TLBR(0, slot, footer, 0));
  return bounds;
}

gfx::Rect FlyBrowserFrame::GetWindowBoundsForClientBounds(
    const gfx::Rect& bounds) const {
  if (!controller_)
    return bounds;
  auto window = bounds;
  window.Inset(gfx::Insets::TLBR(0, -controller_->SidebarSlotWidth(),
                                 -controller_->FooterHeight(), 0));
  return window;
}

int FlyBrowserFrame::NonClientHitTest(const gfx::Point& point) {
  // The window manager owns moving and resizing.
  return HitTestPoint(point) ? HTCLIENT : HTNOWHERE;
}

gfx::Size FlyBrowserFrame::GetMinimumSize() const {
  auto* view = GetBrowserView();
  gfx::Size size = view ? view->GetMinimumSize() : gfx::Size();
  if (controller_) {
    size.Enlarge(controller_->SidebarSlotWidth(), controller_->FooterHeight());
  }
  return size;
}

void FlyBrowserFrame::Layout(PassKey) {
  LayoutSuperclass<BrowserFrameView>(this);
  if (controller_)
    controller_->LayoutFrameChrome();
}

void FlyBrowserFrame::ChildPreferredSizeChanged(views::View* child) {
  // The footer grows with the download shelf and the status bar.
  InvalidateLayout();
}

void FlyBrowserFrame::ChildVisibilityChanged(views::View* child) {
  InvalidateLayout();
}

void FlyBrowserFrame::OnPaint(gfx::Canvas* canvas) {
  // Behind the sidebar slot and the footer before they paint.
  canvas->DrawColor(GetFlyColors().bg_main);
}

std::unique_ptr<BrowserFrameView> CreateFlyBrowserFrame(BrowserWidget* widget,
                                                        BrowserView* view) {
  return std::make_unique<FlyBrowserFrame>(widget, view);
}

BEGIN_METADATA(FlyBrowserFrame)
END_METADATA
}  // namespace fly
