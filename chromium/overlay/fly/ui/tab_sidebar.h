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
class MdTextButton;
class ImageButton;
class Button;
}  // namespace views
namespace ui {
class SimpleMenuModel;
}

namespace fly {
class SidebarTabMenu;
class FlyTabButton;
// A projection of the selected profile's upstream tab model. Rows hold weak
// WebContents identities, never a cached tab index or an extension's session
// ID. Every native tab is projected, including about:blank; URL and title are
// presentation only. An empty model has no synthetic placeholder tab.
class TabSidebar : public views::View,
                   public TabStripModelObserver,
                   public views::ContextMenuController {
 public:
  METADATA_HEADER(TabSidebar, views::View)

 public:
  explicit TabSidebar(base::RepeatingClosure new_tab, bool compact = false);
  ~TabSidebar() override;
  void SetBrowser(BrowserWindowInterface* browser);
  void OnThemeChanged() override;
  void FocusActiveTab();
  size_t row_count_for_testing() const;
  views::View* row_for_testing(content::WebContents* contents) const;
  views::Button* audio_for_testing(content::WebContents* contents) const;
  ui::SimpleMenuModel* context_menu_for_testing() const;
  void CloseContextMenu();

  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabStripModelDestroyed(TabStripModel* model) override;

 private:
  enum class RowAction { kSelect, kClose, kPin, kMute };
  void ScheduleRefresh();
  void Refresh();
  void Invoke(base::WeakPtr<content::WebContents> contents, RowAction action);
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;
  void ExecuteMenuAction(uint64_t generation,
                         base::WeakPtr<content::WebContents> contents,
                         int action);

  struct Row {
    base::WeakPtr<content::WebContents> contents;
    raw_ptr<views::View> view;
    raw_ptr<FlyTabButton> select;
    raw_ptr<views::Button> pin;
    raw_ptr<views::Button> close;
    raw_ptr<views::Button> audio;
    gfx::ImageSkia favicon;
    bool favicon_initialized = false;
    bool pinned = false;
    bool muted = false;
    bool audible = false;
    base::CallbackListSubscription audible_subscription;
  };
  std::map<content::WebContents*, Row> tab_rows_;
  std::unique_ptr<SidebarTabMenu> context_menu_;
  uint64_t context_generation_ = 0;
  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  raw_ptr<TabStripModel> model_ = nullptr;
  raw_ptr<views::View> rows_ = nullptr;
  base::RepeatingClosure new_tab_;
  const bool compact_;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<TabSidebar> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_TAB_SIDEBAR_H_
