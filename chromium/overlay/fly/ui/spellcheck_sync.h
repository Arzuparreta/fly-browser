// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_SPELLCHECK_SYNC_H_
#define FLY_UI_SPELLCHECK_SYNC_H_

#include <map>
#include <memory>
#include <set>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/profiles/profile_observer.h"

class PrefChangeRegistrar;
class Profile;

namespace fly {
class PreferencesService;

// Spell checking is one choice for every workspace. Fly writes its choice into
// each workspace profile (ApplyProfilePreferences); a change made in one
// workspace elsewhere (the page's context menu, chrome://settings) becomes
// Fly's choice, which then reaches the other workspaces. Before Fly has a
// choice, each profile keeps Chromium's own.
class SpellcheckSync : public ProfileObserver {
 public:
  explicit SpellcheckSync(PreferencesService& preferences);
  ~SpellcheckSync() override;
  SpellcheckSync(const SpellcheckSync&) = delete;
  SpellcheckSync& operator=(const SpellcheckSync&) = delete;

  void AddProfile(Profile* profile);

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  // Waits for the end of the current task: Fly writes the dictionaries and the
  // switch one after the other.
  void OnProfileChanged(Profile* profile);
  void Compare(Profile* profile);

  const raw_ref<PreferencesService> preferences_;
  std::map<raw_ptr<Profile>, std::unique_ptr<PrefChangeRegistrar>> registrars_;
  std::set<raw_ptr<Profile>> pending_;
  base::ScopedMultiSourceObservation<Profile, ProfileObserver> observations_{
      this};
  base::WeakPtrFactory<SpellcheckSync> weak_factory_{this};
};
}  // namespace fly

#endif  // FLY_UI_SPELLCHECK_SYNC_H_
