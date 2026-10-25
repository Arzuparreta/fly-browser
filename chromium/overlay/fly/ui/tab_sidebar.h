// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_TAB_SIDEBAR_H_
#define FLY_UI_TAB_SIDEBAR_H_

#include <cstdint>
#include <map>
#include <memory>

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
namespace content {
class WebContents;
}
namespace views {
class Button;
class ScrollView;
}  // namespace views
namespace ui {
class SimpleMenuModel;
}

namespace fly {
class SidebarTabMenu;
class FlyTabButton;
class TabRowView;
// The tab list: a projection of the selected profile's upstream tab model. Rows
// hold weak WebContents identities, never a cached tab index or an extension's
// session ID. Every native tab is projected, including about:blank; URL and
// title are presentation only. An empty model has no synthetic placeholder tab.
// One list serves both sidebar states: rows are 40px tall, 4px apart, with the
// favicon on the collapsed rail's centre line and the text past its edge. The
// expanded row surfaces, the rail's 28 x 40px tiles and the rail's pin and
// audio marks are separate layers that crossfade.
class TabSidebar : public views::View,
                   public TabStripModelObserver,
                   public views::ContextMenuController {
 public:
  METADATA_HEADER(TabSidebar, views::View)

 public:
  explicit TabSidebar(base::RepeatingClosure new_tab);
  ~TabSidebar() override;
  void SetBrowser(BrowserWindowInterface* browser);
  void SetStateChangedCallback(base::RepeatingClosure callback) {
    state_changed_ = std::move(callback);
  }
  void OnSplitTabChanged(const SplitTabChange& change) override;
  // The collapsed rail shows no hover tint (a hover left over from before
  // the rail collapsed must not tint it).
  void SetRail(bool rail);
  bool rail() const { return rail_; }
  views::View* expanded_surfaces() { return expanded_surfaces_; }
  views::View* rail_surfaces() { return rail_surfaces_; }
  views::View* rail_marks() { return rail_marks_; }
  // The tab selection numbers over the favicons (1-8, and 9 on the last tab).
  void SetNumbersVisible(bool visible);
  bool numbers_visible() const { return numbers_visible_; }
  void OnThemeChanged() override;
  void FocusActiveTab();
  size_t row_count_for_testing() const;
  views::View* row_for_testing(content::WebContents* contents) const;
  views::Button* audio_for_testing(content::WebContents* contents) const;
  // The site favicon a row shows (null while it shows the globe).
  gfx::ImageSkia favicon_for_testing(content::WebContents* contents) const;
  ui::SimpleMenuModel* context_menu_for_testing() const;
  void CloseContextMenu();

  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabStripModelDestroyed(TabStripModel* model) override;
  // Rereads the tabs soon, e.g. after a close request that a page's unload
  // handlers may hold back.
  void ScheduleRefresh();

 private:
  class Surfaces;
  class RailMarks;
  class Numbers;
  enum class RowAction { kSelect, kClose, kPin, kMute };
  class CloseWatch;
  base::RepeatingClosure state_changed_;
  void Refresh();
  void Invoke(base::WeakPtr<content::WebContents> contents, RowAction action);
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;
  void ExecuteMenuAction(uint64_t generation,
                         base::WeakPtr<content::WebContents> contents,
                         int action);
  void RepaintSurfaces();

  struct Row {
    base::WeakPtr<content::WebContents> contents;
    raw_ptr<TabRowView> view;
    gfx::ImageSkia favicon;
    bool favicon_initialized = false;
    base::CallbackListSubscription audible_subscription;
    // Watches a tab whose close waits for its page; a cancelled close
    // restores the row.
    std::unique_ptr<CloseWatch> close_watch;
  };
  std::map<content::WebContents*, Row> tab_rows_;
  std::unique_ptr<SidebarTabMenu> context_menu_;
  uint64_t context_generation_ = 0;
  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  raw_ptr<TabStripModel> model_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::View> rows_ = nullptr;
  raw_ptr<views::View> expanded_surfaces_ = nullptr;
  raw_ptr<views::View> rail_surfaces_ = nullptr;
  raw_ptr<views::View> rail_marks_ = nullptr;
  raw_ptr<views::View> numbers_ = nullptr;
  bool numbers_visible_ = false;
  base::RepeatingClosure new_tab_;
  bool rail_ = false;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<TabSidebar> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_TAB_SIDEBAR_H_
