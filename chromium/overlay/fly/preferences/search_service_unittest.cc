// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/search_service.h"

#include "base/values.h"
#include "build/build_config.h"
#include "components/prefs/testing_pref_service.h"
#include "fly/preferences/preferences_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class SearchServiceTest : public testing::Test {
 protected:
  SearchServiceTest() : service_(prefs_) {
    PreferencesService::RegisterLocalStatePrefs(prefs_.registry());
  }
  TestingPrefServiceSimple prefs_;
  SearchService service_;
};
TEST_F(SearchServiceTest, BuiltinsEncodingAndDirectNavigationNeverWrite) {
  ASSERT_TRUE(service_.Read());
  EXPECT_EQ(12u, service_.Read()->aliases.size());
  EXPECT_EQ(
      GURL("https://github.com/search?q=c%2B%2B%20%26%20rust%20%2F%20web"),
      service_.ResolveNavigation("GH c++ & rust / web"));
  EXPECT_EQ(GURL("https://duckduckgo.com/?q=fly%20browser"),
            service_.ResolveNavigation(" fly browser "));
  EXPECT_EQ(GURL("https://example.com/path"),
            service_.ResolveNavigation("example.com/path"));
  EXPECT_EQ(GURL("http://localhost:8000/fixture"),
            service_.ResolveNavigation("localhost:8000/fixture"));
  EXPECT_EQ(GURL("about:blank"), service_.ResolveNavigation("about:blank"));
  // Unsupported schemes are never rewritten into an https URL for another host.
  EXPECT_FALSE(service_.ResolveNavigation("mailto:user@example.com"));
  EXPECT_FALSE(service_.ResolveNavigation("ftp://files.example.org"));
  EXPECT_EQ(GURL("https://example.com:8443/path"),
            service_.ResolveNavigation("example.com:8443/path"));
  EXPECT_EQ(GURL("https://[::1]:8080/index.html"),
            service_.ResolveNavigation("[::1]:8080/index.html"));
  EXPECT_EQ(GURL("https://intranet:8080/?q=a.b"),
            service_.ResolveNavigation("intranet:8080?q=a.b"));
#if BUILDFLAG(IS_WIN)
  EXPECT_EQ(GURL("file:///C:/Fly%20fixture.txt"),
            service_.ResolveNavigation("C:\\Fly fixture.txt"));
  EXPECT_EQ(GURL("file://server/share/fixture.txt"),
            service_.ResolveNavigation("\\\\server\\share\\fixture.txt"));
#else
  EXPECT_EQ(GURL("file:///tmp/fixture.txt"),
            service_.ResolveNavigation("/tmp/fixture.txt"));
#endif
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
}
TEST_F(SearchServiceTest, OverridesAndRemovalRestoreDefaultsAcrossRestart) {
  ASSERT_TRUE(service_.SetEngine("https://example.com/find?q=%s"));
  ASSERT_TRUE(service_.SetAlias(" G ", "Custom", "https://example.org/?q="));
  SearchService restarted(prefs_);
  EXPECT_EQ(GURL("https://example.org/?q=two%20words"),
            restarted.ResolveNavigation("g two words"));
  EXPECT_EQ(GURL("https://example.com/find?q=two%20words"),
            restarted.ResolveNavigation("two words"));
  ASSERT_TRUE(restarted.RemoveAlias("G"));
  EXPECT_EQ(GURL("https://www.google.com/search?q=two%20words"),
            restarted.ResolveNavigation("g two words"));
  EXPECT_FALSE(restarted.RemoveAlias("g"));
}
TEST_F(SearchServiceTest, AliasesRetainUnknownMetadataAndCaseOnEdit) {
  prefs_.SetDict(
      kPreferencesPref,
      base::DictValue()
          .Set("schema", 1)
          .Set("future", "keep")
          .Set("values",
               base::DictValue()
                   .Set("other", 3)
                   .Set("custom_search_aliases",
                        base::DictValue().Set(
                            "GH", base::DictValue()
                                      .Set("name", "Old")
                                      .Set("url", "https://example.com/?q=%s")
                                      .Set("future", 4)))));
  ASSERT_TRUE(service_.SetAlias("gh", "New", "https://example.org/?q=%s"));
  const auto& store = prefs_.GetDict(kPreferencesPref);
  EXPECT_EQ("keep", *store.FindString("future"));
  EXPECT_EQ(3, store.FindDict("values")->FindInt("other"));
  const auto* aliases =
      store.FindDict("values")->FindDict("custom_search_aliases");
  EXPECT_EQ(1u, aliases->size());
  EXPECT_EQ(4, aliases->FindDict("GH")->FindInt("future"));
  EXPECT_EQ("New", *aliases->FindDict("GH")->FindString("name"));
}
TEST_F(SearchServiceTest, RecoveryDataAndManagedChoicesAreNeverOverwritten) {
  for (const auto& value :
       {base::DictValue().Set("schema", 2).Set("values", base::DictValue()),
        base::DictValue()
            .Set("schema", 1)
            .Set("values", base::DictValue().Set("custom_search_aliases",
                                                 base::ListValue())),
        base::DictValue()
            .Set("schema", 1)
            .Set("values", base::DictValue().Set(
                               "custom_search_aliases",
                               base::DictValue()
                                   .Set("G", "https://one.test/?q=%s")
                                   .Set("g", "https://two.test/?q=%s")))}) {
    prefs_.SetDict(kPreferencesPref, value.Clone());
    EXPECT_FALSE(service_.Read());
    EXPECT_FALSE(service_.SetEngine("https://example.com/?q=%s"));
    EXPECT_FALSE(service_.SetAlias("g", "Google", "https://example.com/%s"));
    EXPECT_FALSE(service_.RemoveAlias("g"));
    EXPECT_EQ(value, prefs_.GetDict(kPreferencesPref));
  }
  prefs_.ClearPref(kPreferencesPref);
  prefs_.SetManagedPref(
      kPreferencesPref,
      base::DictValue()
          .Set("schema", 1)
          .Set("values", base::DictValue().Set("search_engine",
                                               "https://managed.test/%s")));
  EXPECT_TRUE(service_.Read());
  EXPECT_FALSE(service_.CanEdit());
  EXPECT_FALSE(service_.SetEngine("https://example.com/?q=%s"));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
}
TEST_F(SearchServiceTest, RejectsInvalidTemplatesAndExecutableSchemes) {
  for (auto input :
       {"", "javascript:alert(1)", "data:text/html,test", "ws:work", "ws work"})
    EXPECT_FALSE(service_.ResolveNavigation(input)) << input;
  for (auto pattern : {"javascript:alert(%s)", "file:///tmp/%s",
                       "https://user:pass@example.com/%s"}) {
    EXPECT_FALSE(service_.SetEngine(pattern));
    EXPECT_FALSE(service_.SetAlias("g", "Name", pattern));
  }
  EXPECT_FALSE(
      service_.SetAlias("two words", "Name", "https://example.com/%s"));
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
}
TEST_F(SearchServiceTest, DuckDuckGoBangsPreserveCompleteInput) {
  ASSERT_TRUE(service_.SetEngine("https://example.com/%s"));
  EXPECT_EQ(GURL("https://duckduckgo.com/?q=rust%20!gh%20language"),
            service_.ResolveNavigation("rust !gh language"));
  EXPECT_EQ(GURL("https://example.com/wow!"),
            service_.ResolveNavigation("wow!"));
}
}  // namespace
}  // namespace fly
