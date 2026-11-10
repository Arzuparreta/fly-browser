// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/app/feature_defaults.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/strings/string_split.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {

std::vector<std::string> List(const base::CommandLine& command_line,
                              const char* name) {
  return base::SplitString(command_line.GetSwitchValueASCII(name), ",",
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

TEST(FeatureDefaultsTest, DisablesTheTestingConfigAndAddsFlyFeatures) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  ApplyFeatureDefaults(&command_line);
  EXPECT_TRUE(command_line.HasSwitch("disable-field-trial-config"));
  std::vector<std::string> enabled(std::begin(kKeptFeatures),
                                   std::end(kKeptFeatures));
  enabled.insert(enabled.end(), std::begin(kEnabledFeatures),
                 std::end(kEnabledFeatures));
  EXPECT_EQ(enabled, List(command_line, switches::kEnableFeatures));
  // The first page of a new profile depends on it (feature_defaults.h).
  EXPECT_TRUE(std::ranges::contains(enabled, "ForceWebRequestProxyForTest"));
  EXPECT_EQ(std::vector<std::string>(std::begin(kDisabledFeatures),
                                     std::end(kDisabledFeatures)),
            List(command_line, switches::kDisableFeatures));
}

TEST(FeatureDefaultsTest, KeepsFeaturesNamedOnTheCommandLine) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(
      switches::kEnableFeatures,
      "Other, *SharedStorageAPI,Fledge<Study,BrowsingTopics:a/b");
  command_line.AppendSwitchASCII(switches::kDisableFeatures,
                                 "DomStorageSqliteNewDatabases");
  ApplyFeatureDefaults(&command_line);
  const auto enabled = List(command_line, switches::kEnableFeatures);
  const auto disabled = List(command_line, switches::kDisableFeatures);
  EXPECT_EQ((std::vector<std::string>{
                "Other", "*SharedStorageAPI", "Fledge<Study",
                "BrowsingTopics:a/b",
                "IdbSqliteOnDiskRollout:stage/UseSqliteForNewStores",
                "PrepopulatedEnginesMigration",
                "ApplySearchEngineTypeMigration",
                "ForceWebRequestProxyForTest"}),
            enabled);
  EXPECT_EQ((std::vector<std::string>{"DomStorageSqliteNewDatabases",
                                      "ConversionMeasurement",
                                      "AdInterestGroupAPI"}),
            disabled);
}

}  // namespace
}  // namespace fly
