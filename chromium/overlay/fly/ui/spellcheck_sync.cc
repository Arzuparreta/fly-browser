// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/spellcheck_sync.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/spellcheck/browser/pref_names.h"
#include "fly/preferences/preferences_service.h"
#include "fly/preferences/spellcheck_languages.h"
#include "fly/ui/fly_actions.h"

namespace fly {

SpellcheckSync::SpellcheckSync(PreferencesService& preferences)
    : preferences_(preferences) {}
SpellcheckSync::~SpellcheckSync() = default;

void SpellcheckSync::AddProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() || registrars_.contains(profile))
    return;
  auto registrar = std::make_unique<PrefChangeRegistrar>();
  registrar->Init(profile->GetPrefs());
  const auto changed = base::BindRepeating(&SpellcheckSync::OnProfileChanged,
                                           base::Unretained(this), profile);
  registrar->Add(spellcheck::prefs::kSpellCheckEnable, changed);
  registrar->Add(spellcheck::prefs::kSpellCheckDictionaries, changed);
  registrars_[profile] = std::move(registrar);
  observations_.AddObservation(profile);
}

void SpellcheckSync::OnProfileWillBeDestroyed(Profile* profile) {
  registrars_.erase(profile);
  pending_.erase(profile);
  observations_.RemoveObservation(profile);
}

void SpellcheckSync::OnProfileChanged(Profile* profile) {
  if (!pending_.insert(profile).second)
    return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SpellcheckSync::Compare,
                                weak_factory_.GetWeakPtr(), profile));
}

void SpellcheckSync::Compare(Profile* profile) {
  // A destroyed profile left `pending_`.
  if (!pending_.erase(profile))
    return;
  const auto choice = preferences_->ReadSpellcheck();
  if (!choice)
    return;
  const auto* prefs = profile->GetPrefs();
  const bool enabled = prefs->GetBoolean(spellcheck::prefs::kSpellCheckEnable);
  std::vector<std::string> dictionaries;
  for (const auto& value :
       prefs->GetList(spellcheck::prefs::kSpellCheckDictionaries))
    if (value.is_string())
      dictionaries.push_back(value.GetString());
  if (enabled == choice->enabled && dictionaries == choice->languages)
    return;
  const SpellcheckPreferences changed{
      .enabled = enabled,
      .languages = NormalizeSpellcheckLanguages(dictionaries)};
  // Storing the change applies it to every workspace. Languages Fly does not
  // offer, or more than it allows, are written over in this one.
  if (changed == *choice || !preferences_->SetSpellcheck(changed))
    ApplyProfilePreferences(profile, *preferences_);
}

}  // namespace fly
