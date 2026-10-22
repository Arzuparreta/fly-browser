// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/embedded_browser_frame.h"

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_native_widget.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "fly/ui/fly_window_controller.h"
#include "ui/base/hit_test.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/views/widget/native_widget_aura.h"

namespace fly {
namespace {
// This is a native Aura window in the Fly desktop's tree. In contrast to
// DesktopNativeWidgetAura it never creates a second HWND/X11/Wayland window.
// Keep child=false: each BrowserView needs its own FocusManager and IME state;
// sharing those would register inactive profiles' accelerators on one manager.
class EmbeddedBrowserNativeWidget final : public views::NativeWidgetAura,
                                          public BrowserNativeWidget {
 public:
  EmbeddedBrowserNativeWidget(BrowserWidget* widget, BrowserView* view)
      : views::NativeWidgetAura(widget), browser_(view->browser()->GetWeakPtr()) {}
  views::Widget::InitParams GetWidgetParams(
      views::Widget::InitParams::Ownership ownership) override {
    views::Widget::InitParams params(ownership);
    params.native_widget = this;
    params.parent = Desktop()->GetNativeView();
    params.child = false;
    params.remove_standard_frame = true;
    params.bounds = gfx::Rect(0, 0, 1020, 760);
    return params;
  }
  bool UseCustomFrame() const override { return true; }
  bool UsesNativeSystemMenu() const override { return true; }
  bool ShouldSaveWindowPlacement() const override { return false; }
  bool ShouldRestorePreviousBrowserWidgetState() const override {
    return false;
  }
  bool ShouldUseInitialVisibleOnAllWorkspaces() const override { return false; }
  void GetWindowPlacement(gfx::Rect* bounds,
                          ui::mojom::WindowShowState* state) const override {
    *bounds = Desktop()->GetRestoredBounds();
    *state = Desktop()->IsMaximized()   ? ui::mojom::WindowShowState::kMaximized
             : Desktop()->IsMinimized() ? ui::mojom::WindowShowState::kMinimized
                                        : ui::mojom::WindowShowState::kNormal;
  }
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  bool HandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override {
    return false;
  }
  void Show(ui::mojom::WindowShowState state,
            const gfx::Rect& restore_bounds) override {
    if (!browser_ || browser_->IsDeleteScheduled())
      return;
    auto& window = FlyWindowController::Get();
    // A background native window is still a separate browser/tab model. It
    // must not replace the currently visible workspace merely because Chrome
    // requests ShowInactive (for example chrome.windows.create focused:false).
    if (state == ui::mojom::WindowShowState::kInactive &&
        window.has_selected_browser() && !window.IsSelected(browser_.get())) {
      views::NativeWidgetAura::Hide();
      return;
    }
    views::NativeWidgetAura::Show(state, restore_bounds);
    if (browser_)
      window.Select(browser_.get(), false);
  }
  void Activate() override {
    if (auto* window = FlyWindowController::GetIfCreated(); window && browser_) {
      // FocusManager can request activation while it is focusing the native
      // location bar. Restoring page focus here re-enters that same operation
      // before the native control receives focus (notably under Openbox).
      window->Select(browser_.get(), false);
      window->desktop_widget()->Activate();
    }
  }
  void Deactivate() override { Desktop()->Deactivate(); }
  bool IsActive() const override {
    const auto* window = FlyWindowController::GetIfCreated();
    return window && browser_ && window->IsSelected(browser_.get()) &&
           window->desktop_widget()->IsActive();
  }
  void Maximize() override { Desktop()->Maximize(); }
  void Minimize() override { Desktop()->Minimize(); }
  bool IsMaximized() const override { return Desktop()->IsMaximized(); }
  bool IsMinimized() const override { return Desktop()->IsMinimized(); }
  void Restore() override { Desktop()->Restore(); }
  void SetFullscreen(bool fullscreen, int64_t display) override {
    Desktop()->SetFullscreen(fullscreen, display);
    FlyWindowController::Get().UpdateFullscreenChrome();
  }
  bool IsFullscreen() const override { return Desktop()->IsFullscreen(); }
  gfx::Rect GetRestoredBounds() const override {
    return Desktop()->GetRestoredBounds();
  }
  void FlashFrame(bool flash) override { Desktop()->FlashFrame(flash); }

 private:
  views::Widget* Desktop() const {
    return FlyWindowController::Get().desktop_widget();
  }
  // Chromium deletes the browser model before the posted Aura window teardown.
  // The widget must neither retain a dangling raw_ptr nor reactivate that model.
  const base::WeakPtr<BrowserWindowInterface> browser_;
};

// Platform BrowserFrameViews assume a DesktopNativeWidgetAuraLinux/Win. The
// embedded browser has no desktop titlebar: Fly owns it exactly once. Retain a
// real BrowserFrameView so BrowserView's native layout/theme paths stay intact.
class EmbeddedBrowserFrame final : public BrowserFrameView {
 public:
  EmbeddedBrowserFrame(BrowserWidget* widget, BrowserView* view)
      : BrowserFrameView(widget, view) {}
  int GetTopInset(bool restored) const override { return 0; }
  void UpdateThrobber(bool running) override {}
  bool CaptionButtonsOnLeadingEdge() const override { return false; }
  bool CaptionButtonsOnTrailingEdge() const override { return false; }
  BrowserLayoutParams GetBrowserLayoutParams() const override {
    BrowserLayoutParams params;
    params.visual_client_area = GetLocalBounds();
    return params;
  }
  gfx::Rect GetBoundsForClientView() const override { return GetLocalBounds(); }
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& bounds) const override {
    return bounds;
  }
  int NonClientHitTest(const gfx::Point& point) override {
    return HitTestPoint(point) ? HTCLIENT : HTNOWHERE;
  }
};
}  // namespace

BrowserNativeWidget* CreateEmbeddedBrowserNativeWidget(BrowserWidget* widget,
                                                       BrowserView* view) {
  return new EmbeddedBrowserNativeWidget(widget, view);
}
std::unique_ptr<BrowserFrameView> CreateEmbeddedBrowserFrame(
    BrowserWidget* widget,
    BrowserView* view) {
  return std::make_unique<EmbeddedBrowserFrame>(widget, view);
}
}  // namespace fly
