// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/sessions/tab_identity.h"

#include <set>

#include "base/check.h"
#include "base/command_line.h"
#include "base/no_destructor.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace fly {
namespace {
std::set<std::string>& LiveIds() {
  static base::NoDestructor<std::set<std::string>> ids;
  return *ids;
}
}  // namespace
WEB_CONTENTS_USER_DATA_KEY_IMPL(TabIdentity);

TabIdentity::TabIdentity(content::WebContents* contents,
                         const std::string& restored_id)
    : content::WebContentsUserData<TabIdentity>(*contents),
      content::WebContentsObserver(contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const auto restored = base::Uuid::ParseLowercase(restored_id);
  if (CanPersist(contents) && restored.is_valid() &&
      !LiveIds().contains(restored_id)) {
    id_ = restored_id;
  } else {
    do {
      id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    } while (LiveIds().contains(id_));
  }
  LiveIds().insert(id_);
}
TabIdentity::~TabIdentity() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (owns_id_)
    LiveIds().erase(id_);
}
void TabIdentity::AboutToBeDiscarded(content::WebContents* new_contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Chromium calls this before attaching the empty replacement to its native
  // tab. Never reassign an identity that has already been published, nor move
  // metadata across a BrowserContext boundary.
  if (!owns_id_ || !new_contents || FromWebContents(new_contents) ||
      new_contents->GetBrowserContext() != web_contents()->GetBrowserContext())
    return;
  CreateForWebContents(new_contents, std::string());
  auto* replacement = FromWebContents(new_contents);
  LiveIds().erase(replacement->id_);
  replacement->id_ = id_;
  // Keep the original ID reserved throughout replacement. The old contents
  // remain observable until Chromium finishes replacing the tab and deletes
  // them, but their destructor must not release the new owner's reservation.
  owns_id_ = false;
}
const std::string& TabIdentity::Get(content::WebContents* contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CreateForWebContents(contents, std::string());
  return FromWebContents(contents)->id_;
}
bool TabIdentity::PrepareProfileMove(content::WebContents* source,
                                     content::WebContents* destination) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!source || !destination || FromWebContents(destination) ||
      source->GetBrowserContext() == destination->GetBrowserContext() ||
      source->GetBrowserContext()->IsOffTheRecord() !=
          destination->GetBrowserContext()->IsOffTheRecord())
    return false;
  auto& controller = destination->GetController();
  // Chromium creates an initial NavigationEntry with every fresh FrameTree.
  // Check its actual initial-document state, not an empty history count.
  if (!controller.IsInitialBlankNavigation() || controller.GetPendingEntry() ||
      !controller.GetLastCommittedEntry() ||
      !controller.GetLastCommittedEntry()->IsInitialEntry())
    return false;
  Get(source);
  auto* original = FromWebContents(source);
  if (!original->owns_id_)
    return false;
  CreateForWebContents(destination, std::string());
  auto* replacement = FromWebContents(destination);
  LiveIds().erase(replacement->id_);
  replacement->id_ = original->id_;
  original->owns_id_ = false;
  return true;
}
bool TabIdentity::CanPersist(content::WebContents* contents) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch("fly-native") &&
         !contents->GetBrowserContext()->IsOffTheRecord();
}
void TabIdentity::Populate(content::WebContents* contents,
                           std::map<std::string, std::string>* extra_data) {
  if (CanPersist(contents))
    extra_data->insert_or_assign(kExtraDataKey, Get(contents));
}
void TabIdentity::Restore(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data) {
  if (!CanPersist(contents))
    return;
  const auto it = extra_data.find(kExtraDataKey);
  CreateForWebContents(contents,
                       it == extra_data.end() ? std::string() : it->second);
}
}  // namespace fly
