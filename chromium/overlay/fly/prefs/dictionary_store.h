// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_PREFS_DICTIONARY_STORE_H_
#define FLY_PREFS_DICTIONARY_STORE_H_

#include "base/values.h"
#include "components/prefs/pref_service.h"

namespace fly {
// Typed reads can silently fall back when a persisted user value has the wrong
// type. Native store provenance is presence-based; reject that fallback instead
// of treating damaged data as an editable empty dictionary. Do not use the typed
// user getter, which can diagnose a type mismatch fatally. Explicit valid user
// dictionaries, including empty ones, keep their own effective value pointer.
// Managed values retain native precedence and may be read; callers must also
// check IsUserModifiable before writing.
inline const base::DictValue* ReadDictionaryStore(const PrefService& prefs,
                                                 const char* name) {
  const auto* pref = prefs.FindPreference(name);
  if (!pref)
    return nullptr;
  const auto* effective = pref->GetValue();
  if (pref->HasUserSetting() && pref->IsUserControlled() &&
      (effective == prefs.GetDefaultPrefValue(name) ||
       effective == pref->GetRecommendedValue()))
    return nullptr;
  return effective ? effective->GetIfDict() : nullptr;
}
}  // namespace fly
#endif  // FLY_PREFS_DICTIONARY_STORE_H_
