// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_SESSIONS_TAB_MOVE_GATE_H_
#define FLY_SESSIONS_TAB_MOVE_GATE_H_

#include <cstdint>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"

namespace fly {
// A UI-sequence, scoped bridge into native close processing. It contains no
// browser/profile UI dependencies. The callback performs a prepared move only
// when native beforeunload permits closing, or immediately before detachment
// for documents without beforeunload. Native close cancellation remains intact.
class ScopedTabMoveGate : public content::WebContentsObserver {
 public:
  ScopedTabMoveGate(content::WebContents* source,
                    base::RepeatingCallback<bool()> validate,
                    base::RepeatingCallback<bool()> commit);
  ~ScopedTabMoveGate() override;
  ScopedTabMoveGate(const ScopedTabMoveGate&) = delete;
  ScopedTabMoveGate& operator=(const ScopedTabMoveGate&) = delete;
  void WebContentsDestroyed() override;
  bool Commit();
  bool Validate();
  bool committed() const { return committed_; }

 private:
  const uintptr_t key_;
  base::RepeatingCallback<bool()> commit_;
  base::RepeatingCallback<bool()> validate_;
  bool committed_ = false;
  base::WeakPtrFactory<ScopedTabMoveGate> weak_factory_{this};
};

bool HasNativeTabMoveGate(content::WebContents* source);
bool ValidateNativeTabMoveClose(content::WebContents* source);
// No registered move means normal native close behavior.
bool CommitNativeTabMove(content::WebContents* source);
bool NativeTabMoveCommitted(content::WebContents* source);
}  // namespace fly
#endif  // FLY_SESSIONS_TAB_MOVE_GATE_H_
