// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license.
#include "fly/ui/startup_maintenance.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/compositor/compositor.h"
#include "ui/views/widget/widget.h"

namespace fly {
StartupMaintenance::StartupMaintenance(base::OnceClosure tasks,
                                       base::TimeDelta max_wait)
    : tasks_(std::move(tasks)) {
  TRACE_EVENT_INSTANT("browser", "Fly.StartupMaintenance.Scheduled");
  deadline_.Start(FROM_HERE, max_wait, this, &StartupMaintenance::OnDeadline);
}

StartupMaintenance::~StartupMaintenance() {
  StopObserving();
}

void StartupMaintenance::SetWindow(TabStripModel* tabs, views::Widget* widget) {
  if (!tasks_)
    return;
  if (tabs_ != tabs || widget_.GetSource() != widget) {
    StopObserving();
    if (tabs && widget) {
      tabs_ = tabs;
      tabs_->AddObserver(this);
      widget_.Observe(widget);
    }
  }
  ObserveSelectedPage();
}

void StartupMaintenance::SetPaused(bool paused) {
  paused_ = paused;
  presentation_weak_factory_.InvalidateWeakPtrs();
  presentation_pending_ = false;
  if (!paused_)
    ObserveSelectedPage();
}

void StartupMaintenance::ObserveSelectedPage() {
  if (!tasks_)
    return;
  auto* selected = tabs_ ? tabs_->GetActiveWebContents() : nullptr;
  if (selected != web_contents()) {
    content_painted_ = false;
    presentation_weak_factory_.InvalidateWeakPtrs();
    presentation_pending_ = false;
    Observe(selected);
  }
  if (expired_)
    Run("deadline");
  else
    MaybeRequestPresentation();
}

void StartupMaintenance::MaybeRequestPresentation() {
  if (!tasks_ || paused_ || presentation_pending_ || !web_contents() ||
      !widget_.IsObserving() ||
      web_contents()->GetVisibility() != content::Visibility::VISIBLE ||
      web_contents()->GetController().IsInitialBlankNavigation() ||
      !HasPaintedPage() || !widget_.GetSource()->IsVisible())
    return;
  auto* compositor = widget_.GetSource()->GetCompositor();
  if (!compositor)
    return;
  presentation_pending_ = true;
  TRACE_EVENT_INSTANT("browser", "Fly.StartupMaintenance.PagePainted");
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce(
      [](base::WeakPtr<StartupMaintenance> self,
         const viz::FrameTimingDetails&) {
        if (self)
          self->OnPresented();
      },
      presentation_weak_factory_.GetWeakPtr()));
  // Already-painted pages may otherwise produce no more frames. Request just
  // one frame, rather than waiting for unrelated animation or polling.
  compositor->ScheduleFullRedraw();
}

void StartupMaintenance::OnPresented() {
  presentation_pending_ = false;
  if (web_contents() && widget_.IsObserving() &&
      web_contents()->GetVisibility() == content::Visibility::VISIBLE &&
      HasPaintedPage() && widget_.GetSource()->IsVisible())
    Run("presented");
}

bool StartupMaintenance::HasPaintedPage() const {
  // The non-empty-layout heuristic can wait for parsing to finish on small
  // streaming documents. FCP releases those as soon as their content paints.
  // Already-painted selected tabs can predate our observation; their cached
  // non-empty-paint flag avoids waiting for another navigation or the deadline.
  return web_contents() &&
         (content_painted_ ||
          web_contents()->CompletedFirstVisuallyNonEmptyPaint());
}

void StartupMaintenance::OnDeadline() {
  expired_ = true;
  Run("deadline");
}

void StartupMaintenance::Run(const char* reason) {
  // A disappearing window is not an opportunity to show an offer elsewhere.
  // Retain the expired deadline until a surviving/new window is selected.
  if (!tasks_ || paused_ || !tabs_ || !widget_.IsObserving())
    return;
  TRACE_EVENT_INSTANT("browser", "Fly.StartupMaintenance.Ready", "reason",
                      reason);
  deadline_.Stop();
  StopObserving();
  std::move(tasks_).Run();
}

void StartupMaintenance::StopObserving() {
  presentation_weak_factory_.InvalidateWeakPtrs();
  presentation_pending_ = false;
  Observe(nullptr);
  TabStripModelObserver::StopObservingAll(this);
  tabs_ = nullptr;
  widget_.Reset();
}

void StartupMaintenance::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  ObserveSelectedPage();
}

void StartupMaintenance::OnTabStripModelDestroyed(TabStripModel*) {
  StopObserving();
}

void StartupMaintenance::DidFirstVisuallyNonEmptyPaint() {
  MaybeRequestPresentation();
}

void StartupMaintenance::OnFirstContentfulPaintInPrimaryMainFrame(
    base::TimeTicks) {
  content_painted_ = true;
  MaybeRequestPresentation();
}

void StartupMaintenance::PrimaryPageChanged(content::Page&) {
  content_painted_ = false;
  presentation_weak_factory_.InvalidateWeakPtrs();
  presentation_pending_ = false;
  MaybeRequestPresentation();
}

void StartupMaintenance::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE) {
    presentation_weak_factory_.InvalidateWeakPtrs();
    presentation_pending_ = false;
  }
  MaybeRequestPresentation();
}

void StartupMaintenance::WebContentsDestroyed() {
  presentation_weak_factory_.InvalidateWeakPtrs();
  presentation_pending_ = false;
  Observe(nullptr);
}

void StartupMaintenance::OnWidgetVisibilityChanged(views::Widget*, bool) {
  presentation_weak_factory_.InvalidateWeakPtrs();
  presentation_pending_ = false;
  MaybeRequestPresentation();
}

void StartupMaintenance::OnWidgetDestroying(views::Widget*) {
  StopObserving();
}
}  // namespace fly
