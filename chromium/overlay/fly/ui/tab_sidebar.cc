// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/tab_sidebar.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "skia/ext/image_operations.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"

namespace fly {
namespace {
void SetActionIcon(views::ImageButton* button, const gfx::VectorIcon& icon) {
  for (auto state :
       {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
        views::Button::STATE_PRESSED, views::Button::STATE_DISABLED})
    button->SetImageModel(state, ui::ImageModel::FromVectorIcon(
                                     icon,
                                     state == views::Button::STATE_DISABLED
                                         ? ui::kColorIconDisabled
                                         : ui::kColorIcon,
                                     16));
}
}  // namespace
TabSidebar::TabSidebar(base::RepeatingClosure new_tab) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  auto* add = AddChildView(
      std::make_unique<views::MdTextButton>(std::move(new_tab), u"New tab"));
  add->GetViewAccessibility().SetName(u"New tab in selected workspace");
  auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  rows_ = scroll->SetContents(std::make_unique<views::View>());
  rows_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  layout->SetFlexForView(scroll, 1);
}
TabSidebar::~TabSidebar() {
  if (model_)
    model_->RemoveObserver(this);
}
void TabSidebar::SetBrowser(BrowserWindowInterface* browser) {
  if (browser_ == browser)
    return;
  if (model_)
    model_->RemoveObserver(this);
  browser_ = browser;
  model_ = browser ? browser->GetTabStripModel() : nullptr;
  if (model_)
    model_->AddObserver(this);
  // Defer rebuilding: a click may synchronously switch workspace or close a
  // tab. Deleting the originating button during its own callback is unsafe.
  ScheduleRefresh();
}
void TabSidebar::ScheduleRefresh() {
  if (refresh_pending_)
    return;
  refresh_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&TabSidebar::Refresh, weak_factory_.GetWeakPtr()));
}
void TabSidebar::Refresh() {
  refresh_pending_ = false;
  std::set<content::WebContents*> live;
  if (model_) {
    for (int i = 0; i < model_->count(); ++i)
      live.insert(model_->GetWebContentsAt(i));
  }
  // Preserve existing button objects and focus through title/selection changes.
  // Remove obsolete rows before rebuilding; a reused allocation is not an
  // identity match when its original WeakPtr has expired.
  for (auto it = tab_rows_.begin(); it != tab_rows_.end();) {
    if (!live.contains(it->first) || !it->second.contents) {
      auto removed = rows_->RemoveChildViewT(it->second.view.get());
      it = tab_rows_.erase(it);
    } else {
      ++it;
    }
  }
  if (!model_)
    return;
  for (int i = 0; i < model_->count(); ++i) {
    auto* contents = model_->GetWebContentsAt(i);
    if (!tab_rows_.contains(contents)) {
      auto row = std::make_unique<views::View>();
      auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
      auto callback = [&](RowAction action) {
        return base::BindRepeating(&TabSidebar::Invoke,
                                   weak_factory_.GetWeakPtr(),
                                   contents->GetWeakPtr(), action);
      };
      auto* select = row->AddChildView(std::make_unique<views::MdTextButton>(
          callback(RowAction::kSelect), u""));
      select->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      layout->SetFlexForView(select, 1);
      auto icon_button = [&](RowAction action, const gfx::VectorIcon& icon,
                             const std::u16string& name) {
        auto* button = row->AddChildView(views::ImageButton::CreateIconButton(
            callback(action), icon, name,
            views::ImageButton::MaterialIconStyle::kSmall, gfx::Insets(4)));
        button->SetPreferredSize(gfx::Size(24, 24));
        return button;
      };
      auto* pin = icon_button(RowAction::kPin, views::kKeepIcon, u"Pin tab");
      auto* close = icon_button(RowAction::kClose, vector_icons::kCloseIcon,
                                u"Close tab");
      auto* audio = icon_button(RowAction::kMute, vector_icons::kVolumeUpIcon,
                                u"Mute tab");
      auto* view = rows_->AddChildView(std::move(row));
      auto& data = tab_rows_.try_emplace(contents).first->second;
      data.contents = contents->GetWeakPtr();
      data.view = view;
      data.select = select;
      data.pin = pin;
      data.close = close;
      data.audio = audio;
      if (auto* audible = RecentlyAudibleHelper::FromWebContents(contents)) {
        data.audible_subscription =
            audible->RegisterRecentlyAudibleChangedCallback(base::BindRepeating(
                [](base::WeakPtr<TabSidebar> sidebar, bool) {
                  if (sidebar)
                    sidebar->ScheduleRefresh();
                },
                weak_factory_.GetWeakPtr()));
      }
    }
    auto& row = tab_rows_.at(contents);
    rows_->ReorderChildView(row.view, i);
    std::u16string title = contents->GetTitle();
    if (title.empty())
      title = base::UTF8ToUTF16(contents->GetVisibleURL().spec());
    if (title.empty())
      title = u"New tab";
    const std::u16string position = base::NumberToString16(i + 1);
    row.select->SetText(position + u"  " + title);
    row.select->SetStyle(i == model_->active_index() ? ui::ButtonStyle::kTonal
                                                     : ui::ButtonStyle::kText);
    row.select->SetTooltipText(title);
    row.select->GetViewAccessibility().SetName(
        position + u". " + title +
        (i == model_->active_index() ? u", selected" : u"") +
        (model_->IsTabPinned(i) ? u", pinned" : u""));
    auto* navigation = contents->GetController().GetVisibleEntry();
    const gfx::ImageSkia icon =
        navigation && navigation->GetFavicon().valid &&
                !navigation->GetFavicon().image.IsEmpty()
            ? navigation->GetFavicon().image.AsImageSkia()
            : gfx::ImageSkia();
    if (!row.favicon_initialized || !icon.BackedBySameObjectAs(row.favicon)) {
      row.favicon = icon;
      row.favicon_initialized = true;
      row.select->SetImageModel(
          views::Button::STATE_NORMAL,
          icon.isNull() ? ui::ImageModel::FromVectorIcon(
                              vector_icons::kGlobeIcon, ui::kColorIcon, 16)
                        : ui::ImageModel::FromImageSkia(
                              gfx::ImageSkiaOperations::CreateResizedImage(
                                  icon, skia::ImageOperations::RESIZE_BEST,
                                  gfx::Size(16, 16))));
    }
    if (row.pinned != model_->IsTabPinned(i)) {
      row.pinned = model_->IsTabPinned(i);
      SetActionIcon(row.pin,
                    row.pinned ? views::kKeepFilledIcon : views::kKeepIcon);
    }
    const auto pin_label =
        (model_->IsTabPinned(i) ? u"Unpin " : u"Pin ") + title;
    row.pin->GetViewAccessibility().SetName(pin_label);
    row.pin->SetTooltipText(pin_label);
    row.close->GetViewAccessibility().SetName(u"Close " + title);
    row.close->SetTooltipText(u"Close " + title);
    const auto* audible = RecentlyAudibleHelper::FromWebContents(contents);
    const bool muted = contents->IsAudioMuted();
    row.audio->SetVisible(muted || (audible ? audible->WasRecentlyAudible()
                                            : contents->IsCurrentlyAudible()));
    if (row.muted != muted) {
      row.muted = muted;
      SetActionIcon(row.audio, muted ? vector_icons::kVolumeOffIcon
                                     : vector_icons::kVolumeUpIcon);
    }
    const auto audio_label = (muted ? u"Unmute " : u"Mute ") + title;
    row.audio->SetTooltipText(audio_label);
    row.audio->GetViewAccessibility().SetName(audio_label);
  }
}
void TabSidebar::Invoke(base::WeakPtr<content::WebContents> contents,
                        RowAction action) {
  if (!model_ || !contents)
    return;
  int index = model_->GetIndexOfWebContents(contents.get());
  if (index == TabStripModel::kNoTab)
    return;
  switch (action) {
    case RowAction::kSelect:
      model_->ActivateTabAt(index);
      contents->Focus();
      break;
    case RowAction::kMute:
      SetTabAudioMuted(contents.get(), !contents->IsAudioMuted(),
                       TabMutedReason::kAudioIndicator, std::string());
      break;
    case RowAction::kClose:
      model_->CloseWebContentsAt(
          index, TabCloseTypes::CLOSE_USER_GESTURE |
                     TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      break;
    case RowAction::kPin:
      model_->SetTabPinned(index, !model_->IsTabPinned(index));
      break;
  }
}
void TabSidebar::FocusActiveTab() {
  if (refresh_pending_)
    Refresh();
  if (!model_ || model_->active_index() < 0 ||
      static_cast<size_t>(model_->active_index()) >= rows_->children().size())
    return;
  rows_->children()[model_->active_index()]->children().front()->RequestFocus();
}
size_t TabSidebar::row_count_for_testing() const {
  return rows_->children().size();
}
views::View* TabSidebar::row_for_testing(content::WebContents* contents) const {
  auto it = tab_rows_.find(contents);
  return it == tab_rows_.end() ? nullptr : it->second.view.get();
}
views::ImageButton* TabSidebar::audio_for_testing(
    content::WebContents* contents) const {
  auto it = tab_rows_.find(contents);
  return it == tab_rows_.end() ? nullptr : it->second.audio.get();
}
void TabSidebar::OnTabStripModelChanged(TabStripModel*,
                                        const TabStripModelChange&,
                                        const TabStripSelectionChange&) {
  ScheduleRefresh();
}
void TabSidebar::OnTabChangedAt(tabs::TabInterface*, TabChangeType) {
  ScheduleRefresh();
}
void TabSidebar::OnTabPinnedStateChanged(tabs::TabInterface*, int) {
  ScheduleRefresh();
}
void TabSidebar::OnTabStripModelDestroyed(TabStripModel*) {
  // The observer base removes its registration before this notification.
  model_ = nullptr;
  browser_ = nullptr;
  ScheduleRefresh();
}
BEGIN_METADATA(TabSidebar)
END_METADATA
}  // namespace fly
