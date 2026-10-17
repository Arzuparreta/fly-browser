// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/tab_sidebar.h"

#include <set>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"

namespace fly {
TabSidebar::TabSidebar() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  auto* add = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](base::WeakPtr<TabSidebar> sidebar) {
            if (sidebar && sidebar->browser_)
              chrome::NewTab(sidebar->browser_, NewTabTypes::kNewTabCommand);
          },
          weak_factory_.GetWeakPtr()),
      u"New tab"));
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
      auto* pin = row->AddChildView(std::make_unique<views::MdTextButton>(
          callback(RowAction::kPin), u""));
      pin->SetStyle(ui::ButtonStyle::kText);
      auto* close = row->AddChildView(std::make_unique<views::MdTextButton>(
          callback(RowAction::kClose), u"×"));
      close->SetStyle(ui::ButtonStyle::kText);
      auto* view = rows_->AddChildView(std::move(row));
      tab_rows_.emplace(contents,
                        Row{contents->GetWeakPtr(), view, select, pin, close});
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
    row.pin->SetText(model_->IsTabPinned(i) ? u"Unpin" : u"Pin");
    row.pin->GetViewAccessibility().SetName(
        (model_->IsTabPinned(i) ? u"Unpin " : u"Pin ") + title);
    row.close->GetViewAccessibility().SetName(u"Close " + title);
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
