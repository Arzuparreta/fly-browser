// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_WELCOME_VIEW_H_
#define FLY_UI_WELCOME_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"

namespace views {
class ImageView;
}

namespace fly {
class FlyLabel;
// Electron Fly's empty workspace view (#EmptyWorkspaceWelcome): logo, product
// name, the workspace, four shortcut cards and the version line. Shown in the
// content area while the current workspace has no tabs.
class WelcomeView : public views::View {
  METADATA_HEADER(WelcomeView, views::View)

 public:
  struct Card {
    std::u16string keys;
    std::u16string label;
    base::RepeatingClosure action;
  };
  WelcomeView();
  ~WelcomeView() override;
  void Update(std::u16string workspace, std::vector<Card> cards);
  void SetLogo(const gfx::ImageSkia& logo);
  std::u16string workspace_for_testing() const;
  views::View* card_for_testing(size_t index);

 private:
  raw_ptr<views::ImageView> logo_ = nullptr;
  raw_ptr<FlyLabel> workspace_ = nullptr;
  raw_ptr<views::View> grid_ = nullptr;
};
}  // namespace fly

#endif  // FLY_UI_WELCOME_VIEW_H_
