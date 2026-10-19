// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_SESSIONS_TAB_IDENTITY_H_
#define FLY_SESSIONS_TAB_IDENTITY_H_

#include <map>
#include <string>

#include "content/public/browser/web_contents_user_data.h"
#include "content/public/browser/web_contents_observer.h"

namespace fly {
class TabMoveTransaction;
// Product identity, independent of native extension/session IDs. Persisted by
// Chromium's session command store and closed-tab restore service, never in a
// competing URL/session file. Private tabs have runtime identities only.
class TabIdentity : public content::WebContentsUserData<TabIdentity>,
                    public content::WebContentsObserver {
 public:
  static constexpr char kExtraDataKey[] = "fly.tab_id";
  static const std::string& Get(content::WebContents* contents);
  static bool CanPersist(content::WebContents* contents);
  static void Populate(content::WebContents* contents,
                       std::map<std::string, std::string>* extra_data);
  // Call on new restored contents before insertion. Invalid/colliding IDs get
  // a fresh identity; an already published identity cannot be reassigned.
  static void Restore(content::WebContents* contents,
                      const std::map<std::string, std::string>& extra_data);
  ~TabIdentity() override;
  void AboutToBeDiscarded(content::WebContents* new_contents) override;

 private:
  friend class content::WebContentsUserData<TabIdentity>;
  friend class TabMoveTransaction;
  // Only the native move coordinator may hand off identity immediately before
  // source detachment. The new destination has never been published or loaded;
  // this transfers metadata, never contents, renderers or BrowserContexts.
  static bool PrepareProfileMove(content::WebContents* source,
                                 content::WebContents* destination);
  TabIdentity(content::WebContents* contents, const std::string& restored_id);
  std::string id_;
  // During replacement discard both contents briefly represent one logical tab.
  // Only the replacement releases the live-ID reservation on destruction.
  bool owns_id_ = true;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace fly
#endif  // FLY_SESSIONS_TAB_IDENTITY_H_
