// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_NATIVE_COMMANDS_H_
#define FLY_UI_NATIVE_COMMANDS_H_
#include <string_view>
#include "content/public/browser/keyboard_event_processing_result.h"

class BrowserWindowInterface;
namespace input {
struct NativeWebKeyboardEvent;
}
namespace fly {
// Runs through the existing profile-bound tab/window services. False denotes an
// action that still needs Fly UI, never a successful no-op.
bool ExecuteNativeCommand(BrowserWindowInterface* browser,
                          std::string_view action);
content::KeyboardEventProcessingResult RouteNativePageKey(
    BrowserWindowInterface* browser,
    const input::NativeWebKeyboardEvent& event,
    bool browser_ui_editable = false);
}  // namespace fly
#endif  // FLY_UI_NATIVE_COMMANDS_H_
