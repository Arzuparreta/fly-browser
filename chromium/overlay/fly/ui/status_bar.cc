// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/status_bar.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace fly {
class StatusBar::TargetObserver : public content::WebContentsObserver {
 public:
  explicit TargetObserver(StatusBar* owner) : owner_(owner) {}
  void Set(content::WebContents* contents) { Observe(contents); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE)
      owner_->ClearTarget();
  }
  void PrimaryPageChanged(content::Page&) override { owner_->ClearTarget(); }
  void WebContentsDestroyed() override { owner_->ClearTarget(); }

 private:
  raw_ptr<StatusBar> owner_;
};
StatusBar::StatusBar()
    : target_observer_(std::make_unique<TargetObserver>(this)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(3, 8), 12));
  SetBackground(views::CreateSolidBackground(ui::kColorDialogBackground));
  mode_ = AddChildView(std::make_unique<views::Label>(u"NORMAL"));
  mode_->SetTooltipText(u"Keyboard mode");
  location_ = AddChildView(std::make_unique<views::Label>());
  location_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  location_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  layout->SetFlexForView(location_, 1);
  security_ = AddChildView(std::make_unique<views::Label>());
  workspace_ = AddChildView(std::make_unique<views::Label>());
}
StatusBar::~StatusBar() {
  target_observer_.reset();
  Observe(nullptr);
  if (model_)
    model_->RemoveObserver(this);
}
void StatusBar::SetBrowser(BrowserWindowInterface* browser,
                           std::u16string workspace) {
  if (model_)
    model_->RemoveObserver(this);
  browser_ = browser ? browser->GetWeakPtr() : nullptr;
  model_ = browser ? browser->GetTabStripModel() : nullptr;
  if (model_)
    model_->AddObserver(this);
  workspace_->SetText(browser && browser->GetProfile()->IsOffTheRecord()
                          ? u"Private"
                          : std::move(workspace));
  workspace_->GetViewAccessibility().SetName(
      u"Workspace: " + std::u16string(workspace_->GetText()));
  target_ = GURL();
  target_observer_->Set(nullptr);
  Observe(nullptr);
  SelectContents();
}
void StatusBar::SelectContents() {
  auto* selected = model_ ? model_->GetActiveWebContents() : nullptr;
  if (selected != web_contents()) {
    target_ = GURL();
    target_observer_->Set(nullptr);
    Observe(selected);
  }
  Update();
}
void StatusBar::SetTargetURL(content::WebContents* source, const GURL& url) {
  // Both split panes can be visible. Accept the native pointer's visible pane,
  // while rejecting messages from hidden tabs or another profile/window model.
  if (!model_ || !source ||
      model_->GetIndexOfWebContents(source) == TabStripModel::kNoTab ||
      source->GetVisibility() != content::Visibility::VISIBLE)
    return;
  if (url.is_empty()) {
    if (target_observer_->web_contents() == source)
      ClearTarget();
    return;
  }
  target_ = url;
  target_observer_->Set(source);
  Update();
}
void StatusBar::ClearTarget() {
  target_ = GURL();
  target_observer_->Set(nullptr);
  Update();
}
void StatusBar::Update() {
  if (!web_contents()) {
    location_->SetText(u"");
    location_->SetTooltipText(u"");
    location_->GetViewAccessibility().SetName(u"");
    security_->SetText(u"");
    security_->SetTooltipText(u"");
    security_->GetViewAccessibility().SetName(u"");
    return;
  }
  const GURL page = web_contents()->GetVisibleURL();
  const auto location =
      url_formatter::FormatUrl(target_.is_empty() ? page : target_);
  location_->SetText(target_.is_empty() ? location : u"Link: " + location);
  location_->SetTooltipText(std::u16string(location_->GetText()));
  location_->GetViewAccessibility().SetName(
      std::u16string(location_->GetText()));
  std::u16string security;
  switch (chrome_security_state::GetSecurityLevel(web_contents())) {
    case security_state::SECURE:
      security = u"Page secure";
      break;
    case security_state::WARNING:
      security = u"Page not secure";
      break;
    case security_state::DANGEROUS:
      security = u"Page security warning";
      break;
    default:
      security = u"";
      break;
  }
  security_->SetText(security);
  security_->SetTooltipText(
      security.empty() ? u""
                       : security + u": " + url_formatter::FormatUrl(page));
  security_->GetViewAccessibility().SetName(security_->GetTooltipText());
}
void StatusBar::OnTabStripModelChanged(TabStripModel*,
                                       const TabStripModelChange&,
                                       const TabStripSelectionChange&) {
  SelectContents();
}
void StatusBar::OnTabStripModelDestroyed(TabStripModel*) {
  model_ = nullptr;
  browser_.reset();
  Observe(nullptr);
  ClearTarget();
}
void StatusBar::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame()) {
    ClearTarget();
  }
}
void StatusBar::DidChangeVisibleSecurityState() {
  Update();
}
void StatusBar::WebContentsDestroyed() {
  Observe(nullptr);
  ClearTarget();
}
BEGIN_METADATA(StatusBar)
END_METADATA
}  // namespace fly
