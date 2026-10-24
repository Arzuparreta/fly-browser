// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_APP_FEATURE_DEFAULTS_H_
#define FLY_APP_FEATURE_DEFAULTS_H_

#include <string>
#include <string_view>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/strings/string_split.h"

namespace fly {

// Experiments of fieldtrial_testing_config.json that already migrated profile
// data written by earlier Fly releases, with the stage those releases ran:
// localStorage and IndexedDB in SQLite and the migrated search engine data.
inline constexpr std::string_view kKeptFeatures[] = {
    "DomStorageSqliteNewDatabases:DomStorageSqliteNewDatabasesStage/"
    "UseSqliteForNewDatabases",
    "IdbSqliteOnDiskRollout:stage/UseSqliteForNewStores",
    "PrepopulatedEnginesMigration",
    "ApplySearchEngineTypeMigration",
};
// Privacy Sandbox APIs that the config turned off and Chromium enables by
// default.
inline constexpr std::string_view kDisabledFeatures[] = {
    "BrowsingTopics", "ConversionMeasurement", "Fledge",
    "AdInterestGroupAPI", "SharedStorageAPI",
};

// Native Fly runs Chromium's default features instead of the experiments that
// testing/variations/fieldtrial_testing_config.json applies to unbranded
// builds; its README asks browsers other than Chrome to disable it. Those
// experiments cost memory, idle CPU and tab-switching time. Called by the
// browser process before it creates its feature list. A feature named on the
// command line (in --enable-features or --disable-features, with or without
// a study, parameters or a leading '*') keeps the state given there.
inline void ApplyFeatureDefaults(base::CommandLine* command_line) {
  // variations::switches::kDisableFieldTrialTestingConfig
  command_line->AppendSwitch("disable-field-trial-config");
  const auto named = [&](std::string_view feature) {
    for (const char* list :
         {switches::kEnableFeatures, switches::kDisableFeatures}) {
      const std::string value = command_line->GetSwitchValueASCII(list);
      for (std::string_view entry :
           base::SplitStringPiece(value, ",", base::TRIM_WHITESPACE,
                                  base::SPLIT_WANT_NONEMPTY)) {
        if (entry.starts_with('*')) {
          entry.remove_prefix(1);
        }
        if (entry.substr(0, entry.find_first_of(":<")) == feature) {
          return true;
        }
      }
    }
    return false;
  };
  const auto add = [&](const char* list, std::string_view entry) {
    if (named(entry.substr(0, entry.find(':')))) {
      return;
    }
    std::string value = command_line->GetSwitchValueASCII(list);
    if (!value.empty()) {
      value += ',';
    }
    value += entry;
    command_line->AppendSwitchASCII(list, value);
  };
  for (std::string_view feature : kKeptFeatures) {
    add(switches::kEnableFeatures, feature);
  }
  for (std::string_view feature : kDisabledFeatures) {
    add(switches::kDisableFeatures, feature);
  }
}

}  // namespace fly

#endif  // FLY_APP_FEATURE_DEFAULTS_H_
