// Measurement harness for docs/chromium/cache-study.md. Not part of any build
// target: to run it, copy it to the engine's src/fly/test/, add it next to
// workspace_browser_test.cc in the fly_browser_tests sources of the engine's
// fly/BUILD.gn (plus "//components/prefs:test_support" in deps), build
// out/FlyNative fly_browser_tests and run --gtest_filter='FlyCacheBench.*'.
// Restore fly/BUILD.gn and remove the file before refresh-overlay.
// `#define private public` reaches BookmarkFavicons internals; it does not work
// for modular headers, so WorkspaceService is measured through fresh instances.
#include <algorithm>
#include <vector>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/favicon/core/favicon_service.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/test/history_service_test_util.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/browser_test.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/fly_app.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/native_commands.h"
#define private public
#include "fly/ui/bookmark_favicons.h"
#include "fly/workspaces/workspace_service.h"
#undef private
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace fly {
namespace {

struct Stats {
  double p50, p90, p99, mean;
};
Stats Summ(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  double sum = 0;
  for (double x : v)
    sum += x;
  auto at = [&](double q) {
    return v[std::min(v.size() - 1, static_cast<size_t>(q * v.size()))];
  };
  return {at(.5), at(.9), at(.99), sum / v.size()};
}
void Print(const std::string& name, const std::vector<double>& v) {
  const auto s = Summ(v);
  LOG(ERROR) << "FLYBENCH " << name
             << base::StringPrintf(" n=%zu p50=%.1fus p90=%.1fus p99=%.1fus mean=%.1fus",
                                   v.size(), s.p50, s.p90, s.p99, s.mean);
}

class FlyCacheBench : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch("fly-native");
  }

 protected:
  void Seed(Profile* profile, int count, int px) {
    auto* favicons = FaviconServiceFactory::GetForProfile(
        profile, ServiceAccessType::EXPLICIT_ACCESS);
    for (int i = 0; i < count; ++i) {
      SkBitmap bitmap;
      bitmap.allocN32Pixels(px, px);
      bitmap.eraseColor(SkColorSetRGB(i % 255, (i * 7) % 255, (i * 13) % 255));
      const GURL page("https://site" + base::NumberToString(i) + ".test/");
      favicons->SetFavicons(
          {page},
          GURL("https://site" + base::NumberToString(i) + ".test/favicon.ico"),
          favicon_base::IconType::kFavicon,
          gfx::Image::CreateFrom1xBitmap(bitmap));
    }
    history::BlockUntilHistoryProcessesPendingRequests(
        HistoryServiceFactory::GetForProfile(
            profile, ServiceAccessType::EXPLICIT_ACCESS));
  }
  double TimedGet(BookmarkFavicons* cache, const GURL& url, int px,
                  gfx::ImageSkia* out = nullptr) {
    base::test::TestFuture<gfx::ImageSkia> result;
    base::ElapsedTimer timer;
    cache->Get(url, px, result.GetCallback(), &tracker_);
    auto image = result.Take();
    const double us = timer.Elapsed().InMicrosecondsF();
    if (out)
      *out = image;
    return us;
  }
  base::CancelableTaskTracker tracker_;
};

// Cost of one cache miss (history thread + SQLite + PNG decode) against one
// hit, per requested pixel size, plus the bytes a cached entry holds.
IN_PROC_BROWSER_TEST_F(FlyCacheBench, FaviconMissAndHit) {
  auto* profile = browser()->GetProfile();
  constexpr int kSites = 400;
  Seed(profile, kSites, 32);
  auto* cache = BookmarkFavicons::ForProfile(profile);
  for (int px : {16, 32, 64}) {
    std::vector<double> miss, hit, absent;
    size_t bytes = 0;
    cache->cache_.Clear();
    for (int i = 0; i < 250; ++i) {
      const GURL page("https://site" + base::NumberToString(i) + ".test/");
      gfx::ImageSkia image;
      miss.push_back(TimedGet(cache, page, px, &image));
      if (!image.isNull())
        bytes = image.bitmap()->computeByteSize();
    }
    for (int i = 0; i < 250; ++i)
      hit.push_back(TimedGet(
          cache, GURL("https://site" + base::NumberToString(i) + ".test/"),
          px));
    for (int i = 0; i < 250; ++i)
      absent.push_back(TimedGet(
          cache, GURL("https://absent" + base::NumberToString(i) + ".test/"),
          px));
    const std::string suffix = "_" + base::NumberToString(px) + "px";
    Print(("favicon_miss" + suffix), miss);
    Print(("favicon_hit" + suffix), hit);
    Print(("favicon_absent_miss" + suffix), absent);
    LOG(ERROR) << "FLYBENCH favicon_entry_pixels_bytes" << suffix << "=" << bytes;
  }
}

// What the person waits for: open the bookmark picker with N bookmarks and
// count the time until every built row has its icon, with a cold and a warm
// cache, then the same for one filter keystroke.
IN_PROC_BROWSER_TEST_F(FlyCacheBench, BookmarkPickerOpen) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  auto* profile = browser()->GetProfile();
  constexpr int kBookmarks = 1000;
  Seed(profile, kBookmarks, 32);
  for (int i = 0; i < kBookmarks; ++i)
    ASSERT_TRUE(FlyApp::Get().bookmarks().Add(
        "https://site" + base::NumberToString(i) + ".test/",
        "Site " + base::NumberToString(i)));
  auto* window = FlyWindowController::FromBrowser(browser());
  auto& picker = window->bookmark_picker();
  auto* cache = BookmarkFavicons::ForProfile(profile);
  std::vector<double> cold, warm, cold_key, warm_key;
  size_t built = 0;
  for (int round = 0; round < 12; ++round) {
    const bool is_cold = round % 2 == 0;
    if (is_cold)
      cache->cache_.Clear();
    const size_t before = cache->queries_;
    base::ElapsedTimer timer;
    ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
    built = picker.built_rows_for_testing();
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (size_t r = 0; r < built; ++r)
        if (picker.row_favicon_for_testing(r).isNull())
          return false;
      return true;
    }));
    (is_cold ? cold : warm).push_back(timer.Elapsed().InMicrosecondsF());
    LOG(ERROR) << "FLYBENCH picker_round=" << round << " cold=" << is_cold
               << " queries=" << (cache->queries_ - before) << " built=" << built;
    // One keystroke: "Site 9" keeps 111 matches; rows are rebuilt.
    if (is_cold)
      cache->cache_.Clear();
    base::ElapsedTimer key_timer;
    picker.search_for_testing()->SetText(u"Site 9");
    picker.ContentsChanged(picker.search_for_testing(), u"Site 9");
    const size_t key_built = picker.built_rows_for_testing();
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (size_t r = 0; r < key_built; ++r)
        if (picker.row_favicon_for_testing(r).isNull())
          return false;
      return true;
    }));
    (is_cold ? cold_key : warm_key)
        .push_back(key_timer.Elapsed().InMicrosecondsF());
    picker.Cancel(true);
    ASSERT_TRUE(base::test::RunUntil([&] { return !picker.showing(); }));
  }
  LOG(ERROR) << "FLYBENCH picker_built_rows=" << built << " bookmarks=" << kBookmarks;
  Print("picker_open_icons_cold", cold);
  Print("picker_open_icons_warm", warm);
  Print("picker_keystroke_icons_cold", cold_key);
  Print("picker_keystroke_icons_warm", warm_key);
}

// Cost of WorkspaceService::Read with and without its validated cache.
IN_PROC_BROWSER_TEST_F(FlyCacheBench, WorkspaceValidation) {
  for (int count : {1, 5, 20, 50}) {
    TestingPrefServiceSimple prefs;
    WorkspaceService::RegisterLocalStatePrefs(prefs.registry());
    WorkspaceService service(prefs);
    for (int i = 0; i < count; ++i)
      ASSERT_TRUE(service.Add("workspace " + base::NumberToString(i),
                              i == 0 ? "Default"
                                     : "Profile " + base::NumberToString(i)));
    ASSERT_EQ(static_cast<size_t>(count), service.Read()->items.size());
    constexpr int kReads = 20000;
    std::vector<double> cached, uncached, construct;
    for (int batch = 0; batch < 20; ++batch) {
      base::ElapsedTimer a;
      for (int i = 0; i < kReads / 20; ++i)
        CHECK(service.Read());
      cached.push_back(a.Elapsed().InMicrosecondsF() / (kReads / 20));
      base::ElapsedTimer b;
      for (int i = 0; i < kReads / 20; ++i) {
        WorkspaceService fresh(prefs);
        CHECK(fresh.Read());
      }
      uncached.push_back(b.Elapsed().InMicrosecondsF() / (kReads / 20));
      base::ElapsedTimer c;
      for (int i = 0; i < kReads / 20; ++i) {
        WorkspaceService fresh(prefs);
      }
      construct.push_back(c.Elapsed().InMicrosecondsF() / (kReads / 20));
    }
    const std::string n = base::NumberToString(count);
    Print(("workspace_read_cached_" + n), cached);
    Print(("workspace_read_uncached_" + n), uncached);
    Print(("workspace_construct_only_" + n), construct);
  }
}

}  // namespace
}  // namespace fly
