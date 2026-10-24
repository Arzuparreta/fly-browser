// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/welcome_view.h"
#include "ui/gfx/canvas.h"
#include "cc/paint/draw_looper.h"
#include "base/i18n/case_conversion.h"

#include <memory>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "base/version_info/version_info.h"
#include "fly/fly_version.h"
#include "fly/ui/fly_theme.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/box_layout.h"

namespace fly {
namespace {
// .welcome-shortcut-card: the binding over its description; hover shows the
// selection colour and an accent border.
class WelcomeCard : public views::Button {
  METADATA_HEADER(WelcomeCard, views::Button)

 public:
  explicit WelcomeCard(const WelcomeView::Card& card)
      : views::Button(card.action) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    keys_ = AddChildView(std::make_unique<FlyLabel>(
        card.keys.empty() ? u"Unassigned" : card.keys, FlyFace::kMono, 13,
        gfx::Font::Weight::BOLD, FlyLabel::Tone::kAccent));
    keys_->SetBorder(FlyBorder(FlyRole::kBorder, 1, 4, gfx::Insets::VH(3, 7)));
    keys_->SetBackground(FlyBackground(FlyRole::kSelected, 4));
    keys_->SetCanProcessEventsWithinSubtree(false);
    auto* label = AddChildView(std::make_unique<FlyLabel>(
        card.label, FlyFace::kSans, 13));
    label->SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetName(card.label + u" (" + card.keys + u")");
    UpdateLook();
  }
  void StateChanged(ButtonState old_state) override { UpdateLook(); }

 private:
  void UpdateLook() {
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBackground(FlyBackground(hot ? FlyRole::kSelected : FlyRole::kMain, 8));
    SetBorder(FlyBorder(hot ? FlyRole::kAccent : FlyRole::kBorder, 1, 8));
  }
  raw_ptr<FlyLabel> keys_ = nullptr;
};
BEGIN_METADATA(WelcomeCard)
END_METADATA
// .welcome-card: at most 650 px wide; the height follows the contents.
class FixedWidthView : public views::View {
  METADATA_HEADER(FixedWidthView, views::View)

 public:
  explicit FixedWidthView(int width) : width_(width) {}
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    int width = width_;
    if (available.width().is_bounded())
      width = std::min(width, available.width().value());
    return gfx::Size(width,
                     GetLayoutManager()->GetPreferredHeightForWidth(this, width));
  }

 private:
  const int width_;
};
BEGIN_METADATA(FixedWidthView)
END_METADATA
}  // namespace

WelcomeView::WelcomeView() {
  auto* outer = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(24)));
  outer->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  outer->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  // A matte card on the canvas, 2px corners.
  auto* card = AddChildView(std::make_unique<FixedWidthView>(650));
  card_ = card;
  card->SetBackground(FlyBackground(FlyRole::kMain, 2));
  card->SetBorder(FlyBorder(FlyRole::kBorder, 1, 2, gfx::Insets(40)));
  auto* layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  logo_ = card->AddChildView(std::make_unique<views::ImageView>());
  logo_->SetImageSize(gfx::Size(96, 96));
  logo_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 16, 0)));
  auto* title = card->AddChildView(std::make_unique<FlyLabel>(
      u"Fly Browser", FlyFace::kDisplay, 28, gfx::Font::Weight::BLACK));
  title->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 8, 0)));
  workspace_ = card->AddChildView(std::make_unique<FlyLabel>(
      u"Workspace", FlyFace::kSans, 16, gfx::Font::Weight::SEMIBOLD,
      FlyLabel::Tone::kAccent));
  workspace_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 16, 0)));
  auto* description = card->AddChildView(std::make_unique<FlyLabel>(
      u"This workspace is currently empty. Use the quick actions below or "
      u"keyboard shortcuts to get started:",
      FlyFace::kSans, 14, gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
  description->SetMultiLine(true);
  description->SetMaximumWidth(570);
  description->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 30, 0)));
  grid_ = card->AddChildView(std::make_unique<views::View>());
  grid_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 16));

  auto* hint = card->AddChildView(std::make_unique<FlyLabel>(
      u"Open Preferences from the command palette to customise keyboard "
      u"controls.",
      FlyFace::kSans, 12, gfx::Font::Weight::NORMAL, FlyLabel::Tone::kAccent));
  hint->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(14, 0, 0, 0)));
  auto* versions = card->AddChildView(std::make_unique<FlyLabel>(
      base::UTF8ToUTF16(std::string("Fly v") + kFlyVersion + " · Chromium " +
                        std::string(version_info::GetVersionNumber())),
      FlyFace::kMono, 11, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kAccent));
  // .welcome-versions: a bordered badge 24px below the hint.
  versions->SetBackground(FlyBackground(FlyRole::kMain, 4));
  versions->SetBorder(FlyBorder(FlyRole::kBorder, 1, 4, gfx::Insets::VH(5, 13)));
  auto* versions_gap = card->AddChildView(std::make_unique<views::View>());
  versions_gap->SetPreferredSize(gfx::Size(1, 12));
  card->ReorderChildView(versions_gap, card->children().size() - 2);
}
WelcomeView::~WelcomeView() = default;

// The canvas with a 16px grid of 0.7px dots in the
// border colour, and the card's shadow (0 16px 48px rgba(0, 0, 0, 0.5)).
void WelcomeView::OnPaintBackground(gfx::Canvas* canvas) {
  const auto& colors = GetFlyColors();
  canvas->DrawColor(colors.bg_main);
  cc::PaintFlags dot;
  dot.setAntiAlias(true);
  dot.setColor(colors.border);
  gfx::Rect clip(size());
  canvas->GetClipBounds(&clip);
  for (int y = (clip.y() / 16) * 16; y < clip.bottom() + 16; y += 16) {
    for (int x = (clip.x() / 16) * 16; x < clip.right() + 16; x += 16)
      canvas->DrawCircle(gfx::PointF(x + 8, y + 8), 0.7f, dot);
  }
  if (!card_)
    return;
  cc::DrawLooperBuilder looper;
  looper.AddShadow({0, 16}, 24, SkColor4f::FromColor(SkColorSetARGB(128, 0, 0, 0)),
                   cc::DrawLooper::kOverrideAlphaFlag);
  cc::PaintFlags shadow;
  shadow.setAntiAlias(true);
  shadow.setLooper(looper.Detach());
  shadow.setColor(colors.bg_main);
  canvas->DrawRoundRect(gfx::RectF(card_->bounds()), 2, shadow);
}

void WelcomeView::Update(std::u16string workspace, std::vector<Card> cards) {
  // .welcome-subtitle is upper case.
  workspace_->SetText(base::i18n::ToUpper(workspace));
  grid_->RemoveAllChildViews();
  // Two columns, as .welcome-shortcuts-grid.
  for (size_t i = 0; i < cards.size(); i += 2) {
    auto* row = grid_->AddChildView(std::make_unique<views::View>());
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 16));
    for (size_t j = i; j < std::min(i + 2, cards.size()); ++j) {
      auto* card = row->AddChildView(std::make_unique<WelcomeCard>(cards[j]));
      // Two equal columns of the 570 px grid (.welcome-card is 650 wide with
      // 40 px padding); the height follows the text.
      card->SetPreferredSize(
          gfx::Size(277, card->GetPreferredSize().height() + 4));
      layout->SetFlexForView(card, 1);
    }
  }
  InvalidateLayout();
}

void WelcomeView::SetLogo(const gfx::ImageSkia& logo) {
  logo_->SetImage(ui::ImageModel::FromImageSkia(logo));
}

std::u16string WelcomeView::workspace_for_testing() const {
  return std::u16string(workspace_->GetText());
}

views::View* WelcomeView::card_for_testing(size_t index) {
  for (views::View* row : grid_->children()) {
    if (index < row->children().size())
      return row->children()[index];
    index -= row->children().size();
  }
  return nullptr;
}
BEGIN_METADATA(WelcomeView)
END_METADATA
}  // namespace fly
