// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_TAB_MOVE_TRANSACTION_H_
#define FLY_UI_TAB_MOVE_TRANSACTION_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;
class ScopedProfileKeepAlive;
namespace fly {
class ScopedTabMoveGate;

// Prepares the destination profile without navigating it. A scoped native-close
// gate commits a fresh destination WebContents only after native beforeunload
// decisions, immediately before source detachment. No live contents, opener,
// referrer, POST body, SiteInstance or storage crosses profiles.
class TabMoveTransaction : public content::WebContentsObserver,
                           public TabStripModelObserver {
 public:
  using Done = base::OnceCallback<void(bool,
                                       base::WeakPtr<BrowserWindowInterface>,
                                       base::WeakPtr<content::WebContents>)>;
  TabMoveTransaction(BrowserWindowInterface* source,
                     content::WebContents* contents,
                     base::FilePath destination_path,
                     base::RepeatingCallback<bool(bool)> validate,
                     Done done);
  ~TabMoveTransaction() override;
  static bool CanMoveUrl(const GURL& url);
  void Start();
  void BeforeUnloadFired(bool proceed) override;
  void BeforeUnloadDialogCancelled() override;
  void DidStartNavigation(content::NavigationHandle* handle) override;
  void WebContentsDestroyed() override;
  void OnTabCloseCancelled(const tabs::TabInterface* tab) override;
  void OnTabStripModelDestroyed(TabStripModel* model) override;

 private:
  void OnProfileReady(Profile* original);
  void OnBrowserReady(BrowserWindowInterface* destination);
  bool SourceValid(bool require_selection) const;
  bool DestinationValid() const;
  bool ValidateClose();
  bool Commit();
  void Finish(bool success);
  void ScheduleFinish(bool success);
  void CloseUnusedDestination();

  base::WeakPtr<BrowserWindowInterface> source_browser_;
  base::WeakPtr<content::WebContents> source_;
  base::WeakPtr<BrowserWindowInterface> destination_;
  base::WeakPtr<content::WebContents> moved_;
  const base::FilePath destination_path_;
  const GURL url_;
  const bool private_;
  base::RepeatingCallback<bool(bool)> validate_;
  Done done_;
  std::unique_ptr<ScopedProfileKeepAlive> destination_keep_alive_;
  std::unique_ptr<ScopedTabMoveGate> gate_;
  bool created_destination_ = false;
  bool close_requested_ = false;
  bool aborted_ = false;
  bool committed_ = false;
  bool finish_pending_ = false;
  base::WeakPtrFactory<TabMoveTransaction> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_TAB_MOVE_TRANSACTION_H_
