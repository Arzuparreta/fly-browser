// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/command_palette.h"

#include <algorithm>
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/input/native_web_keyboard_event.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/native_commands.h"
#include "ui/aura/window.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
namespace {
class PickerButton : public views::MdTextButton {
  METADATA_HEADER(PickerButton, views::MdTextButton)
 public:
  PickerButton(PressedCallback callback, std::u16string_view text)
      : views::MdTextButton(std::move(callback), text) {
    label()->SetMultiLine(true);
  }
};
BEGIN_METADATA(PickerButton)
END_METADATA
class DismissSurface : public views::View {
 public:
  explicit DismissSurface(base::RepeatingClosure dismiss)
      : dismiss_(std::move(dismiss)) {}
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!children().empty() &&
        children().front()->bounds().Contains(event.location()))
      return false;
    dismiss_.Run();
    return true;
  }

 private:
  base::RepeatingClosure dismiss_;
};
}  // namespace
CommandPalette::CommandPalette(
    views::Widget* desktop,
    base::RepeatingCallback<bool(std::string_view)> available,
    base::RepeatingCallback<void(content::WebContents*, std::string)> execute,
    base::RepeatingCallback<void(content::WebContents*, std::string)> remove)
    : desktop_(desktop),
      available_(std::move(available)),
      execute_(std::move(execute)), remove_(std::move(remove)) {
  desktop_->AddObserver(this);
}
CommandPalette::~CommandPalette() {
  weak_factory_.InvalidateWeakPtrs();
  Observe(nullptr);
  buttons_.clear();
  remove_buttons_.clear();
  search_ = nullptr;
  results_ = nullptr;
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
void CommandPalette::Toggle(content::WebContents* contents,
                            const KeyboardConfig& config) {
  ToggleItems(contents, PaletteCatalog(config),
              config.shortcuts.at("toggle-palette"), u"Search commands", false);
}
void CommandPalette::ToggleItems(content::WebContents* contents,
                                 std::vector<PaletteEntry> items,
                                 std::vector<std::string> close_bindings,
                                 std::u16string placeholder, bool removable,
                                 bool allow_empty_source) {
  if (overlay_) {
    Cancel(true);
    return;
  }
  if (!desktop_ || (!contents && !allow_empty_source))
    return;
  Observe(contents);
  empty_source_ = !contents;
  if (contents)
    contents->StoreFocus();
  catalog_ = std::move(items);
  palette_bindings_ = std::move(close_bindings);
  removable_ = removable;
  closing_ = false;
  cancelled_ = false;
  overlay_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = desktop_->GetNativeWindow();
  params.child = true;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.native_widget = new views::NativeWidgetAura(overlay_.get());
  params.name = "FlyCommandPalette";
  params.bounds = desktop_->GetContentsView()->GetBoundsInScreen();
  wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &params.bounds);
  const auto bounds = params.bounds;
  overlay_->Init(std::move(params));
  auto root = std::make_unique<DismissSurface>(base::BindRepeating(
      &CommandPalette::Cancel, weak_factory_.GetWeakPtr(), true));
  root->SetBackground(
      views::CreateSolidBackground(SkColorSetARGB(96, 0, 0, 0)));
  const int margin = std::max(8, (bounds.width() - 640) / 2);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(24, margin, 24, margin)));
  auto panel = std::make_unique<views::View>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));
  panel->SetPreferredSize(
      gfx::Size(640, std::max(80, std::min(520, bounds.height() - 48))));
  panel->SetBackground(
      views::CreateSolidBackground(ui::kColorDialogBackground));
  search_ = panel->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(placeholder);
  search_->GetViewAccessibility().SetName(placeholder);
  search_->set_controller(this);
  auto* scroll = panel->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, 440);
  results_ = scroll->SetContents(std::make_unique<views::View>());
  results_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->SetFlexForView(scroll, 1);
  root->AddChildView(std::move(panel));
  overlay_->SetContentsView(std::move(root));
  Refresh();
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  search_->RequestFocus();
}
void CommandPalette::UpdateItems(std::vector<PaletteEntry> items, bool removable) {
  if (!active())
    return;
  std::string selected;
  if (selected_ >= 0)
    selected = filtered_[selected_].action;
  catalog_ = std::move(items);
  removable_ = removable;
  Refresh();
  for (size_t i = 0; i < filtered_.size(); ++i) {
    if (filtered_[i].action == selected && buttons_[i]->GetEnabled()) {
      if (selected_ >= 0)
        buttons_[selected_]->SetIsDefault(false);
      selected_ = static_cast<int>(i);
      buttons_[i]->SetIsDefault(true);
      break;
    }
  }
}
void CommandPalette::Refresh() {
  if (!active())
    return;
  filtered_ = FilterPalette(catalog_, search_->GetText());
  buttons_.clear();
  remove_buttons_.clear();
  results_->RemoveAllChildViews();
  selected_ = -1;
  for (const auto& entry : filtered_) {
    auto text = entry.title;
    if (remove_ && !entry.category.empty())
      text += "\n" + entry.category;
    if (!entry.shortcuts.empty())
      text += "    " + base::JoinString(entry.shortcuts, " / ");
    if (!entry.normal.empty())
      text += "    " + base::JoinString(entry.normal, " / ") + " (NORMAL)";
    auto* row = results_->AddChildView(std::make_unique<views::View>());
    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    auto* button = row->AddChildView(std::make_unique<PickerButton>(
        base::BindRepeating(&CommandPalette::Activate,
                            weak_factory_.GetWeakPtr(), entry.action),
        base::UTF8ToUTF16(text)));
    button->SetEnabled(available_.Run(entry.action));
    row_layout->SetFlexForView(button, 1);
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(text));
    buttons_.push_back(button);
    if (remove_) {
      auto* remove = row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&CommandPalette::Remove, weak_factory_.GetWeakPtr(),
                              entry.action), u"Remove"));
      remove->SetEnabled(removable_ && available_.Run(entry.action));
      remove->GetViewAccessibility().SetName(u"Remove " + base::UTF8ToUTF16(text));
      remove_buttons_.push_back(remove);
    }
  }
  if (buttons_.empty())
    results_->AddChildView(
        std::make_unique<views::Label>(remove_ ? u"No matching bookmarks"
                                              : u"No matching commands"));
  Select(1);
}
void CommandPalette::Select(int direction) {
  if (buttons_.empty())
    return;
  if (selected_ >= 0)
    buttons_[selected_]->SetIsDefault(false);
  const int count = static_cast<int>(buttons_.size());
  for (int i = 0; i < count; ++i) {
    selected_ = (selected_ + direction + count) % count;
    if (buttons_[selected_]->GetEnabled()) {
      buttons_[selected_]->SetIsDefault(true);
      buttons_[selected_]->ScrollViewToVisible();
      return;
    }
  }
  selected_ = -1;
}
void CommandPalette::ContentsChanged(views::Textfield*, const std::u16string&) {
  Refresh();
}
bool CommandPalette::HandleKeyEvent(views::Textfield*,
                                    const ui::KeyEvent& event) {
  return HandleOverlayKey(event);
}
bool CommandPalette::HandleOverlayKey(const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || search_->IsIMEComposing() ||
      event.IsAltGrDown())
    return false;
  const input::NativeWebKeyboardEvent native(event);
  const auto stroke = NativeEventStroke(native);
  if (event.key_code() == ui::VKEY_ESCAPE ||
      std::ranges::find(palette_bindings_, stroke) != palette_bindings_.end()) {
    Cancel(true);
    return true;
  }
  if (event.flags() &
      (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN))
    return false;
  if (remove_ && removable_ && selected_ >= 0 &&
      event.key_code() == ui::VKEY_DELETE &&
      (!search_->HasFocus() || (search_->GetSelectedRange().is_empty() &&
        search_->GetCursorPosition() == search_->GetText().size()))) {
    Remove(filtered_[selected_].action);
    return true;
  }
  if (!search_->HasFocus())
    return false;
  if (event.key_code() == ui::VKEY_DOWN || event.key_code() == ui::VKEY_UP) {
    Select(event.key_code() == ui::VKEY_DOWN ? 1 : -1);
    return true;
  }
  if (remove_ && (event.key_code() == ui::VKEY_NEXT ||
                  event.key_code() == ui::VKEY_PRIOR)) {
    if (selected_ >= 0) {
      buttons_[selected_]->SetIsDefault(false);
      selected_ = std::clamp(selected_ + (event.key_code() == ui::VKEY_NEXT ? 5 : -5),
                             0, static_cast<int>(buttons_.size()) - 1);
      buttons_[selected_]->SetIsDefault(true);
      buttons_[selected_]->ScrollViewToVisible();
    }
    return true;
  }
  if (event.key_code() == ui::VKEY_RETURN) {
    if (selected_ >= 0)
      Activate(filtered_[selected_].action);
    return true;
  }
  return false;
}
void CommandPalette::Remove(std::string action) {
  if (!active() || !remove_ || !removable_ || !available_.Run(action))
    return;
  // An update may replace the row. Run only after the native input callback has
  // returned, then recheck surface lifetime and the current collection.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CommandPalette::RemoveAfterEvent,
                               weak_factory_.GetWeakPtr(), std::move(action)));
}
void CommandPalette::RemoveAfterEvent(std::string action) {
  if (active() && !cancelled_ && removable_ && web_contents() &&
      available_.Run(action))
    remove_.Run(web_contents(), std::move(action));
}
void CommandPalette::Activate(std::string action) {
  if (!active() || !available_.Run(action))
    return;
  closing_ = true;
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&CommandPalette::FinishClose, weak_factory_.GetWeakPtr(),
                     true, std::move(action)));
}
void CommandPalette::Cancel(bool restore_focus) {
  if (!overlay_)
    return;
  if (!restore_focus)
    cancelled_ = true;
  if (closing_)
    return;
  closing_ = true;
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CommandPalette::FinishClose,
                                weak_factory_.GetWeakPtr(), restore_focus, ""));
}
void CommandPalette::FinishClose(bool restore_focus, std::string action) {
  auto contents = web_contents() ? web_contents()->GetWeakPtr() : nullptr;
  const bool source_was_empty = empty_source_;
  empty_source_ = false;
  Observe(nullptr);
  buttons_.clear();
  remove_buttons_.clear();
  search_ = nullptr;
  results_ = nullptr;
  overlay_.reset();
  filtered_.clear();
  catalog_.clear();
  closing_ = false;
  // A pointer-opened surface may have saved a sidebar control as the
  // BrowserWidget focus target. Return to the actual page; its DOM focused
  // element is retained by Chromium, independently of native chrome focus.
  if (restore_focus && contents && !cancelled_)
    contents->Focus();
  if (!action.empty() && (contents || source_was_empty) && !cancelled_)
    execute_.Run(contents.get(), std::move(action));
}
void CommandPalette::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  Cancel(true);
}
void CommandPalette::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active)
    Cancel();
}
void CommandPalette::OnWidgetDestroying(views::Widget*) {
  desktop_ = nullptr;
  Cancel();
}
void CommandPalette::PrimaryPageChanged(content::Page&) {
  Cancel();
}
void CommandPalette::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Cancel();
}
void CommandPalette::WebContentsDestroyed() {
  Cancel();
}
}  // namespace fly
