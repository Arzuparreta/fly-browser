// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/workspaces/workspace_service.h"

#include "base/task/single_thread_task_executor.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class WorkspaceServiceTest : public testing::Test {
 protected:
  WorkspaceServiceTest() : service_(prefs_) {
    WorkspaceService::RegisterLocalStatePrefs(prefs_.registry());
  }
  base::SingleThreadTaskExecutor task_executor_;
  TestingPrefServiceSimple prefs_;
  WorkspaceService service_;
};

TEST_F(WorkspaceServiceTest, EmptyReadDoesNotWriteBeforeMigration) {
  auto catalog = service_.Read();
  ASSERT_TRUE(catalog);
  EXPECT_TRUE(catalog->items.empty());
  EXPECT_TRUE(catalog->active_id.empty());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kWorkspacesPref));
}

TEST_F(WorkspaceServiceTest, RenameAndRestartKeepStorageIdentity) {
  const auto item = service_.Add("Trabajo", "Profile 1");
  ASSERT_TRUE(item);
  ASSERT_TRUE(service_.Rename(item->id, "Diseño: español / 日本語"));
  TestingPrefServiceSimple restarted_prefs;
  WorkspaceService::RegisterLocalStatePrefs(restarted_prefs.registry());
  restarted_prefs.SetDict(kWorkspacesPref,
                          prefs_.GetDict(kWorkspacesPref).Clone());
  WorkspaceService restarted(restarted_prefs);
  const auto catalog = restarted.Read();
  ASSERT_TRUE(catalog);
  ASSERT_EQ(1u, catalog->items.size());
  EXPECT_EQ(item->id, catalog->items[0].id);
  EXPECT_EQ("Profile 1", catalog->items[0].profile_directory);
  EXPECT_EQ("Diseño: español / 日本語", catalog->items[0].name);
  EXPECT_EQ(item->id, catalog->active_id);
}

TEST_F(WorkspaceServiceTest, DistinctNamesNeverShareProfiles) {
  const auto first = service_.Add("work", "Profile 1");
  const auto second = service_.Add("WORK", "Profile 2");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(first->id, second->id);
  EXPECT_FALSE(service_.Add("third", "profile 1"));
  EXPECT_FALSE(service_.Add("work", "Profile 3"));
  EXPECT_FALSE(service_.Rename(second->id, "work"));
  EXPECT_TRUE(service_.Rename(second->id, "WORK"));
}

TEST_F(WorkspaceServiceTest, DeleteAndRecreateNeverReviveOldCookies) {
  const auto first = service_.Add("work", "Profile 1");
  const auto second = service_.Add("personal", "Profile 2");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(service_.Remove(second->id));
  EXPECT_EQ(first->id, service_.Read()->active_id);
  EXPECT_FALSE(service_.Add("personal", "Profile 2"));
  EXPECT_FALSE(service_.Add("new", "PROFILE 2"));
  const auto recreated = service_.Add("personal", "Profile 3");
  ASSERT_TRUE(recreated);
  EXPECT_NE(second->id, recreated->id);
  ASSERT_TRUE(service_.Remove(first->id));
  ASSERT_TRUE(service_.Remove(recreated->id));
  EXPECT_TRUE(service_.Read()->active_id.empty());
  EXPECT_TRUE(service_.Read()->items.empty());
}

TEST_F(WorkspaceServiceTest, RetirementQueriesPreserveMetadataAndPolicy) {
  const auto first = service_.Add("work", "Default");
  const auto second = service_.Add("personal", "Profile 2");
  ASSERT_TRUE(first && second);
  ASSERT_TRUE(service_.Remove(second->id));
  const auto before = prefs_.GetDict(kWorkspacesPref).Clone();
  EXPECT_EQ(true, service_.IsProfileRetired("PROFILE 2"));
  EXPECT_EQ(false, service_.IsProfileRetired("Default"));
  EXPECT_EQ(false, service_.IsProfileRetired("Profile 3"));
  EXPECT_FALSE(service_.IsProfileRetired("../Profile 2"));
  EXPECT_EQ(before, prefs_.GetDict(kWorkspacesPref));
  prefs_.ClearPref(kWorkspacesPref);
  prefs_.SetManagedPref(kWorkspacesPref, base::Value(before.Clone()));
  EXPECT_EQ(true, service_.IsProfileRetired("Profile 2"));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kWorkspacesPref));
  auto future = before.Clone();
  future.Set("schema", 1000);
  prefs_.SetManagedPref(kWorkspacesPref, base::Value(future.Clone()));
  EXPECT_FALSE(service_.IsProfileRetired("Profile 2"));
  EXPECT_EQ(future, prefs_.GetDict(kWorkspacesPref));
}

TEST_F(WorkspaceServiceTest, PendingRetirementIsScopedToOneExactProfilePath) {
  const auto first = base::FilePath::FromASCII("root").AppendASCII("Profile 1");
  const auto second =
      base::FilePath::FromASCII("other").AppendASCII("Profile 1");
  EXPECT_FALSE(IsWorkspaceRetiring(first));
  {
    ScopedWorkspaceRetirement pending(first);
    EXPECT_TRUE(IsWorkspaceRetiring(first));
    EXPECT_FALSE(IsWorkspaceRetiring(second));
    EXPECT_EQ(nullptr, prefs_.GetUserPref(kWorkspacesPref));
  }
  EXPECT_FALSE(IsWorkspaceRetiring(first));
}

TEST_F(WorkspaceServiceTest, RejectPathsAndInvalidNamesWithoutChangingData) {
  ASSERT_TRUE(service_.Add("default", "Default"));
  const auto before = prefs_.GetDict(kWorkspacesPref).Clone();
  for (const char* directory :
       {"", "../Profile 1", "/tmp/profile", "a/b", "a\\b", "C:profile", ".",
        "..", " Profile", "Profile ", "CON", "nul", "COM1", "lpt9"}) {
    EXPECT_FALSE(service_.Add("second", directory)) << directory;
  }
  EXPECT_FALSE(service_.Add("", "Profile 2"));
  EXPECT_FALSE(service_.Add(std::string("nul\0name", 8), "Profile 2"));
  EXPECT_FALSE(service_.Add(std::string("\xff", 1), "Profile 2"));
  EXPECT_FALSE(service_.Add(std::string(4097, 'a'), "Profile 2"));
  EXPECT_EQ(before, prefs_.GetDict(kWorkspacesPref));
}

TEST_F(WorkspaceServiceTest, ReorderIsAnAtomicPermutation) {
  const auto first = service_.Add("one", "Profile 1");
  const auto second = service_.Add("two", "Profile 2");
  const auto third = service_.Add("three", "Profile 3");
  ASSERT_TRUE(first && second && third);
  const auto before = prefs_.GetDict(kWorkspacesPref).Clone();
  EXPECT_FALSE(service_.Reorder({first->id, first->id, third->id}));
  EXPECT_FALSE(service_.Reorder({first->id, second->id}));
  EXPECT_FALSE(service_.Reorder({first->id, second->id, "missing"}));
  EXPECT_EQ(before, prefs_.GetDict(kWorkspacesPref));
  EXPECT_TRUE(service_.Reorder({third->id, first->id, second->id}));
  const auto catalog = service_.Read();
  ASSERT_TRUE(catalog);
  EXPECT_EQ(third->id, catalog->items[0].id);
  EXPECT_EQ(first->id, catalog->items[1].id);
  EXPECT_EQ(second->id, catalog->items[2].id);
  EXPECT_EQ(third->id, catalog->active_id);
}

TEST_F(WorkspaceServiceTest, UnknownIdsCannotMutateTheCatalog) {
  const auto first = service_.Add("one", "Default");
  ASSERT_TRUE(first);
  const auto before = prefs_.GetDict(kWorkspacesPref).Clone();
  EXPECT_FALSE(service_.Rename("missing", "new"));
  EXPECT_FALSE(service_.Activate("missing"));
  EXPECT_FALSE(service_.Remove("missing"));
  EXPECT_EQ(before, prefs_.GetDict(kWorkspacesPref));
}

TEST_F(WorkspaceServiceTest, FutureSchemaIsPreservedForRecovery) {
  auto future =
      base::DictValue().Set("schema", 1000).Set("opaque", "future data");
  prefs_.SetDict(kWorkspacesPref, future.Clone());
  EXPECT_FALSE(service_.Read());
  EXPECT_FALSE(service_.Add("new", "Default"));
  EXPECT_FALSE(service_.Rename("missing", "new"));
  EXPECT_FALSE(service_.Remove("missing"));
  EXPECT_FALSE(service_.Reorder({}));
  EXPECT_EQ(future, prefs_.GetDict(kWorkspacesPref));
}

TEST_F(WorkspaceServiceTest, MalformedCatalogIsNeverReinitialized) {
  ASSERT_TRUE(service_.Add("one", "Default"));
  const auto valid = prefs_.GetDict(kWorkspacesPref).Clone();
  std::vector<base::DictValue> corrupt;
  corrupt.push_back(valid.Clone());
  corrupt.back().Set("active_id", "missing");
  corrupt.push_back(valid.Clone());
  corrupt.back().FindList("items")->Append(base::DictValue());
  corrupt.push_back(valid.Clone());
  corrupt.back().FindList("items")->front().GetDict().Set("id", "not-a-uuid");
  corrupt.push_back(valid.Clone());
  corrupt.back().FindList("retired_profiles")->Append("Default");
  corrupt.push_back(valid.Clone());
  corrupt.back().Set("items", 12);
  for (const auto& invalid : corrupt) {
    prefs_.SetDict(kWorkspacesPref, invalid.Clone());
    EXPECT_FALSE(service_.Read());
    EXPECT_FALSE(service_.Add("recovery must be explicit", "Profile 9"));
    EXPECT_EQ(invalid, prefs_.GetDict(kWorkspacesPref));
  }
}

TEST_F(WorkspaceServiceTest, EditsPreserveUnknownMetadata) {
  const auto first = service_.Add("one", "Default");
  ASSERT_TRUE(first);
  auto extended = prefs_.GetDict(kWorkspacesPref).Clone();
  extended.Set("migration", base::DictValue().Set("transaction", "keep"));
  extended.FindList("items")->front().GetDict().Set("layout", "retain");
  prefs_.SetDict(kWorkspacesPref, std::move(extended));
  EXPECT_TRUE(service_.Rename(first->id, "renamed"));
  EXPECT_TRUE(service_.Reorder({first->id}));
  EXPECT_TRUE(service_.Activate(first->id));
  const auto& after = prefs_.GetDict(kWorkspacesPref);
  EXPECT_EQ("keep", *after.FindDict("migration")->FindString("transaction"));
  EXPECT_EQ("retain",
            *after.FindList("items")->front().GetDict().FindString("layout"));
}
TEST_F(WorkspaceServiceTest, WrongTypedUserValuesCannotAllocateOrReplaceCatalog) {
  for (const auto& invalid : {base::Value("corrupt"), base::Value(false),
                              base::Value(base::ListValue())}) {
    prefs_.SetUserPref(kWorkspacesPref, invalid.Clone());
    EXPECT_FALSE(service_.Read());
    EXPECT_FALSE(service_.CanEdit());
    EXPECT_FALSE(service_.Add("work", "Profile 1"));
    EXPECT_FALSE(service_.Rename("missing", "new name"));
    EXPECT_FALSE(service_.Activate("missing"));
    EXPECT_FALSE(service_.Remove("missing"));
    EXPECT_FALSE(service_.Reorder({}));
    ASSERT_TRUE(prefs_.GetUserPref(kWorkspacesPref));
    EXPECT_EQ(invalid, *prefs_.GetUserPref(kWorkspacesPref));
    prefs_.SetRecommendedPref(kWorkspacesPref, base::Value(base::DictValue()));
    EXPECT_FALSE(service_.Read());
    EXPECT_FALSE(service_.CanEdit());
    EXPECT_FALSE(service_.Add("work", "Profile 2"));
    EXPECT_EQ(invalid, *prefs_.GetUserPref(kWorkspacesPref));
    prefs_.RemoveRecommendedPref(kWorkspacesPref);
  }
  prefs_.SetDict(kWorkspacesPref, base::DictValue());
  EXPECT_TRUE(service_.Read());
  EXPECT_TRUE(service_.CanEdit());
  EXPECT_TRUE(service_.Add("work", "Profile 3"));
}
TEST_F(WorkspaceServiceTest, ManagedCatalogIsReadableWithoutCreatingUserWrites) {
  const auto item = service_.Add("work", "Profile 1");
  ASSERT_TRUE(item);
  const auto policy = prefs_.GetDict(kWorkspacesPref).Clone();
  prefs_.ClearPref(kWorkspacesPref);
  prefs_.SetManagedPref(kWorkspacesPref, base::Value(policy.Clone()));
  ASSERT_TRUE(service_.Read());
  EXPECT_FALSE(service_.CanEdit());
  EXPECT_EQ(*item, service_.Read()->items.front());
  EXPECT_FALSE(service_.Add("second", "Profile 2"));
  EXPECT_FALSE(service_.Rename(item->id, "changed"));
  EXPECT_FALSE(service_.Activate(item->id));
  EXPECT_FALSE(service_.Remove(item->id));
  EXPECT_FALSE(service_.Reorder({item->id}));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kWorkspacesPref));
  EXPECT_EQ(policy, prefs_.GetDict(kWorkspacesPref));
}
}  // namespace
}  // namespace fly
