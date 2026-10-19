// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_NATIVE_COMMANDS_H_
#define FLY_UI_NATIVE_COMMANDS_H_
#include <string>
#include <string_view>
#include "content/public/browser/keyboard_event_processing_result.h"

class BrowserWindowInterface;
namespace input {
struct NativeWebKeyboardEvent;
}
namespace content {
class WebContents;
}
namespace fly {
// Opens an actual about:blank navigation in this browser's own profile.
// A nonnegative index requests native tab-strip placement, including pin rules.
bool OpenNativeBlankTab(BrowserWindowInterface* browser, int index = -1);

// Retained-document native freezing votes. A request is not a claim that the
// policy has frozen the page: Chromium keeps its connected-page/protection rules.
bool RequestNativeTabSuspension(content::WebContents* contents);
void ResumeNativeTab(content::WebContents* contents);
bool NativeTabSuspensionRequested(content::WebContents* contents);

std::string NativeEventStroke(const input::NativeWebKeyboardEvent& event);
std::u16string NativePageModeLabel(BrowserWindowInterface* browser);

// Runs through the existing profile-bound tab/window services. False denotes an
// action that still needs Fly UI, never a successful no-op.
bool InvokeNativeCommand(BrowserWindowInterface* browser, std::string action);
bool NativeCommandAvailable(BrowserWindowInterface* browser,
                            std::string_view action);
bool ExecuteNativeCommand(BrowserWindowInterface* browser,
                          std::string_view action);
content::KeyboardEventProcessingResult RouteNativePageKey(
    BrowserWindowInterface* browser,
    const input::NativeWebKeyboardEvent& event,
    bool browser_ui_editable = false);
}  // namespace fly
#endif  // FLY_UI_NATIVE_COMMANDS_H_
