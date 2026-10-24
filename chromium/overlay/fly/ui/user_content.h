// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_USER_CONTENT_H_
#define FLY_UI_USER_CONTENT_H_

#include <memory>
#include <set>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/threading/sequence_bound.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile_observer.h"

class Profile;

namespace fly {
// User scripts and styles (<user data>/scripts/*.user.js and
// styles/*.user.css).
//
// Fly generates the "Fly User Content" extension in
// <user data>/fly-user-content: its fixed worker (fly_resources/user_content)
// plus content.js holding the current files. The worker parses metadata,
// registers chrome.userScripts and serves the GM_* bridge. Edits in the folders regenerate content.js and
// reload the extension in every workspace.
class UserContentService : public ProfileObserver {
 public:
  inline static constexpr char kExtensionId[] =
      "ipdcgicjjgglkiekhaoniobgomajoeof";
  explicit UserContentService(
      base::RepeatingCallback<void(std::u16string)> announce);
  ~UserContentService() override;
  UserContentService(const UserContentService&) = delete;
  UserContentService& operator=(const UserContentService&) = delete;

  void AddProfile(Profile* profile);
  // Rereads the folders now (reload-user-content).
  void Reload();
  // The email_handler preference for the Share menu ("system" or a compose URL
  // with %s/%b); regenerates the extension when it changes.
  void SetEmailHandler(std::string handler);
  size_t script_count() const { return scripts_; }
  size_t style_count() const { return styles_; }

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  class Watcher;
  // When a regeneration reports itself in the footer.
  enum class Announce { kNever, kIfChanged, kAlways };
  struct Generated {
    bool ok = false;
    base::FilePath directory;
    base::DictValue manifest;
    std::string version;
    size_t scripts = 0;
    size_t styles = 0;
  };
  static Generated Generate(std::string email_handler);
  void OnGenerated(Announce announce, Generated generated);
  void Load(Profile* profile);
  void Regenerate(Announce announce);

  base::RepeatingCallback<void(std::u16string)> announce_;
  base::SequenceBound<Watcher> watcher_;
  std::optional<Generated> current_;
  bool generating_ = false;
  std::string email_handler_ = "system";
  bool pending_ = false;
  Announce pending_announce_ = Announce::kNever;
  size_t scripts_ = 0;
  size_t styles_ = 0;
  std::set<raw_ptr<Profile>> profiles_;
  base::ScopedMultiSourceObservation<Profile, ProfileObserver> profile_obs_{
      this};
  base::WeakPtrFactory<UserContentService> weak_factory_{this};
};
}  // namespace fly

#endif  // FLY_UI_USER_CONTENT_H_
