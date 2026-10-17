// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/sessions/tab_identity.h"

#include <set>

#include "base/command_line.h"
#include "base/no_destructor.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
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
    : content::WebContentsUserData<TabIdentity>(*contents) {
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
  LiveIds().erase(id_);
}
const std::string& TabIdentity::Get(content::WebContents* contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CreateForWebContents(contents, std::string());
  return FromWebContents(contents)->id_;
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
