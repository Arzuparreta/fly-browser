// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_PREFERENCES_NEW_TAB_TIPS_H_
#define FLY_PREFERENCES_NEW_TAB_TIPS_H_

#include <cstddef>
#include <optional>
#include <string_view>

#include "base/functional/function_ref.h"
#include "base/memory/raw_ref.h"

class PrefRegistrySimple;
class PrefService;

namespace fly {
// Local State: action id -> times its keys were used, up to kNewTabTipUses.
inline constexpr char kNewTabTipsPref[] = "fly.new_tab_tips";
// Using an action's keys this many times retires its tip.
inline constexpr int kNewTabTipUses = 2;

struct NewTabTip {
  std::string_view action;
  std::string_view text;
};

// The keyboard tips the new tab teaches, most useful first. Each one shows
// until its keys have been used kNewTabTipUses times or it is dismissed, so
// the page becomes quiet once the user knows them all.
class NewTabTips {
 public:
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  explicit NewTabTips(PrefService& local_state);
  ~NewTabTips();
  NewTabTips(const NewTabTips&) = delete;
  NewTabTips& operator=(const NewTabTips&) = delete;

  static std::optional<NewTabTip> Find(std::string_view action);
  // True while `action` is a tip that has not been learned or dismissed.
  bool Pending(std::string_view action) const;
  // The next pending tip whose action `usable` accepts (it has a key). Each
  // call moves to the following tip, so consecutive new tabs teach different
  // keys; the rotation is kept in memory and never written.
  std::optional<NewTabTip> Next(
      base::FunctionRef<bool(std::string_view action)> usable);
  // Called for every action run from the keyboard. Writes Local State at most
  // kNewTabTipUses times per tip, ever; any other action returns at once.
  void RecordKeyboardUse(std::string_view action);
  void Dismiss(std::string_view action);
  // Shows every tip again.
  void Reset();

 private:
  int Uses(std::string_view action) const;
  void SetUses(std::string_view action, int uses);
  const raw_ref<PrefService> local_state_;
  size_t rotation_ = 0;
};
}  // namespace fly

#endif  // FLY_PREFERENCES_NEW_TAB_TIPS_H_
