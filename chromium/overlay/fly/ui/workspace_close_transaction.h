// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_WORKSPACE_CLOSE_TRANSACTION_H_
#define FLY_UI_WORKSPACE_CLOSE_TRANSACTION_H_

#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"

namespace fly {
class ScopedWorkspaceRetirement;

// UI-sequence coordinator. Native warnings and beforeunload finish before any
// window closes; persistent retirement is the owner's final successful commit.
// Dropping the coordinator cancels pending intent and releases its creation
// ban.
class WorkspaceCloseTransaction {
 public:
  WorkspaceCloseTransaction(base::FilePath original_profile_path,
                            base::RepeatingCallback<bool()> validate,
                            base::OnceCallback<void(bool)> done);
  ~WorkspaceCloseTransaction();
  WorkspaceCloseTransaction(const WorkspaceCloseTransaction&) = delete;
  WorkspaceCloseTransaction& operator=(const WorkspaceCloseTransaction&) =
      delete;
  void Start();

 private:
  enum class Phase { kWarnings, kBeforeUnload, kClosing, kComplete };
  void Advance();
  void ScheduleAdvance();
  void Confirmed(size_t index, bool proceed);
  void OnClosed(BrowserWindowInterface* browser);
  void OnCancelled(BrowserWindowInterface* browser,
                   BrowserWindowInterface::ClosingStatus status);
  void Finish(bool success);
  void ResetHandlers();

  const base::FilePath path_;
  base::RepeatingCallback<bool()> validate_;
  base::OnceCallback<void(bool)> done_;
  std::unique_ptr<ScopedWorkspaceRetirement> lease_;
  std::vector<base::WeakPtr<BrowserWindowInterface>> browsers_;
  std::vector<base::CallbackListSubscription> subscriptions_;
  Phase phase_ = Phase::kWarnings;
  size_t index_ = 0;
  bool advance_pending_ = false;
  bool cancelled_ = false;
  base::WeakPtrFactory<WorkspaceCloseTransaction> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_WORKSPACE_CLOSE_TRANSACTION_H_
