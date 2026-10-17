// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_TAB_SIDEBAR_H_
#define FLY_UI_TAB_SIDEBAR_H_

#include <map>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
namespace content {
class WebContents;
}
namespace views {
class MdTextButton;
}

namespace fly {
// A projection of the selected profile's upstream tab model. Rows hold weak
// WebContents identities, never a cached tab index or an extension's session
// ID.
class TabSidebar : public views::View, public TabStripModelObserver {
 public:
  METADATA_HEADER(TabSidebar, views::View)

 public:
  explicit TabSidebar(base::RepeatingClosure new_tab);
  ~TabSidebar() override;
  void SetBrowser(BrowserWindowInterface* browser);
  void FocusActiveTab();
  size_t row_count_for_testing() const;
  views::View* row_for_testing(content::WebContents* contents) const;

  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnTabStripModelDestroyed(TabStripModel* model) override;

 private:
  enum class RowAction { kSelect, kClose, kPin };
  void ScheduleRefresh();
  void Refresh();
  void Invoke(base::WeakPtr<content::WebContents> contents, RowAction action);

  struct Row {
    base::WeakPtr<content::WebContents> contents;
    raw_ptr<views::View> view;
    raw_ptr<views::MdTextButton> select;
    raw_ptr<views::MdTextButton> pin;
    raw_ptr<views::MdTextButton> close;
  };
  std::map<content::WebContents*, Row> tab_rows_;
  raw_ptr<BrowserWindowInterface> browser_ = nullptr;
  raw_ptr<TabStripModel> model_ = nullptr;
  raw_ptr<views::View> rows_ = nullptr;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<TabSidebar> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_TAB_SIDEBAR_H_
