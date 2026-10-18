// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/keyboard_editor.h"

#include <algorithm>
#include <set>
#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/native_commands.h"
#include "ui/aura/window.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
KeyboardEditor::KeyboardEditor(views::Widget* desktop, CommandService& commands)
    : desktop_(desktop), commands_(commands) {
  desktop_->AddObserver(this);
}
KeyboardEditor::~KeyboardEditor() {
  weak_factory_.InvalidateWeakPtrs();
  recording_timer_.Stop();
  Observe(nullptr);
  fields_.clear();
  search_ = nullptr;
  tabs_ = nullptr;
  rows_ = nullptr;
  status_ = nullptr;
  save_ = nullptr;
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
void KeyboardEditor::Toggle(content::WebContents* contents) {
  if (overlay_) {
    Cancel(true);
    return;
  }
  if (!desktop_ || !contents)
    return;
  Observe(contents);
  contents->StoreFocus();
  const auto current = commands_->Read();
  original_ = current.value_or(DefaultKeyboardConfig());
  generation_ = commands_->generation();
  catalog_ = PaletteCatalog(original_);
  std::set<std::string> actions;
  std::erase_if(catalog_, [&](const PaletteEntry& entry) {
    return !actions.insert(entry.action).second;
  });
  close_bindings_ = original_.shortcuts.at("toggle-help");
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? original_.normal : original_.shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(original_.tabs);
  closing_ = cancelled_ = false;
  overlay_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = desktop_->GetNativeWindow();
  params.child = true;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.native_widget = new views::NativeWidgetAura(overlay_.get());
  params.name = "FlyKeyboardPreferences";
  params.bounds = desktop_->GetContentsView()->GetBoundsInScreen();
  wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &params.bounds);
  const auto bounds = params.bounds;
  overlay_->Init(std::move(params));
  auto root = std::make_unique<views::View>();
  root->SetBackground(
      views::CreateSolidBackground(SkColorSetARGB(96, 0, 0, 0)));
  const int margin = std::max(8, (bounds.width() - 900) / 2);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(16, margin, 16, margin)));
  auto panel = std::make_unique<views::View>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));
  panel->SetPreferredSize(gfx::Size(900, std::max(160, bounds.height() - 32)));
  panel->SetBackground(
      views::CreateSolidBackground(ui::kColorDialogBackground));
  panel->AddChildView(
      std::make_unique<views::Label>(u"Preferences · Keyboard"));
  panel->AddChildView(std::make_unique<views::Label>(
      u"One binding per line. Add key records a shortcut or a two-key "
      u"NORMAL sequence."));
  search_ = panel->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(u"Search actions");
  search_->GetViewAccessibility().SetName(u"Search keyboard actions");
  search_->set_controller(this);
  auto* modifiers = panel->AddChildView(std::make_unique<views::View>());
  auto* modifier_layout =
      modifiers->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  modifiers->AddChildView(
      std::make_unique<views::Label>(u"Numbered tab modifier  "));
  tabs_ = modifiers->AddChildView(std::make_unique<views::Textfield>());
  tabs_->SetText(tab_modifiers_);
  tabs_->GetViewAccessibility().SetName(u"Numbered tab modifier");
  tabs_->set_controller(this);
  modifier_layout->SetFlexForView(tabs_, 1);
  auto* scroll = panel->AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  rows_ = scroll->SetContents(std::make_unique<views::View>());
  rows_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
  layout->SetFlexForView(scroll, 1);
  status_ = panel->AddChildView(std::make_unique<views::Label>(u""));
  status_->SetMultiLine(true);
  status_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  auto* buttons = panel->AddChildView(std::make_unique<views::View>());
  buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  buttons->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&KeyboardEditor::ResetAll,
                          weak_factory_.GetWeakPtr()),
      u"Restore defaults"));
  buttons->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&KeyboardEditor::Cancel, weak_factory_.GetWeakPtr(),
                          true),
      u"Cancel"));
  save_ = buttons->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&KeyboardEditor::Save, weak_factory_.GetWeakPtr()),
      u"Apply"));
  save_->SetStyle(ui::ButtonStyle::kProminent);
  save_->SetEnabled(current.has_value());
  if (!current)
    status_->SetText(
        u"Existing keyboard settings need recovery. They have been preserved.");
  root->AddChildView(std::move(panel));
  overlay_->SetContentsView(std::move(root));
  Refresh();
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  search_->RequestFocus();
}
void KeyboardEditor::Refresh() {
  StopRecording();
  fields_.clear();
  rows_->RemoveAllChildViews();
  for (const auto& entry : FilterPalette(catalog_, search_->GetText())) {
    auto* row = rows_->AddChildView(std::make_unique<views::View>());
    row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    row->AddChildView(
        std::make_unique<views::Label>(base::UTF8ToUTF16(entry.title)));
    for (const auto normal : {false, true}) {
      const Key key{entry.action, normal};
      auto* scope = row->AddChildView(std::make_unique<views::View>());
      auto* layout = scope->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
      auto* label = scope->AddChildView(
          std::make_unique<views::Label>(normal ? u"NORMAL" : u"Global"));
      label->SetPreferredSize(gfx::Size(70, 24));
      auto* field = scope->AddChildView(std::make_unique<views::Textarea>());
      field->SetPreferredSize(gfx::Size(320, 52));
      field->SetText(draft_[key]);
      field->set_controller(this);
      field->GetViewAccessibility().SetName(
          base::UTF8ToUTF16(entry.title) +
          (normal ? u", NORMAL bindings" : u", global bindings"));
      layout->SetFlexForView(field, 1);
      auto* record = scope->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&KeyboardEditor::StartRecording,
                              weak_factory_.GetWeakPtr(), key),
          u"Add key"));
      scope->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&KeyboardEditor::WriteField,
                              weak_factory_.GetWeakPtr(), key, u""),
          u"Clear"));
      scope->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&KeyboardEditor::ResetField,
                              weak_factory_.GetWeakPtr(), key),
          u"Default"));
      fields_.emplace(key, Field{field, record});
    }
  }
}
void KeyboardEditor::ContentsChanged(views::Textfield* sender,
                                     const std::u16string& text) {
  if (!active())
    return;
  if (sender == search_) {
    Refresh();
  } else if (sender == tabs_) {
    tab_modifiers_ = text;
  } else {
    for (const auto& [key, field] : fields_)
      if (field.input == sender) {
        draft_[key] = text;
        break;
      }
  }
}
void KeyboardEditor::WriteField(Key key, std::u16string value) {
  StopRecording();
  draft_[key] = value;
  if (const auto field = fields_.find(key); field != fields_.end())
    field->second.input->SetText(value);
}
void KeyboardEditor::ResetField(Key key) {
  const auto& table = key.second ? DefaultKeyboardConfig().normal
                                 : DefaultKeyboardConfig().shortcuts;
  const auto found = table.find(key.first);
  WriteField(key, found == table.end() ? u""
                                       : base::UTF8ToUTF16(base::JoinString(
                                             found->second, "\n")));
}
void KeyboardEditor::ResetAll() {
  StopRecording();
  for (const auto& [key, value] : draft_) {
    const auto& table = key.second ? DefaultKeyboardConfig().normal
                                   : DefaultKeyboardConfig().shortcuts;
    const auto found = table.find(key.first);
    draft_[key] =
        found == table.end()
            ? u""
            : base::UTF8ToUTF16(base::JoinString(found->second, "\n"));
  }
  tab_modifiers_ = base::UTF8ToUTF16(DefaultKeyboardConfig().tabs);
  tabs_->SetText(tab_modifiers_);
  status_->SetText(u"Defaults restored in this draft. Apply to save them.");
  Refresh();
}
void KeyboardEditor::StartRecording(Key key) {
  StopRecording();
  recording_ = std::move(key);
  fields_.at(*recording_).record->RequestFocus();
  status_->SetText(recording_->second
                       ? u"Press one or two keys. Escape cancels recording."
                       : u"Press a shortcut. Escape cancels recording.");
}
void KeyboardEditor::StopRecording() {
  recording_timer_.Stop();
  recording_.reset();
  chord_.clear();
}
void KeyboardEditor::FinishRecording() {
  if (!recording_ || chord_.empty())
    return;
  const auto key = *recording_;
  auto bindings =
      base::SplitString(base::UTF16ToUTF8(draft_[key]), "\n",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (std::ranges::find(bindings, chord_) == bindings.end())
    bindings.push_back(chord_);
  WriteField(key, base::UTF8ToUTF16(base::JoinString(bindings, "\n")));
  status_->SetText(u"Binding added to this draft. Apply to save it.");
}
bool KeyboardEditor::HandleOverlayKey(const ui::KeyEvent& event) {
  if (!active())
    return false;
  if (event.type() != ui::EventType::kKeyPressed || event.is_char())
    return recording();
  const input::NativeWebKeyboardEvent native(event);
  const auto stroke = NativeEventStroke(native);
  if (stroke == "Escape") {
    if (recording()) {
      StopRecording();
      status_->SetText(u"Recording cancelled.");
    } else {
      Cancel(true);
    }
    return true;
  }
  if (recording()) {
    if (native.skip_if_unhandled ||
        (native.GetModifiers() & (blink::WebInputEvent::kIsComposing |
                                  blink::WebInputEvent::kAltGrKey))) {
      StopRecording();
      status_->SetText(u"Composition keys are not recorded.");
      return false;
    }
    if (event.is_repeat() || stroke.empty())
      return true;
    if (chord_.empty()) {
      chord_ = stroke;
      if (!recording_->second) {
        FinishRecording();
      } else {
        recording_timer_.Start(FROM_HERE, base::Milliseconds(700),
                               base::BindOnce(&KeyboardEditor::FinishRecording,
                                              weak_factory_.GetWeakPtr()));
      }
    } else {
      chord_ += " " + stroke;
      FinishRecording();
    }
    return true;
  }
  if (!stroke.empty() &&
      std::ranges::find(close_bindings_, stroke) != close_bindings_.end()) {
    Cancel(true);
    return true;
  }
  return false;
}
bool KeyboardEditor::HandleKeyEvent(views::Textfield*,
                                    const ui::KeyEvent& event) {
  return HandleOverlayKey(event);
}
void KeyboardEditor::Save() {
  StopRecording();
  if (commands_->generation() != generation_) {
    status_->SetText(
        u"Keyboard settings changed while this editor was open. "
        u"Reopen it to edit the latest settings.");
    return;
  }
  auto config = original_.ToValue();
  for (const auto& [key, text] : draft_) {
    base::ListValue bindings;
    for (const auto& chord :
         base::SplitString(base::UTF16ToUTF8(text), "\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY))
      bindings.Append(chord);
    config.FindDict(key.second ? "normal" : "shortcuts")
        ->Set(key.first, std::move(bindings));
  }
  config.Set("tabs", base::UTF16ToUTF8(tab_modifiers_));
  const auto saved = commands_->Save(config);
  if (!saved.config) {
    status_->SetText(base::UTF8ToUTF16(saved.error));
    return;
  }
  Cancel(true);
}
void KeyboardEditor::Cancel(bool restore_focus) {
  StopRecording();
  if (!overlay_)
    return;
  if (!restore_focus)
    cancelled_ = true;
  if (closing_)
    return;
  closing_ = true;
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::FinishClose,
                                weak_factory_.GetWeakPtr(), restore_focus));
}
void KeyboardEditor::FinishClose(bool restore_focus) {
  auto contents = web_contents() ? web_contents()->GetWeakPtr() : nullptr;
  Observe(nullptr);
  fields_.clear();
  search_ = nullptr;
  tabs_ = nullptr;
  rows_ = nullptr;
  status_ = nullptr;
  save_ = nullptr;
  overlay_.reset();
  draft_.clear();
  catalog_.clear();
  closing_ = false;
  if (restore_focus && contents && !cancelled_)
    contents->RestoreFocus();
}
views::Textfield* KeyboardEditor::field_for_testing(std::string_view action,
                                                    bool normal) {
  auto it = fields_.find({std::string(action), normal});
  return it == fields_.end() ? nullptr : it->second.input.get();
}
views::MdTextButton* KeyboardEditor::record_for_testing(std::string_view action,
                                                        bool normal) {
  auto it = fields_.find({std::string(action), normal});
  return it == fields_.end() ? nullptr : it->second.record.get();
}
std::u16string KeyboardEditor::error_for_testing() const {
  return status_ ? std::u16string(status_->GetText()) : std::u16string();
}
void KeyboardEditor::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  Cancel(true);
}
void KeyboardEditor::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active)
    Cancel();
}
void KeyboardEditor::OnWidgetDestroying(views::Widget*) {
  desktop_ = nullptr;
  Cancel();
}
void KeyboardEditor::PrimaryPageChanged(content::Page&) {
  Cancel();
}
void KeyboardEditor::WebContentsDestroyed() {
  Cancel();
}
void KeyboardEditor::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Cancel();
}
}  // namespace fly
