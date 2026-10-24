// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_DIAGNOSTICS_HUB_H_
#define FLY_UI_DIAGNOSTICS_HUB_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane_listener.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

namespace base {
class ProcessMetrics;
}
namespace ui {
class KeyEvent;
}
namespace views {
class Label;
class TabbedPane;
class Textfield;
class View;
class Widget;
}  // namespace views

namespace fly {
// One entry of the in-memory diagnostics log.
struct DiagnosticEvent {
  base::Time time;
  std::string level;     // info, warn or error.
  std::string category;  // status, update, process, extensions, …
  std::u16string message;
};
struct DiagnosticsSession {
  size_t workspaces = 0;
  std::u16string active_workspace;
  int tabs = 0;
  int suspended = 0;
  int loaded = 0;
  bool recording = false;
  std::vector<DiagnosticEvent> events;
};

// Diagnostics & Health Hub: a page over the content area with System &
// Runtimes, Processes & Memory, the event log and a sanitised bug report.
class FlyToggle;
class DiagnosticsHub : public views::TabbedPaneListener,
                       public views::TextfieldController,
                       public views::WidgetObserver,
                       public ui::SelectFileDialog::Listener {
 public:
  struct Delegate {
    base::RepeatingCallback<DiagnosticsSession()> session;
    base::RepeatingCallback<void(bool)> set_recording;
    base::RepeatingClosure clear_events;
    base::RepeatingCallback<void(GURL)> open_url;
    base::RepeatingCallback<gfx::Insets()> page_insets;
  };
  DiagnosticsHub(views::Widget* desktop, Delegate delegate);
  ~DiagnosticsHub() override;
  DiagnosticsHub(const DiagnosticsHub&) = delete;
  DiagnosticsHub& operator=(const DiagnosticsHub&) = delete;

  void Toggle();
  void Cancel();
  bool active() const { return overlay_ != nullptr; }
  // Rebuilds the visible tab from current data.
  void Refresh();
  bool HandleOverlayKey(const ui::KeyEvent& event);
  // The Markdown report to copy into a GitHub issue.
  std::string MarkdownReport();
  void SelectTabForTesting(size_t index);
  std::u16string tab_text_for_testing() const;

  // views::TabbedPaneListener:
  void TabSelectedAt(int index) override;
  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& contents) override;
  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetDestroying(views::Widget*) override;
  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
 public:
  struct ProcessRow {
    base::ProcessId pid = 0;
    std::u16string type;
    double cpu = 0;
    uint64_t resident = 0;
  };
  // What a blocking sequence reads from the system (/proc and friends).
  struct Snapshot {
    std::string os_name;
    std::string os_version;
    std::string arch;
    std::string cpu_model;
    int cores = 0;
    int64_t total_mb = 0;
    int64_t free_mb = 0;
    base::TimeDelta uptime;
    std::vector<ProcessRow> processes;
  };
  bool has_snapshot_for_testing() const { return snapshot_.has_value(); }

 private:
  class Sampler;
  void Sample();
  void OnSample(Snapshot snapshot);
  void BuildSystem(views::View* pane);
  void BuildProcesses(views::View* pane);
  void BuildLogs(views::View* pane);
  void BuildReport(views::View* pane);
  void BuildLogList();
  std::string ReportJson();

  raw_ptr<views::Widget> desktop_;
  Delegate delegate_;
  std::unique_ptr<views::Widget> overlay_;
  raw_ptr<views::TabbedPane> tabs_ = nullptr;
  std::vector<raw_ptr<views::View>> panes_;
  raw_ptr<views::Label> badge_ = nullptr;
  raw_ptr<FlyToggle> recording_ = nullptr;
  raw_ptr<views::Textfield> log_search_ = nullptr;
  raw_ptr<views::View> log_list_ = nullptr;
  std::string log_filter_ = "all";
  std::u16string log_query_;
  base::SequenceBound<Sampler> sampler_;
  std::optional<Snapshot> snapshot_;
  scoped_refptr<ui::SelectFileDialog> file_dialog_;
  base::WeakPtrFactory<DiagnosticsHub> weak_factory_{this};
};
}  // namespace fly

#endif  // FLY_UI_DIAGNOSTICS_HUB_H_
