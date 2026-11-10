// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/hint_controller.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/scoped_accessibility_mode.h"
#include "content/public/browser/web_contents.h"
#include "fly/commands/hint_codes.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/page_scroller.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_clipping_behavior.h"
#include "ui/accessibility/ax_coordinate_system.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_offscreen_result.h"
#include "ui/accessibility/ax_role_properties.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_updates_and_events.h"
#include "ui/accessibility/platform/browser_accessibility.h"
#include "ui/accessibility/platform/browser_accessibility_manager.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {

// One label, drawn like Vimium's: small bold capitals on a light tag with a
// thin darker edge. Typed letters fade, as in Vimium.
class HintLabel : public views::View {
  METADATA_HEADER(HintLabel, views::View)

 public:
  explicit HintLabel(std::string_view code)
      : code_(base::UTF8ToUTF16(base::ToUpperASCII(code))) {
    SetCanProcessEventsWithinSubtree(false);
  }

  void SetTyped(size_t typed) {
    if (typed_ == typed)
      return;
    typed_ = typed;
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    const auto& font = Font();
    return gfx::Size(gfx::GetStringWidth(code_, font) + 2 * HPadding() + 2,
                     font.GetHeight() + 2);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const float radius = FlyScaled(3);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(kEdge);
    canvas->DrawRoundRect(bounds, radius, flags);
    flags.setColor(kFill);
    gfx::RectF inside = bounds;
    inside.Inset(1);
    canvas->DrawRoundRect(inside, radius - 1, flags);
    const auto& font = Font();
    int x = 1 + HPadding();
    for (size_t i = 0; i < code_.size(); ++i) {
      const auto letter = code_.substr(i, 1);
      const int width = gfx::GetStringWidth(letter, font);
      canvas->DrawStringRect(letter, font, i < typed_ ? kTyped : kText,
                             gfx::Rect(x, 1, width, font.GetHeight()));
      x += width;
    }
  }

 private:
  // Fly's paper and ink, the same in both themes so labels read on any page.
  static constexpr SkColor kFill = SkColorSetRGB(0xf6, 0xf1, 0xe4);
  static constexpr SkColor kEdge = SkColorSetRGB(0xa8, 0x9c, 0x82);
  static constexpr SkColor kText = SkColorSetRGB(0x15, 0x13, 0x10);
  static constexpr SkColor kTyped = SkColorSetRGB(0xb3, 0xa9, 0x94);

  static gfx::FontList Font() {
    return FlyFont(FlyFace::kMono, 11, gfx::Font::Weight::BOLD);
  }
  static int HPadding() { return FlyScaled(3); }

  std::u16string code_;
  size_t typed_ = 0;
};
BEGIN_METADATA(HintLabel)
END_METADATA

namespace {
// AX supplies clipped screen DIPs, including nested frames, zoom and display
// transforms. Never reconstruct these coordinates from renderer CSS values.
gfx::Rect ScreenBounds(ui::AXTreeID tree, ui::AXNodeID id) {
  auto* manager = ui::BrowserAccessibilityManager::FromID(tree);
  auto* node = manager ? manager->GetFromID(id) : nullptr;
  if (!node || node->GetData().IsInvisibleOrIgnored() ||
      !node->GetData().IsClickable() ||
      node->GetData().GetRestriction() == ax::mojom::Restriction::kDisabled)
    return {};
  ui::AXOffscreenResult offscreen = ui::AXOffscreenResult::kOffscreen;
  auto rect = node->GetBoundsRect(ui::AXCoordinateSystem::kScreenDIPs,
                                  ui::AXClippingBehavior::kClipped, &offscreen);
  return offscreen == ui::AXOffscreenResult::kOnscreen ? rect : gfx::Rect();
}

bool SameTargets(const auto& a, const auto& b) {
  return std::ranges::equal(a, b, [](const auto& x, const auto& y) {
    return x.tree == y.tree && x.node == y.node;
  });
}
}  // namespace

HintController::HintController(
    views::Widget* desktop,
    base::RepeatingCallback<void(bool)> changed,
    base::RepeatingCallback<void(content::WebContents*, GURL)> open_link)
    : desktop_(desktop),
      state_changed_(std::move(changed)),
      open_link_(std::move(open_link)) {
  desktop_->AddObserver(this);
}
HintController::~HintController() {
  Cancel();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
size_t HintController::visible_count_for_testing() const {
  return std::ranges::count_if(targets_, [&](const Target& target) {
    return target.label && target.label->GetVisible();
  });
}
std::string HintController::code_for_testing(std::u16string_view name) const {
  for (const auto& target : targets_) {
    auto* manager = ui::BrowserAccessibilityManager::FromID(target.tree);
    auto* node = manager ? manager->GetFromID(target.node) : nullptr;
    if (node && node->GetNameAsString16().find(name) != std::u16string::npos)
      return target.code;
  }
  return {};
}
gfx::Rect HintController::label_bounds_for_testing(
    std::u16string_view name) const {
  const auto code = code_for_testing(name);
  for (const auto& target : targets_) {
    if (target.code == code && target.label)
      return target.label->GetBoundsInScreen();
  }
  return {};
}
void HintController::Toggle(content::WebContents* contents, Mode mode) {
  if (active_) {
    // The key of the mode on screen closes it; another mode replaces it.
    const bool same = mode == mode_;
    Cancel();
    if (same)
      return;
  }
  if (!desktop_ || !contents || !contents->GetRenderWidgetHostView() ||
      contents->GetVisibility() != content::Visibility::VISIBLE)
    return;
  Observe(contents);
  mode_ = mode;
  active_ = true;
  cancellation_ = "none";
  state_changed_.Run(true);
  // Hints only need what is on screen, so Blink serializes the visible part of
  // the page. Filters add to every mode the page receives, so a page that
  // already has a tree for an assistive technology keeps its full tree.
  const ui::AXMode tree_mode =
      contents->GetAccessibilityMode().has_mode(ui::AXMode::kWebContents)
          ? ui::kAXModeBasic
          : ui::AXMode(ui::kAXModeBasic.flags(), ui::AXMode::kOnScreenOnly);
  accessibility_ = content::BrowserAccessibilityState::GetInstance()
                       ->CreateScopedModeForWebContents(contents, tree_mode);
  // Looking for a text field ends sooner: there is no overlay to say that
  // Fly is still waiting for the page.
  startup_timeout_.Start(
      FROM_HERE,
      mode_ == Mode::kFocusInput ? base::Milliseconds(1500) : base::Seconds(5),
      base::BindOnce(&HintController::CancelWithReason,
                     weak_factory_.GetWeakPtr(),
                     mode_ == Mode::kFocusInput ? "no-input"
                                                : "startup-timeout"));
  tree_changed_ = true;
  ScheduleUpdate();
}
void HintController::Cancel() {
  CancelWithReason("command");
}
void HintController::CancelWithReason(std::string_view reason) {
  if (!active_)
    return;
  cancellation_ = reason;
  VLOG(1) << "Link hints closed: " << reason;
  active_ = false;
  ready_ = false;
  truncated_ = false;
  update_pending_ = false;
  tree_changed_ = false;
  codes_frozen_ = false;
  startup_timeout_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  targets_.clear();
  overlay_.reset();
  prefix_.clear();
  accessibility_.reset();
  Observe(nullptr);
  state_changed_.Run(false);
}
void HintController::ScheduleUpdate() {
  if (!active_ || update_pending_)
    return;
  update_pending_ = true;
  // AX notifications can arrive during tree updates. Never traverse or release
  // their manager reentrantly from that notification stack.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&HintController::Update, weak_factory_.GetWeakPtr()));
}
void HintController::Update() {
  update_pending_ = false;
  if (!active_ || !web_contents() || !desktop_)
    return;
  // Until a label is typed, a tree that is still arriving may add targets.
  if (tree_changed_ && !codes_frozen_) {
    tree_changed_ = false;
    std::vector<Target> targets;
    if (!Collect(&targets)) {
      if (!web_contents()->GetRenderWidgetHostView())
        Cancel();
      return;
    }
    // An initial empty tree may precede the renderer's load-complete update.
    if (targets.empty() && !ready_)
      return;
    if (mode_ == Mode::kFocusInput) {
      // A later update may still bring the field; the timeout ends the wait.
      FocusFirstInput(targets);
      return;
    }
    if (!ready_ || !SameTargets(targets, targets_)) {
      Relabel(std::move(targets));
      return;
    }
  }
  if (ready_)
    Relayout();
}
bool HintController::Collect(std::vector<Target>* targets) const {
  auto* root = web_contents()->GetAccessibilityRootNode();
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!root || !view)
    return false;
  const auto viewport = view->GetViewBounds();
  std::vector<ui::AXNode*> stack{root};
  while (!stack.empty() && targets->size() < kMaxHintTargets) {
    auto* node = stack.back();
    stack.pop_back();
    auto tree = node->tree()->GetAXTreeID();
    auto* frame = content::RenderFrameHost::FromAXTreeID(tree);
    if (frame &&
        content::WebContents::FromRenderFrameHost(frame) == web_contents()) {
      auto rect = ScreenBounds(tree, node->id());
      rect.Intersect(viewport);
      // A link and the text or image it wraps share one target.
      const bool duplicate =
          std::ranges::any_of(*targets, [&](const Target& target) {
            return std::abs(rect.x() - target.bounds.x()) < 5 &&
                   std::abs(rect.y() - target.bounds.y()) < 5 &&
                   std::abs(rect.width() - target.bounds.width()) < 5 &&
                   std::abs(rect.height() - target.bounds.height()) < 5;
          });
      if (!rect.IsEmpty() && !duplicate)
        targets->push_back(Target{tree, node->id(), rect, {}});
    }
    for (size_t i = node->GetUnignoredChildCountCrossingTreeBoundary(); i > 0;
         --i)
      stack.push_back(
          node->GetUnignoredChildAtIndexCrossingTreeBoundary(i - 1));
  }
  return true;
}
void HintController::Relabel(std::vector<Target> targets) {
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!view)
    return Cancel();
  viewport_ = view->GetViewBounds();
  truncated_ = targets.size() >= kMaxHintTargets;
  targets_ = std::move(targets);
  auto codes = GenerateHintCodes(targets_.size());
  for (size_t i = 0; i < targets_.size(); ++i)
    targets_[i].code = std::move(codes[i]);
  if (!ready_) {
    ready_ = true;
    startup_timeout_.Stop();
    state_changed_.Run(true);
  }
  if (overlay_)
    overlay_->GetContentsView()->RemoveAllChildViews();
  for (auto& target : targets_)
    target.label = nullptr;
  Draw();
}
gfx::Rect HintController::CurrentBounds(const Target& target) const {
  auto* frame = content::RenderFrameHost::FromAXTreeID(target.tree);
  if (!frame || !frame->IsRenderFrameLive() ||
      content::WebContents::FromRenderFrameHost(frame) != web_contents())
    return {};
  auto bounds = ScreenBounds(target.tree, target.node);
  bounds.Intersect(viewport_);
  return bounds;
}
void HintController::Relayout() {
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!view)
    return Cancel();
  const auto viewport = view->GetViewBounds();
  bool moved = viewport != viewport_;
  viewport_ = viewport;
  for (auto& target : targets_) {
    const auto bounds = CurrentBounds(target);
    moved |= bounds != target.bounds;
    target.bounds = bounds;
  }
  if (moved)
    Draw();
}
void HintController::Draw() {
  if (!desktop_ || !ready_)
    return;
  if (!overlay_) {
    overlay_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_CONTROL);
    params.parent = desktop_->GetNativeWindow();
    params.child = true;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.accept_events = false;
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.native_widget = new views::NativeWidgetAura(overlay_.get());
    params.name = "FlyLinkHints";
    params.bounds = viewport_;
    wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &params.bounds);
    overlay_->Init(std::move(params));
    overlay_->SetContentsView(std::make_unique<views::View>());
    overlay_->GetContentsView()->SetCanProcessEventsWithinSubtree(false);
  }
  auto bounds = viewport_;
  wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &bounds);
  overlay_->SetBounds(bounds);
  auto* root = overlay_->GetContentsView();
  for (auto& target : targets_) {
    const bool shown =
        !target.bounds.IsEmpty() && target.code.starts_with(prefix_);
    if (!target.label) {
      if (!shown)
        continue;
      target.label =
          root->AddChildView(std::make_unique<HintLabel>(target.code));
    }
    target.label->SetVisible(shown);
    if (!shown)
      continue;
    target.label->SetTyped(prefix_.size());
    // At the element's top left corner, as in Vimium, kept on screen.
    const auto size = target.label->GetPreferredSize();
    const int x = std::clamp(target.bounds.x() - viewport_.x(), 0,
                             std::max(0, viewport_.width() - size.width()));
    const int y = std::clamp(target.bounds.y() - viewport_.y(), 0,
                             std::max(0, viewport_.height() - size.height()));
    target.label->SetBounds(x, y, size.width(), size.height());
  }
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
}
bool HintController::FocusFirstInput(const std::vector<Target>& targets) {
  for (const auto& target : targets) {
    auto* manager = ui::BrowserAccessibilityManager::FromID(target.tree);
    auto* node = manager ? manager->GetFromID(target.node) : nullptr;
    auto* frame = content::RenderFrameHost::FromAXTreeID(target.tree);
    if (!node || !frame || !node->GetData().IsTextField() ||
        node->GetData().GetRestriction() != ax::mojom::Restriction::kNone)
      continue;
    ui::AXActionData action;
    action.target_tree_id = target.tree;
    action.target_node_id = target.node;
    action.action = ax::mojom::Action::kFocus;
    frame->AccessibilityPerformAction(action);
    PageScroller::SetAnchor(web_contents(),
                            gfx::PointF(target.bounds.CenterPoint()));
    CancelWithReason("input-focused");
    return true;
  }
  return false;
}
GURL HintController::LinkOf(const Target& target) const {
  auto* manager = ui::BrowserAccessibilityManager::FromID(target.tree);
  auto* node = manager ? manager->GetFromID(target.node) : nullptr;
  // The label may sit on the text or image inside the link.
  for (int depth = 0; node && depth < 4; ++depth) {
    if (ui::IsLink(node->GetRole())) {
      const GURL url(
          node->GetStringAttribute(ax::mojom::StringAttribute::kUrl));
      // Pages of the web only: a script or an internal address is left to
      // the page's own click handling.
      return url.is_valid() && url.SchemeIsHTTPOrHTTPS() ? url : GURL();
    }
    node = node->PlatformGetParent();
  }
  return GURL();
}
void HintController::Activate(const Target& target) {
  const auto bounds = CurrentBounds(target);
  auto* frame = content::RenderFrameHost::FromAXTreeID(target.tree);
  if (bounds.IsEmpty() || !frame) {
    CancelWithReason("target-gone");
    return;
  }
  if (mode_ == Mode::kBackgroundTab && open_link_) {
    if (const GURL link = LinkOf(target); link.is_valid()) {
      auto* contents = web_contents();
      auto open = open_link_;
      Cancel();
      open.Run(contents, link);
      return;
    }
  }
  ui::AXActionData action;
  action.target_tree_id = target.tree;
  action.target_node_id = target.node;
  action.action = ax::mojom::Action::kFocus;
  frame->AccessibilityPerformAction(action);
  action.action = ax::mojom::Action::kDoDefault;
  frame->AccessibilityPerformAction(action);
  // Like a click, an activated hint directs later keyboard scrolling.
  PageScroller::SetAnchor(web_contents(), gfx::PointF(bounds.CenterPoint()));
  // Queue the native action before releasing the mode needed to perform it.
  Cancel();
}
bool HintController::HandleKey(content::WebContents* contents,
                               const input::NativeWebKeyboardEvent& event) {
  if (!active_)
    return false;
  // Without labels there is nothing to type: the key goes where it would.
  if (mode_ == Mode::kFocusInput) {
    Cancel();
    return false;
  }
  if (contents != web_contents()) {
    Cancel();
    return false;
  }
  using Input = blink::WebInputEvent;
  const int modifiers = event.GetModifiers();
  if (event.skip_if_unhandled ||
      (modifiers & (Input::kIsComposing | Input::kAltGrKey))) {
    Cancel();
    return false;
  }
  if (modifiers & (Input::kControlKey | Input::kAltKey | Input::kMetaKey))
    return false;
  ui::DomKey key(event.dom_key);
  if (key == ui::DomKey::ESCAPE) {
    Cancel();
    return true;
  }
  if (modifiers & Input::kIsAutoRepeat)
    return true;
  if (key == ui::DomKey::BACKSPACE) {
    if (!prefix_.empty())
      prefix_.pop_back();
    Draw();
    return true;
  }
  if (!ready_ || !key.IsCharacter())
    return true;
  const auto character = key.ToCharacter();
  if (character > 127)
    return true;
  const char letter = base::ToLowerASCII(static_cast<char>(character));
  if (kHintAlphabet.find(letter) == std::string_view::npos)
    return true;
  // Only labels on screen can be typed.
  const auto typeable = [&](const Target& target, std::string_view code) {
    return !target.bounds.IsEmpty() && target.code.starts_with(code);
  };
  codes_frozen_ = true;
  const std::string typed = prefix_ + letter;
  auto match = std::ranges::find_if(targets_, [&](const Target& target) {
    return target.code == typed && !target.bounds.IsEmpty();
  });
  if (match != targets_.end()) {
    prefix_ = typed;
    auto target = *match;
    // Activation closes the overlay; do not retain one of its views across it.
    target.label = nullptr;
    Activate(target);
  } else if (std::ranges::any_of(targets_, [&](const Target& target) {
               return typeable(target, typed);
             })) {
    prefix_ = typed;
    Draw();
  } else {
    // No label continues with this letter.
    Cancel();
  }
  return true;
}
void HintController::AccessibilityEventReceived(
    const ui::AXUpdatesAndEvents& details) {
  if (!details.updates.empty())
    tree_changed_ = true;
  ScheduleUpdate();
}
void HintController::AccessibilityLocationChangesReceived(
    const ui::AXTreeID&,
    ui::AXLocationAndScrollUpdates&) {
  ScheduleUpdate();
}
void HintController::PrimaryPageChanged(content::Page&) {
  CancelWithReason("navigation");
}
void HintController::RenderFrameDeleted(content::RenderFrameHost* frame) {
  // A frame that owned hint targets takes them away; the others stay.
  if (web_contents() && frame == web_contents()->GetPrimaryMainFrame())
    CancelWithReason("target-frame-deleted");
  else if (std::ranges::any_of(targets_, [&](const Target& target) {
             return target.tree == frame->GetAXTreeID();
           }))
    ScheduleUpdate();
}
void HintController::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    CancelWithReason("page-hidden");
}
void HintController::OnWebContentsLostFocus(content::RenderWidgetHost*) {
  CancelWithReason("page-focus-lost");
}
void HintController::WebContentsDestroyed() {
  CancelWithReason("contents-destroyed");
}
void HintController::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  CancelWithReason("desktop-bounds");
}
void HintController::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active)
    CancelWithReason("desktop-inactive");
}
void HintController::OnWidgetDestroying(views::Widget*) {
  CancelWithReason("desktop-destroyed");
  desktop_->RemoveObserver(this);
  desktop_ = nullptr;
}
}  // namespace fly
