// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_PALETTE_CATALOG_H_
#define FLY_COMMANDS_PALETTE_CATALOG_H_

#include <string>
#include <string_view>
#include <utility>
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
  // Command Centre texts (Electron COMMAND_DEFINITIONS).
  std::string desc;
  std::string details;
  std::string tip;
  std::vector<std::string> keywords;
  // Nesting level of the row (the dashboard lists tabs under workspaces).
  int indent = 0;
  // Extra buttons on the row: {action, label}; each runs like a selection.
  std::vector<std::pair<std::string, std::string>> row_actions;
};
// Explicit product entries followed by remaining catalogue actions. Bindings
// come from the current configuration, including deliberately disabled keys.
std::vector<PaletteEntry> PaletteCatalog(const KeyboardConfig& config);
std::vector<PaletteEntry> FilterPalette(
    const std::vector<PaletteEntry>& entries,
    std::u16string_view query);
// Electron's Command Centre list: only its COMMAND_DEFINITIONS, with the
// current bindings (the keyboard editor lists every action).
std::vector<PaletteEntry> CommandCentreCatalog(const KeyboardConfig& config);
// Electron's Command Centre search: the whole query, case-insensitive, in the
// title, description, details, category, keywords or a binding.
std::vector<PaletteEntry> FilterCommandCentre(
    const std::vector<PaletteEntry>& entries,
    std::u16string_view query);
// Electron's Command Centre filter chips, in order ("All" first).
const std::vector<std::string>& CommandCentreCategories();
}  // namespace fly
#endif
