// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_PALETTE_CATALOG_H_
#define FLY_COMMANDS_PALETTE_CATALOG_H_

#include <string>
#include <string_view>
#include <vector>
#include "fly/commands/keyboard_config.h"

namespace fly {
struct PaletteEntry {
  std::string id;
  std::string action;
  std::string title;
  std::string category;
  std::vector<std::string> shortcuts;
  std::vector<std::string> normal;
};
// Explicit product entries followed by remaining catalogue actions. Bindings
// come from the current configuration, including deliberately disabled keys.
std::vector<PaletteEntry> PaletteCatalog(const KeyboardConfig& config);
std::vector<PaletteEntry> FilterPalette(
    const std::vector<PaletteEntry>& entries,
    std::u16string_view query);
}  // namespace fly
#endif
