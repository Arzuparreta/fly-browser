// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_STATUS_BAR_H_
#define FLY_UI_STATUS_BAR_H_

#include <memory>
#include <string>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class BrowserWindowInterface;
namespace views {
class Label;
}
namespace fly {
// Read-only presentation of native page/profile state. Link targets arrive from
// the browser delegate; page code never receives a privileged UI binding.
class StatusBar : public views::View,
                  public TabStripModelObserver,
                  public content::WebContentsObserver {
 public:
  METADATA_HEADER(StatusBar, views::View)

 public:
  StatusBar();
  ~StatusBar() override;
  void SetBrowser(BrowserWindowInterface* browser, std::u16string workspace);
  void SetTargetURL(content::WebContents* source, const GURL& url);
  views::Label* mode_label() const { return mode_; }
  views::Label* location_for_testing() const { return location_; }
  views::Label* security_for_testing() const { return security_; }
  views::Label* workspace_for_testing() const { return workspace_; }

  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;
  void OnTabStripModelDestroyed(TabStripModel*) override;
  void DidFinishNavigation(content::NavigationHandle*) override;
  void DidChangeVisibleSecurityState() override;
  void WebContentsDestroyed() override;

 private:
  class TargetObserver;
  void ClearTarget();
  void SelectContents();
  void Update();
  base::WeakPtr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripModel> model_ = nullptr;
  raw_ptr<views::Label> mode_;
  raw_ptr<views::Label> location_;
  raw_ptr<views::Label> security_;
  raw_ptr<views::Label> workspace_;
  GURL target_;
  std::unique_ptr<TargetObserver> target_observer_;
};
}  // namespace fly
#endif
