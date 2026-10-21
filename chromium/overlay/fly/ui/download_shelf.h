// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_DOWNLOAD_SHELF_H_
#define FLY_UI_DOWNLOAD_SHELF_H_

#include <map>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "ui/views/view.h"

class Profile;
namespace views {
class ScrollView;
}

namespace fly {
class DownloadCard;
// Electron's download shelf (downloads.js, #DownloadShelf): a 52px bar above
// the status bar that appears when a download starts in any workspace, with
// a card per download (progress, speed, cancel; open, show in folder and
// dismiss once complete), "clear completed" and close.
class DownloadShelf : public views::View,
                      public content::DownloadManager::Observer,
                      public download::DownloadItem::Observer {
  METADATA_HEADER(DownloadShelf, views::View)

 public:
  // `show_all` opens Fly's list of every workspace's downloads.
  explicit DownloadShelf(base::RepeatingClosure show_all);
  DownloadShelf(const DownloadShelf&) = delete;
  DownloadShelf& operator=(const DownloadShelf&) = delete;
  ~DownloadShelf() override;

  void AddProfile(Profile* profile);
  void Toggle();
  bool has_cards() const { return !cards_.empty(); }
  size_t card_count_for_testing() const { return cards_.size(); }
  views::View* card_for_testing(download::DownloadItem* item);

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;
  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

  void OnPaintBackground(gfx::Canvas* canvas) override;

 private:
  friend class DownloadCard;
  void Dismiss(download::DownloadItem* item);
  void ClearCompleted();
  void UpdateVisibility();

  base::RepeatingClosure show_all_;
  raw_ptr<views::View> cards_view_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  std::map<download::DownloadItem*, raw_ptr<DownloadCard>> cards_;
  base::ScopedMultiSourceObservation<content::DownloadManager,
                                     content::DownloadManager::Observer>
      managers_{this};
  base::ScopedMultiSourceObservation<download::DownloadItem,
                                     download::DownloadItem::Observer>
      items_{this};
};
}  // namespace fly

#endif  // FLY_UI_DOWNLOAD_SHELF_H_
