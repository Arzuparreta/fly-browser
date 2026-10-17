// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/hint_controller.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/scoped_accessibility_mode.h"
#include "content/public/browser/web_contents.h"
#include "fly/commands/generated_contract.h"
#include "fly/commands/hint_codes.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_clipping_behavior.h"
#include "ui/accessibility/ax_coordinate_system.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_offscreen_result.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/platform/browser_accessibility.h"
#include "ui/accessibility/platform/browser_accessibility_manager.h"
#include "ui/aura/window.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
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
}  // namespace
HintController::HintController(views::Widget* desktop,
                               base::RepeatingCallback<void(bool)> changed)
    : desktop_(desktop), state_changed_(std::move(changed)) {
  desktop_->AddObserver(this);
}
HintController::~HintController() {
  Cancel();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
void HintController::Toggle(content::WebContents* contents) {
  if (active_) {
    Cancel();
    return;
  }
  if (!desktop_ || !contents || !contents->GetRenderWidgetHostView() ||
      contents->GetVisibility() != content::Visibility::VISIBLE)
    return;
  Observe(contents);
  active_ = true;
  state_changed_.Run(true);
  accessibility_ =
      content::BrowserAccessibilityState::GetInstance()
          ->CreateScopedModeForWebContents(contents, ui::kAXModeBasic);
  startup_timeout_.Start(
      FROM_HERE, base::Seconds(5),
      base::BindOnce(&HintController::Cancel, weak_factory_.GetWeakPtr()));
  ScheduleCollect();
}
void HintController::Cancel() {
  if (!active_)
    return;
  active_ = false;
  ready_ = false;
  truncated_ = false;
  collect_pending_ = false;
  validation_pending_ = false;
  startup_timeout_.Stop();
  overlay_.reset();
  targets_.clear();
  prefix_.clear();
  accessibility_.reset();
  Observe(nullptr);
  state_changed_.Run(false);
}
void HintController::ScheduleCollect() {
  if (!active_ || ready_ || collect_pending_)
    return;
  collect_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&HintController::Collect, weak_factory_.GetWeakPtr()));
}
void HintController::Collect() {
  collect_pending_ = false;
  if (!active_ || ready_ || !web_contents() || !desktop_)
    return;
  auto* root = web_contents()->GetAccessibilityRootNode();
  if (!root)
    return;  // The next native accessibility event retries, with a deadline.
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (!view) {
    Cancel();
    return;
  }
  viewport_ = view->GetViewBounds();
  std::vector<ui::AXNode*> stack{root};
  while (!stack.empty() && targets_.size() < kMaxHintTargets) {
    auto* node = stack.back();
    stack.pop_back();
    auto tree = node->tree()->GetAXTreeID();
    auto* frame = content::RenderFrameHost::FromAXTreeID(tree);
    if (frame &&
        content::WebContents::FromRenderFrameHost(frame) == web_contents()) {
      auto rect = ScreenBounds(tree, node->id());
      rect.Intersect(viewport_);
      const bool duplicate =
          std::ranges::any_of(targets_, [&](const Target& target) {
            return std::abs(rect.x() - target.bounds.x()) < 5 &&
                   std::abs(rect.y() - target.bounds.y()) < 5 &&
                   std::abs(rect.width() - target.bounds.width()) < 5 &&
                   std::abs(rect.height() - target.bounds.height()) < 5;
          });
      if (!rect.IsEmpty() && !duplicate)
        targets_.push_back(Target{tree, node->id(), rect, {}});
    }
    for (size_t i = node->GetUnignoredChildCountCrossingTreeBoundary(); i > 0;
         --i)
      stack.push_back(
          node->GetUnignoredChildAtIndexCrossingTreeBoundary(i - 1));
  }
  // An initial empty tree may precede the renderer's load-complete update.
  if (targets_.empty())
    return;
  truncated_ = !stack.empty();
  auto codes = GenerateHintCodes(targets_.size());
  for (size_t i = 0; i < targets_.size(); ++i)
    targets_[i].code = std::move(codes[i]);
  ready_ = true;
  state_changed_.Run(true);
  startup_timeout_.Stop();
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
  auto* root = overlay_->GetContentsView();
  root->RemoveAllChildViews();
  for (const auto& target : targets_) {
    if (!target.code.starts_with(prefix_))
      continue;
    auto label = std::make_unique<views::Label>(base::UTF8ToUTF16(target.code));
    label->SetEnabledColor(SkColorSetRGB(0x15, 0x13, 0x10));
    label->SetBackground(
        views::CreateSolidBackground(SkColorSetRGB(0xec, 0xe6, 0xd8)));
    label->SetBorder(
        views::CreateSolidBorder(1, SkColorSetRGB(0xe0, 0x26, 0x1c)));
    const auto size = label->GetPreferredSize();
    const int x = std::clamp(target.bounds.x() - viewport_.x(), 0,
                             std::max(0, viewport_.width() - size.width()));
    const int y = std::clamp(target.bounds.y() - viewport_.y(), 0,
                             std::max(0, viewport_.height() - size.height()));
    label->SetBounds(x, y, size.width(), size.height());
    root->AddChildView(std::move(label));
  }
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
}
bool HintController::Resolve(const Target& target, gfx::Rect* bounds) const {
  auto* frame = content::RenderFrameHost::FromAXTreeID(target.tree);
  if (!frame || !frame->IsRenderFrameLive() ||
      content::WebContents::FromRenderFrameHost(frame) != web_contents())
    return false;
  *bounds = ScreenBounds(target.tree, target.node);
  bounds->Intersect(viewport_);
  return !bounds->IsEmpty() && *bounds == target.bounds;
}
void HintController::Activate(const Target& target) {
  gfx::Rect bounds;
  if (!Resolve(target, &bounds)) {
    Cancel();
    return;
  }
  auto* frame = content::RenderFrameHost::FromAXTreeID(target.tree);
  ui::AXActionData action;
  action.target_tree_id = target.tree;
  action.target_node_id = target.node;
  action.action = ax::mojom::Action::kFocus;
  frame->AccessibilityPerformAction(action);
  action.action = ax::mojom::Action::kDoDefault;
  frame->AccessibilityPerformAction(action);
  // Queue the native action before releasing the mode needed to perform it.
  Cancel();
}
bool HintController::HandleKey(content::WebContents* contents,
                               const input::NativeWebKeyboardEvent& event) {
  if (!active_)
    return false;
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
  prefix_ += letter;
  auto match = std::ranges::find(targets_, prefix_, &Target::code);
  if (match != targets_.end()) {
    const auto target = *match;
    Activate(target);
  } else if (std::ranges::any_of(targets_, [&](const Target& target) {
               return target.code.starts_with(prefix_);
             })) {
    Draw();
  } else {
    Cancel();
  }
  return true;
}
void HintController::ScheduleValidation() {
  if (!active_ || !ready_ || validation_pending_)
    return;
  validation_pending_ = true;
  // AX notifications can arrive during tree updates. Never traverse or release
  // their manager reentrantly from that notification stack.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&HintController::ValidateLocations,
                                weak_factory_.GetWeakPtr()));
}
void HintController::ValidateLocations() {
  validation_pending_ = false;
  if (!active_ || !ready_)
    return;
  gfx::Rect bounds;
  if (std::ranges::any_of(targets_, [&](const Target& target) {
        return !Resolve(target, &bounds);
      }))
    Cancel();
}
void HintController::AccessibilityEventReceived(const ui::AXUpdatesAndEvents&) {
  if (ready_)
    ScheduleValidation();
  else
    ScheduleCollect();
}
void HintController::AccessibilityLocationChangesReceived(
    const ui::AXTreeID&,
    ui::AXLocationAndScrollUpdates&) {
  ScheduleValidation();
}
void HintController::PrimaryPageChanged(content::Page&) {
  Cancel();
}
void HintController::RenderFrameDeleted(content::RenderFrameHost*) {
  Cancel();
}
void HintController::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Cancel();
}
void HintController::OnWebContentsLostFocus(content::RenderWidgetHost*) {
  Cancel();
}
void HintController::WebContentsDestroyed() {
  Cancel();
}
void HintController::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  Cancel();
}
void HintController::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active)
    Cancel();
}
void HintController::OnWidgetDestroying(views::Widget*) {
  Cancel();
  desktop_->RemoveObserver(this);
  desktop_ = nullptr;
}
}  // namespace fly
