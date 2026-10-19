// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/bookmarks/frequent_sites_service.h"

#include "components/prefs/testing_pref_service.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/preferences/preferences_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class FrequentSitesServiceTest : public testing::Test {
 protected:
  FrequentSitesServiceTest()
      : sites_(prefs_), bookmarks_(prefs_), settings_(prefs_) {
    BookmarkService::RegisterLocalStatePrefs(prefs_.registry());
    PreferencesService::RegisterLocalStatePrefs(prefs_.registry());
  }
  const base::Time now_ = base::Time::UnixEpoch() + base::Days(1000);
  TestingPrefServiceSimple prefs_;
  FrequentSitesService sites_;
  BookmarkService bookmarks_;
  PreferencesService settings_;
};
TEST_F(FrequentSitesServiceTest,
       FrequencyDecaysAndResultsAreBoundedWithoutWrites) {
  std::vector<SiteVisit> visits = {
      {"https://recent.test/path", now_},
      {"https://frequent.test/one", now_ - base::Days(7)},
      {"https://frequent.test/two", now_ - base::Days(7)},
      {"https://frequent.test/three", now_ - base::Days(7)},
      {"https://stale.test/one", now_ - base::Days(100)},
      {"file:///tmp/private.txt", now_},
      {"javascript:alert(1)", now_},
      {"https://a.test", now_ - base::Days(14)},
      {"https://b.test", now_ - base::Days(14)},
      {"https://c.test", now_ - base::Days(14)},
      {"https://d.test", now_ - base::Days(14)}};
  const auto result = sites_.Read(visits, now_);
  ASSERT_TRUE(result);
  ASSERT_EQ(5u, result->size());
  EXPECT_EQ("https://frequent.test", (*result)[0].url);
  EXPECT_EQ("https://recent.test", (*result)[1].url);
  EXPECT_EQ("https://a.test", (*result)[2].url);
  EXPECT_EQ("https://b.test", (*result)[3].url);
  EXPECT_EQ("https://c.test", (*result)[4].url);
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kBookmarksPref));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
}
TEST_F(FrequentSitesServiceTest, FutureTimeCannotOutweighTwoCurrentVisits) {
  const auto result =
      sites_.Read({{"https://future.test", now_ + base::Days(1000)},
                   {"https://current.test/one", now_},
                   {"https://current.test/two", now_}},
                  now_);
  ASSERT_TRUE(result);
  ASSERT_EQ(2u, result->size());
  EXPECT_EQ("https://current.test", (*result)[0].url);
  EXPECT_EQ("https://future.test", (*result)[1].url);
}
TEST_F(FrequentSitesServiceTest,
       BookmarkAndHiddenExclusionsKeepExactWebOrigins) {
  ASSERT_TRUE(bookmarks_.Add("https://site.test/bookmarked", "Saved", now_));
  ASSERT_TRUE(sites_.Hide("https://hidden.test/path?q=one"));
  FrequentSitesService restarted(prefs_);
  const auto result = restarted.Read({{"https://site.test/another", now_},
                                      {"http://site.test/path", now_},
                                      {"https://site.test:8443/path", now_},
                                      {"https://sub.site.test", now_},
                                      {"https://hidden.test/another", now_}},
                                     now_);
  ASSERT_TRUE(result);
  ASSERT_EQ(3u, result->size());
  for (const auto& row : *result) {
    EXPECT_NE("https://site.test", row.url);
    EXPECT_NE("https://hidden.test", row.url);
  }
  ASSERT_TRUE(bookmarks_.Remove("https://site.test/bookmarked"));
  const auto unbookmarked = restarted.Read({{"https://site.test", now_}}, now_);
  ASSERT_TRUE(unbookmarked);
  EXPECT_EQ(1u, unbookmarked->size());
}
TEST_F(FrequentSitesServiceTest,
       ExplicitHiddenChoicePreservesCollectionAndMetadata) {
  ASSERT_TRUE(bookmarks_.Add("https://saved.test", "Retained", now_));
  auto store = prefs_.GetDict(kBookmarksPref).Clone();
  store.Set("future", base::DictValue().Set("keep", 17));
  prefs_.SetDict(kBookmarksPref, std::move(store));
  ASSERT_TRUE(sites_.Hide("https://hidden.test/path"));
  EXPECT_EQ(17,
            prefs_.GetDict(kBookmarksPref).FindDict("future")->FindInt("keep"));
  EXPECT_TRUE(bookmarks_.Contains("https://saved.test"));
  const auto hidden = prefs_.GetDict(kBookmarksPref).Clone();
  EXPECT_TRUE(sites_.Hide("https://hidden.test/another"));
  EXPECT_EQ(hidden, prefs_.GetDict(kBookmarksPref));
  EXPECT_FALSE(sites_.Hide("file:///tmp/a"));
  EXPECT_FALSE(sites_.Hide("javascript:alert(1)"));
  EXPECT_EQ(hidden, prefs_.GetDict(kBookmarksPref));
}
TEST_F(FrequentSitesServiceTest, FutureMalformedAndManagedChoicesArePreserved) {
  for (const auto& store :
       {base::DictValue().Set("schema", 2).Set("items", base::ListValue()),
        base::DictValue()
            .Set("schema", 1)
            .Set("items", base::ListValue())
            .Set("hidden_sites", true),
        base::DictValue()
            .Set("schema", 1)
            .Set("items", base::ListValue())
            .Set("hidden_sites",
                 base::ListValue().Append("https://one.test/path"))}) {
    prefs_.SetDict(kBookmarksPref, store.Clone());
    EXPECT_FALSE(sites_.Read({}, now_));
    EXPECT_FALSE(sites_.CanHide());
    EXPECT_FALSE(sites_.Hide("https://other.test"));
    EXPECT_EQ(store, prefs_.GetDict(kBookmarksPref));
  }
  prefs_.ClearPref(kBookmarksPref);
  prefs_.SetManagedPref(kBookmarksPref,
                        base::DictValue()
                            .Set("schema", 1)
                            .Set("items", base::ListValue())
                            .Set("hidden_sites", base::ListValue()));
  EXPECT_TRUE(sites_.Read({}, now_));
  EXPECT_FALSE(sites_.CanHide());
  EXPECT_FALSE(sites_.Hide("https://other.test"));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kBookmarksPref));
}
TEST_F(FrequentSitesServiceTest,
       VisibilityChoicePersistsAndRejectsWrongTypedSettings) {
  ASSERT_EQ(true, settings_.ReadFrequentSitesVisible());
  ASSERT_TRUE(settings_.SetFrequentSitesVisible(false));
  PreferencesService restarted(prefs_);
  EXPECT_EQ(false, restarted.ReadFrequentSitesVisible());
  const auto result =
      sites_.Read({{"https://hidden-by-choice.test", now_}}, now_);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->empty());
  auto malformed = prefs_.GetDict(kPreferencesPref).Clone();
  malformed.FindDict("values")->Set("show_frequent_sites", "false");
  prefs_.SetDict(kPreferencesPref, malformed.Clone());
  EXPECT_FALSE(restarted.ReadFrequentSitesVisible());
  EXPECT_FALSE(restarted.SetFrequentSitesVisible(true));
  EXPECT_FALSE(sites_.Read({}, now_));
  EXPECT_EQ(malformed, prefs_.GetDict(kPreferencesPref));
}
}  // namespace
}  // namespace fly
