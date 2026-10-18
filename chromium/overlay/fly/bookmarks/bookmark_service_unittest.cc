// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/bookmarks/bookmark_service.h"

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class BookmarkServiceTest : public testing::Test {
 protected:
  BookmarkServiceTest() : service_(prefs_) {
    BookmarkService::RegisterLocalStatePrefs(prefs_.registry());
  }
  base::Time Added() const { return base::Time::FromMillisecondsSinceUnixEpoch(1000); }
  TestingPrefServiceSimple prefs_;
  BookmarkService service_;
};
TEST_F(BookmarkServiceTest, EmptyReadAndRejectedURLsDoNotWrite) {
  ASSERT_TRUE(service_.Read());
  EXPECT_TRUE(service_.Read()->empty());
  EXPECT_TRUE(service_.CanEdit());
  for (const auto url : {"about:blank", "javascript:alert(1)", "data:text/html,test",
                         "invalid", "https://", "devtools://inspect"}) {
    EXPECT_FALSE(service_.CanAdd(url));
    EXPECT_FALSE(service_.Add(url, "rejected", Added()));
  }
  EXPECT_TRUE(service_.CanAdd("https://valid.test/"));
  EXPECT_FALSE(service_.Add("https://one.test/", "title", base::Time()));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kBookmarksPref));
}
TEST_F(BookmarkServiceTest, NewestFirstAndExactDuplicatesRetainOriginalMetadata) {
  ASSERT_TRUE(service_.Add("https://one.test/", "Español 日本語", Added()));
  ASSERT_TRUE(service_.Add("https://two.test/path", "", Added()));
  const auto before = prefs_.GetDict(kBookmarksPref).Clone();
  ASSERT_TRUE(service_.Add("https://one.test/", "renamed?", Added() + base::Days(1)));
  EXPECT_EQ(before, prefs_.GetDict(kBookmarksPref));
  BookmarkService restarted(prefs_);
  const auto items = restarted.Read();
  ASSERT_TRUE(items);
  ASSERT_EQ(2u, items->size());
  EXPECT_EQ("https://two.test/path", (*items)[0].title);
  EXPECT_EQ("Español 日本語", (*items)[1].title);
  EXPECT_EQ(1000, (*items)[1].timestamp_ms);
  EXPECT_TRUE(restarted.Contains("https://one.test/"));
  EXPECT_FALSE(restarted.Contains("https://one.test"));
}
TEST_F(BookmarkServiceTest, WebOriginsKeepSchemesPortsAndSubdomainsDistinct) {
  for (const auto url : {"https://site.test/path", "https://site.test/another",
                         "http://site.test/", "https://sub.site.test/",
                         "https://site.test:8443/", "file:///tmp/bookmark.pdf"})
    ASSERT_TRUE(service_.Add(url, url, Added()));
  const auto origins = service_.WebOrigins();
  ASSERT_TRUE(origins);
  EXPECT_EQ((std::set<std::string>{"https://site.test", "http://site.test",
      "https://sub.site.test", "https://site.test:8443"}), *origins);
}
TEST_F(BookmarkServiceTest, RemovePreservesOrderingAndUnknownImportMetadata) {
  ASSERT_TRUE(service_.Add("https://one.test/", "One", Added()));
  ASSERT_TRUE(service_.Add("https://two.test/", "Two", Added()));
  auto stored = prefs_.GetDict(kBookmarksPref).Clone();
  stored.Set("import", "preserve provenance");
  stored.FindList("items")->front().GetDict().Set("future", "preserve row");
  prefs_.SetDict(kBookmarksPref, std::move(stored));
  ASSERT_TRUE(service_.Remove("https://one.test/"));
  EXPECT_EQ(1u, service_.Read()->size());
  const auto before = prefs_.GetDict(kBookmarksPref).Clone();
  EXPECT_FALSE(service_.Remove("https://one.test/"));
  EXPECT_EQ(before, prefs_.GetDict(kBookmarksPref));
  ASSERT_TRUE(service_.Add("https://three.test/", "Three", Added()));
  const auto& after = prefs_.GetDict(kBookmarksPref);
  EXPECT_EQ("preserve provenance", *after.FindString("import"));
  EXPECT_EQ("preserve row", *after.FindList("items")->back().GetDict().FindString("future"));
}
TEST_F(BookmarkServiceTest, InvalidAndFutureStoresPreserveRawData) {
  for (const auto& invalid : {base::Value("corrupt"), base::Value(false),
      base::Value(base::DictValue().Set("schema", 2).Set("items", base::ListValue())),
      base::Value(base::DictValue().Set("schema", 1).Set("items", "corrupt")),
      base::Value(base::DictValue().Set("schema", 1).Set("items", base::ListValue()
          .Append(base::DictValue().Set("url", "https://one.test/")
              .Set("title", "one").Set("timestamp", -1))))}) {
    prefs_.SetUserPref(kBookmarksPref, invalid.Clone());
    EXPECT_FALSE(service_.Read());
    EXPECT_FALSE(service_.WebOrigins());
    EXPECT_FALSE(service_.CanEdit());
    EXPECT_FALSE(service_.Add("https://valid.test/", "Valid", Added()));
    EXPECT_FALSE(service_.Remove("https://one.test/"));
    ASSERT_TRUE(prefs_.GetUserPref(kBookmarksPref));
    EXPECT_EQ(invalid, *prefs_.GetUserPref(kBookmarksPref));
  }
}
TEST_F(BookmarkServiceTest, ManagedAndRecommendedValuesRetainNativePrecedence) {
  ASSERT_TRUE(service_.Add("https://one.test/", "One", Added()));
  const auto policy = prefs_.GetDict(kBookmarksPref).Clone();
  prefs_.ClearPref(kBookmarksPref);
  prefs_.SetManagedPref(kBookmarksPref, base::Value(policy.Clone()));
  EXPECT_TRUE(service_.Contains("https://one.test/"));
  EXPECT_FALSE(service_.CanEdit());
  EXPECT_FALSE(service_.CanAdd("https://two.test/"));
  EXPECT_FALSE(service_.Add("https://two.test/", "Two", Added()));
  EXPECT_FALSE(service_.Remove("https://one.test/"));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kBookmarksPref));
  prefs_.RemoveManagedPref(kBookmarksPref);
  prefs_.SetRecommendedPref(kBookmarksPref, base::Value(policy.Clone()));
  prefs_.SetUserPref(kBookmarksPref, base::Value("corrupt"));
  EXPECT_FALSE(service_.Read());
  EXPECT_FALSE(service_.Add("https://two.test/", "Two", Added()));
  EXPECT_EQ("corrupt", prefs_.GetUserPref(kBookmarksPref)->GetString());
}
TEST_F(BookmarkServiceTest, IntegerJSONTimestampAndExplicitEmptyUserStoreAreValid) {
  prefs_.SetDict(kBookmarksPref, base::DictValue().Set("schema", 1)
      .Set("items", base::ListValue().Append(base::DictValue()
          .Set("url", "https://one.test/").Set("title", "One")
          .Set("timestamp", 1000))));
  ASSERT_TRUE(service_.Read());
  EXPECT_EQ(1000, service_.Read()->front().timestamp_ms);
  prefs_.SetDict(kBookmarksPref, base::DictValue());
  EXPECT_TRUE(service_.CanEdit());
  EXPECT_TRUE(service_.Add("https://two.test/", "Two", Added()));
}
}  // namespace
}  // namespace fly
