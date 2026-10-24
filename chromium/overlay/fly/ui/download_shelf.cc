// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/download_shelf.h"

#include <cmath>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/download_item_utils.h"
#include "fly/ui/fly_theme.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"

namespace fly {
namespace {
int S(int pixels) {
  return FlyScaled(pixels);
}
// A size or speed in the largest unit it fills, with one decimal.
std::string Units(double value, const std::vector<const char*>& units,
                  bool whole_first) {
  if (value <= 0)
    return std::string("0 ") + units.front();
  size_t index = std::min<size_t>(
      static_cast<size_t>(std::floor(std::log(value) / std::log(1024.0))),
      units.size() - 1);
  const double scaled = value / std::pow(1024.0, index);
  return (whole_first && index == 0
              ? base::StringPrintf("%.0f", scaled)
              : base::StringPrintf("%.1f", scaled)) +
         " " + units[index];
}
std::string Bytes(int64_t bytes) {
  return Units(bytes, {"B", "KB", "MB", "GB", "TB"}, true);
}
std::string Speed(int64_t bytes_per_second) {
  return Units(bytes_per_second, {"B/s", "KB/s", "MB/s", "GB/s"}, false);
}
std::u16string FileName(download::DownloadItem* item) {
  auto name = item->GetFileNameToReportUser();
  if (name.empty())
    name = item->GetTargetFilePath().BaseName();
  return name.empty() ? u"download" : name.LossyDisplayName();
}

// .download-action-btn: a 22px icon button at 70% that lights on hover.
class ActionButton : public views::Button {
  METADATA_HEADER(ActionButton, views::Button)

 public:
  ActionButton(PressedCallback callback,
               const gfx::VectorIcon& icon,
               std::u16string label,
               bool cancel = false)
      : views::Button(std::move(callback)), icon_(&icon), cancel_(cancel) {
    SetTooltipText(label);
    GetViewAccessibility().SetName(label);
    SetPreferredSize(gfx::Size(S(22), S(22)));
  }
  void StateChanged(ButtonState) override { SchedulePaint(); }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (hot) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(colors.dark ? SkColorSetARGB(31, 255, 255, 255)
                                 : SkColorSetARGB(20, 0, 0, 0));
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 3, flags);
    }
    SkColor color = hot && cancel_ ? colors.accent : colors.text;
    if (!hot)
      color = SkColorSetA(color, 179);
    const int size = S(11);
    canvas->DrawImageInt(gfx::CreateVectorIcon(*icon_, size, color),
                         (width() - size) / 2, (height() - size) / 2);
  }

 private:
  raw_ptr<const gfx::VectorIcon> icon_;
  const bool cancel_;
};
BEGIN_METADATA(ActionButton)
END_METADATA

// .download-card-progress-track and its accent bar.
class Progress : public views::View {
  METADATA_HEADER(Progress, views::View)

 public:
  void SetFraction(double fraction) {
    fraction_ = std::clamp(fraction, 0.0, 1.0);
    SchedulePaint();
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(0, 2);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    canvas->FillRect(GetLocalBounds(), colors.dark
                                           ? SkColorSetARGB(26, 255, 255, 255)
                                           : SkColorSetARGB(26, 0, 0, 0));
    canvas->FillRect(gfx::Rect(0, 0, width() * fraction_, height()),
                     colors.accent);
  }

 private:
  double fraction_ = 0;
};
BEGIN_METADATA(Progress)
END_METADATA
}  // namespace

// .download-card: icon, name, status line and progress, then actions.
class DownloadCard : public views::Button {
  METADATA_HEADER(DownloadCard, views::Button)

 public:
  DownloadCard(DownloadShelf* shelf, download::DownloadItem* item)
      : views::Button(base::BindRepeating(&DownloadCard::Open,
                                          base::Unretained(this))),
        shelf_(shelf),
        item_(item) {
    SetNotifyEnterExitOnChild(true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(S(3), S(8)), S(8)));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kDescriptionIcon, GetFlyColors().accent, S(15)));
    icon_->SetPreferredSize(gfx::Size(S(20), S(20)));
    auto* content = AddChildView(std::make_unique<views::View>());
    content->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
    layout->SetFlexForView(content, 1);
    title_ = content->AddChildView(std::make_unique<FlyLabel>(
        u"", FlyFace::kSans, 11, gfx::Font::Weight::SEMIBOLD));
    status_ = content->AddChildView(
        std::make_unique<FlyLabel>(u"", FlyFace::kSans, 10));
    status_->SetEnabledColor(
        SkColorSetA(GetFlyColors().text, static_cast<U8CPU>(0.75 * 255)));
    for (views::Label* label : {title_.get(), status_.get()}) {
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetElideBehavior(gfx::ELIDE_TAIL);
      label->SetSubpixelRenderingEnabled(false);
    }
    progress_ = content->AddChildView(std::make_unique<Progress>());
    actions_ = AddChildView(std::make_unique<views::View>());
    actions_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
    Update();
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(S(280), S(38));
  }
  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::Button::OnMouseEntered(event);
    SchedulePaint();
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    views::Button::OnMouseExited(event);
    SchedulePaint();
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool hovered = IsMouseHovered();
    const bool complete = state_ == download::DownloadItem::COMPLETE;
    const int alpha = complete && hovered ? 18 : 10;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors.dark ? SkColorSetARGB(alpha, 255, 255, 255)
                               : SkColorSetARGB(alpha, 0, 0, 0));
    gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, 5, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(hovered || state_ == download::DownloadItem::INTERRUPTED
                       ? colors.accent
                       : colors.border);
    bounds.Inset(0.5f);
    canvas->DrawRoundRect(bounds, 5, flags);
  }
  void Update() {
    if (!item_)
      return;
    const auto name = FileName(item_);
    const auto path = item_->GetTargetFilePath();
    title_->SetText(name);
    SetTooltipText(path.empty() ? name : name + u"\n" + path.LossyDisplayName());
    GetViewAccessibility().SetName(name);
    const auto state = item_->GetState();
    const int percent = std::max(0, item_->PercentComplete());
    std::string status;
    bool dangerous = false;
    switch (state) {
      case download::DownloadItem::IN_PROGRESS:
        if (item_->IsDangerous() || item_->IsInsecure()) {
          dangerous = true;
          status = "This file may be harmful";
        } else if (item_->IsPaused()) {
          status = base::NumberToString(percent) + "% (Paused)";
        } else if (item_->GetTotalBytes() > 0) {
          status = base::NumberToString(percent) + "% · " +
                   Bytes(item_->GetReceivedBytes()) + " / " +
                   Bytes(item_->GetTotalBytes()) + " · " +
                   Speed(item_->CurrentSpeed());
        } else {
          status = Bytes(item_->GetReceivedBytes()) + " · " +
                   Speed(item_->CurrentSpeed());
        }
        break;
      case download::DownloadItem::COMPLETE:
        status = Bytes(item_->GetTotalBytes() > 0 ? item_->GetTotalBytes()
                                                  : item_->GetReceivedBytes()) +
                 " · Complete";
        break;
      case download::DownloadItem::CANCELLED:
        status = "Cancelled";
        break;
      case download::DownloadItem::INTERRUPTED:
        status = "Failed";
        break;
      case download::DownloadItem::MAX_DOWNLOAD_STATE:
        break;
    }
    status_->SetText(base::UTF8ToUTF16(status));
    progress_->SetVisible(state == download::DownloadItem::IN_PROGRESS);
    progress_->SetFraction(percent / 100.0);
    // .cancelled: 60%.
    SetPaintToLayerIfNeeded(state == download::DownloadItem::CANCELLED);
    if (state != state_ || dangerous != dangerous_) {
      state_ = state;
      dangerous_ = dangerous;
      RebuildActions();
    }
    SchedulePaint();
  }
  void ItemGone() { item_ = nullptr; }

 private:
  void SetPaintToLayerIfNeeded(bool faded) {
    if (faded && !layer()) {
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
    }
    if (layer())
      layer()->SetOpacity(faded ? 0.6f : 1.0f);
  }
  void RebuildActions() {
    actions_->RemoveAllChildViews();
    auto add = [&](void (DownloadCard::*method)(), const gfx::VectorIcon& icon,
                   std::u16string label, bool cancel = false) {
      actions_->AddChildView(std::make_unique<ActionButton>(
          base::BindRepeating(method, base::Unretained(this)), icon,
          std::move(label), cancel));
    };
    if (state_ == download::DownloadItem::IN_PROGRESS) {
      if (dangerous_)
        add(&DownloadCard::Keep, vector_icons::kFileDownloadOldIcon,
            u"Keep File");
      add(&DownloadCard::Cancel, views::kCloseIcon, u"Cancel Download",
          true);
    } else if (state_ == download::DownloadItem::COMPLETE) {
      add(&DownloadCard::Open, vector_icons::kLaunchOldIcon, u"Open File");
      add(&DownloadCard::Reveal, vector_icons::kFolderOpenIcon,
          u"Show in File Manager");
      add(&DownloadCard::Dismiss, views::kCloseIcon, u"Dismiss");
    } else {
      add(&DownloadCard::Dismiss, views::kCloseIcon, u"Dismiss");
    }
    InvalidateLayout();
  }
  Profile* Owner() {
    return item_ ? Profile::FromBrowserContext(
                       content::DownloadItemUtils::GetBrowserContext(item_))
                 : nullptr;
  }
  // A completed card opens its file.
  void Open() {
    if (item_ && item_->GetState() == download::DownloadItem::COMPLETE &&
        !item_->GetFileExternallyRemoved())
      item_->OpenDownload();
  }
  void Reveal() {
    if (auto* profile = Owner(); profile && item_ &&
                                 !item_->GetTargetFilePath().empty())
      platform_util::ShowItemInFolder(profile, item_->GetTargetFilePath());
  }
  void Keep() {
    if (!item_)
      return;
    if (item_->IsDangerous())
      item_->ValidateDangerousDownload();
    else if (item_->IsInsecure())
      item_->ValidateInsecureDownload();
  }
  void Cancel() {
    if (item_)
      item_->Cancel(true);
  }
  void Dismiss() { shelf_->Dismiss(item_); }

  const raw_ptr<DownloadShelf> shelf_;
  raw_ptr<download::DownloadItem> item_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<FlyLabel> title_ = nullptr;
  raw_ptr<FlyLabel> status_ = nullptr;
  raw_ptr<Progress> progress_ = nullptr;
  raw_ptr<views::View> actions_ = nullptr;
  download::DownloadItem::DownloadState state_ =
      download::DownloadItem::MAX_DOWNLOAD_STATE;
  bool dangerous_ = false;
};
BEGIN_METADATA(DownloadCard)
END_METADATA

DownloadShelf::DownloadShelf(base::RepeatingClosure show_all)
    : show_all_(std::move(show_all)) {
  SetVisible(false);
  SetPreferredSize(gfx::Size(0, S(52)));
  SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0)));
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, S(12)),
      S(12)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  // .download-shelf-header: the accent arrow and "Downloads".
  auto* header = AddChildView(std::make_unique<views::View>());
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(6)));
  auto* arrow = header->AddChildView(std::make_unique<views::ImageView>());
  arrow->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kFileDownloadOldIcon, GetFlyColors().accent, S(13)));
  auto* title = header->AddChildView(std::make_unique<FlyLabel>(
      u"Downloads", FlyFace::kSans, 12, gfx::Font::Weight::SEMIBOLD));
  title->SetSubpixelRenderingEnabled(false);
  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetDrawOverflowIndicator(false);
  cards_view_ = scroll_->SetContents(std::make_unique<views::View>());
  cards_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(S(4), 0),
      S(10)));
  layout->SetFlexForView(scroll_, 1);
  auto* actions = AddChildView(std::make_unique<views::View>());
  actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(6)));
  for (const auto& [callback, icon, label] :
       {std::tuple{base::BindRepeating(
                       [](DownloadShelf* shelf) {
                         if (shelf->show_all_)
                           shelf->show_all_.Run();
                       },
                       base::Unretained(this)),
                   &vector_icons::kFileDownloadOldIcon,
                   u"All Downloads"},
        std::tuple{base::BindRepeating(&DownloadShelf::ClearCompleted,
                                       base::Unretained(this)),
                   &kDeleteIcon, u"Clear Completed Downloads"},
        std::tuple{base::BindRepeating(
                       [](DownloadShelf* shelf) { shelf->SetVisible(false); },
                       base::Unretained(this)),
                   &views::kCloseIcon, u"Close Downloads Shelf"}}) {
    auto button = MakeFlyIconButton(callback, *icon, label, 28);
    button->SetVectorIcon(*icon, 12);
    actions->AddChildView(std::move(button));
  }
}
DownloadShelf::~DownloadShelf() = default;

void DownloadShelf::AddProfile(Profile* profile) {
  auto* manager = profile ? profile->GetDownloadManager() : nullptr;
  if (manager && !managers_.IsObservingSource(manager))
    managers_.AddObservation(manager);
}
void DownloadShelf::Toggle() {
  SetVisible(!GetVisible() && !cards_.empty());
}
views::View* DownloadShelf::card_for_testing(download::DownloadItem* item) {
  auto it = cards_.find(item);
  return it == cards_.end() ? nullptr : it->second.get();
}
void DownloadShelf::OnDownloadCreated(content::DownloadManager*,
                                      download::DownloadItem* item) {
  // History loads completed items too; only new downloads open the shelf.
  if (item->GetState() != download::DownloadItem::IN_PROGRESS ||
      cards_.contains(item))
    return;
  // Newest first.
  auto* card = cards_view_->AddChildViewAt(
      std::make_unique<DownloadCard>(this, item), 0);
  cards_[item] = card;
  items_.AddObservation(item);
  DisableSubpixelText(card);
  SetVisible(true);
  cards_view_->InvalidateLayout();
  InvalidateLayout();
}
void DownloadShelf::ManagerGoingDown(content::DownloadManager* manager) {
  managers_.RemoveObservation(manager);
}
void DownloadShelf::OnDownloadUpdated(download::DownloadItem* item) {
  if (auto it = cards_.find(item); it != cards_.end())
    it->second->Update();
}
void DownloadShelf::OnDownloadDestroyed(download::DownloadItem* item) {
  Dismiss(item);
}
void DownloadShelf::Dismiss(download::DownloadItem* item) {
  auto it = cards_.find(item);
  if (it == cards_.end())
    return;
  if (items_.IsObservingSource(item))
    items_.RemoveObservation(item);
  auto* card = it->second.get();
  cards_.erase(it);
  card->ItemGone();
  // Removing the card from inside its own click handler is unsafe; post it.
  card->SetVisible(false);
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE,
      cards_view_->RemoveChildViewT(static_cast<views::View*>(card)));
  UpdateVisibility();
}
void DownloadShelf::ClearCompleted() {
  std::vector<download::DownloadItem*> done;
  for (const auto& [item, card] : cards_) {
    if (item->GetState() != download::DownloadItem::IN_PROGRESS)
      done.push_back(item);
  }
  for (auto* item : done)
    Dismiss(item);
}
void DownloadShelf::UpdateVisibility() {
  if (cards_.empty())
    SetVisible(false);
  cards_view_->InvalidateLayout();
  InvalidateLayout();
}
void DownloadShelf::OnPaintBackground(gfx::Canvas* canvas) {
  canvas->DrawColor(FlyRoleColor(FlyRole::kOverlaySidebar));
}
BEGIN_METADATA(DownloadShelf)
END_METADATA
}  // namespace fly
