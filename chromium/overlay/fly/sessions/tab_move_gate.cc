// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/sessions/tab_move_gate.h"

#include <map>
#include <utility>

#include "base/check.h"
#include "base/no_destructor.h"
#include "content/public/browser/browser_thread.h"

namespace fly {
namespace {
std::map<uintptr_t, base::WeakPtr<ScopedTabMoveGate>>& Gates() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  static base::NoDestructor<
      std::map<uintptr_t, base::WeakPtr<ScopedTabMoveGate>>>
      gates;
  return *gates;
}
}  // namespace

ScopedTabMoveGate::ScopedTabMoveGate(content::WebContents* source,
                                     base::RepeatingCallback<bool()> validate,
                                     base::RepeatingCallback<bool()> commit)
    : content::WebContentsObserver(source),
      key_(reinterpret_cast<uintptr_t>(source)),
      commit_(std::move(commit)),
      validate_(std::move(validate)) {
  CHECK(source);
  CHECK(Gates().emplace(key_, weak_factory_.GetWeakPtr()).second);
}
ScopedTabMoveGate::~ScopedTabMoveGate() {
  Gates().erase(key_);
}
void ScopedTabMoveGate::WebContentsDestroyed() {
  Gates().erase(key_);
}
bool ScopedTabMoveGate::Commit() {
  if (committed_)
    return true;
  const auto weak = weak_factory_.GetWeakPtr();
  if (!commit_.Run() || !weak)
    return false;
  committed_ = true;
  return true;
}
bool ScopedTabMoveGate::Validate() {
  return committed_ || validate_.Run();
}
bool HasNativeTabMoveGate(content::WebContents* source) {
  return Gates().contains(reinterpret_cast<uintptr_t>(source));
}
bool ValidateNativeTabMoveClose(content::WebContents* source) {
  const auto entry = Gates().find(reinterpret_cast<uintptr_t>(source));
  if (entry == Gates().end())
    return true;
  auto gate = entry->second;
  return gate && gate->Validate();
}
bool CommitNativeTabMove(content::WebContents* source) {
  const auto entry = Gates().find(reinterpret_cast<uintptr_t>(source));
  if (entry == Gates().end())
    return true;
  auto gate = entry->second;
  return gate && gate->Commit();
}
bool NativeTabMoveCommitted(content::WebContents* source) {
  const auto entry = Gates().find(reinterpret_cast<uintptr_t>(source));
  return entry != Gates().end() && entry->second && entry->second->committed();
}
}  // namespace fly
