// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/diagnostics_hub.h"

#include <algorithm>
#include <utility>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/i18n/time_formatting.h"
#include "base/json/json_writer.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/process/process_metrics.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "base/version_info/version_info.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/process_type.h"
#include "fly/fly_version.h"
#include "fly/ui/fly_menu.h"
#include "fly/ui/fly_theme.h"
#include "gpu/config/gpu_feature_info.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/events/event.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "v8/include/v8-version-string.h"

namespace fly {
namespace {
constexpr char kIssuesPage[] =
    "https://github.com/Arzuparreta/fly-browser/issues";

// Paths, the user name, URL queries and local network addresses never leave
// a report.
std::string Sanitise(std::string text) {
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home) &&
      home.value().size() > 1)
    base::ReplaceSubstringsAfterOffset(&text, 0, home.value(), "~");
  auto env = base::Environment::Create();
  if (const auto user = env->GetVar("USER"); user && user->size() > 2)
    base::ReplaceSubstringsAfterOffset(&text, 0, *user, "<user>");
  std::string out;
  for (size_t i = 0; i < text.size();) {
    if (text.compare(i, 4, "http") == 0) {
      size_t end = i;
      while (end < text.size() && !base::IsAsciiWhitespace(text[end]))
        ++end;
      std::string url = text.substr(i, end - i);
      if (const auto query = url.find('?'); query != std::string::npos)
        url = url.substr(0, query) + "?…";
      out += url;
      i = end;
      continue;
    }
    out += text[i++];
  }
  for (const char* prefix : {"192.168.", "10.", "172.16.", "172.17.",
                             "172.18.", "172.19.", "172.2", "172.30.",
                             "172.31."}) {
    size_t at = 0;
    while ((at = out.find(prefix, at)) != std::string::npos) {
      size_t end = at;
      while (end < out.size() &&
             (base::IsAsciiDigit(out[end]) || out[end] == '.'))
        ++end;
      if (std::ranges::count(out.substr(at, end - at), '.') == 3 &&
          (at == 0 || !base::IsAsciiDigit(out[at - 1]))) {
        out.replace(at, end - at, "<private-ip>");
        at += 12;
      } else {
        at = end;
      }
    }
  }
  return out;
}

std::string FeatureStatus(gpu::GpuFeatureStatus status) {
  switch (status) {
    case gpu::kGpuFeatureStatusEnabled:
      return "enabled";
    case gpu::kGpuFeatureStatusBlocklisted:
      return "unavailable_off";
    case gpu::kGpuFeatureStatusDisabled:
      return "disabled_off";
    case gpu::kGpuFeatureStatusSoftware:
      return "disabled_software";
    default:
      return "unavailable";
  }
}

std::vector<std::pair<std::string, std::string>> GpuFeatures() {
  auto* manager = content::GpuDataManager::GetInstance();
  const auto status = [&](gpu::GpuFeatureType type) {
    return FeatureStatus(manager->GetFeatureStatus(type));
  };
  return {
      {"2d_canvas", status(gpu::GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS)},
      {"gpu_compositing",
       manager->HardwareAccelerationEnabled() ? "enabled"
                                              : "disabled_software"},
      {"webgl", status(gpu::GPU_FEATURE_TYPE_ACCELERATED_WEBGL)},
      {"webgl2", status(gpu::GPU_FEATURE_TYPE_ACCELERATED_WEBGL2)},
      {"video_decode", status(gpu::GPU_FEATURE_TYPE_ACCELERATED_VIDEO_DECODE)},
      {"rasterization", status(gpu::GPU_FEATURE_TYPE_GPU_TILE_RASTERIZATION)},
  };
}

std::string DesktopSession() {
  auto env = base::Environment::Create();
  return env->GetVar("XDG_CURRENT_DESKTOP")
      .value_or(env->GetVar("DESKTOP_SESSION").value_or("unknown"));
}
std::string SessionType() {
  auto env = base::Environment::Create();
  return env->GetVar("XDG_SESSION_TYPE")
      .value_or(env->GetVar("WAYLAND_DISPLAY") ? "wayland" : "x11");
}

FlyLabel* AddText(views::View* parent,
                  std::u16string text,
                  int size = 12,
                  FlyLabel::Tone tone = FlyLabel::Tone::kText,
                  FlyFace face = FlyFace::kSans,
                  gfx::Font::Weight weight = gfx::Font::Weight::NORMAL) {
  auto* label = parent->AddChildView(
      std::make_unique<FlyLabel>(std::move(text), face, size, weight, tone));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

views::View* AddBox(views::View* parent,
                    views::BoxLayout::Orientation orientation,
                    int spacing,
                    gfx::Insets insets = {}) {
  auto* view = parent->AddChildView(std::make_unique<views::View>());
  auto* layout = view->SetLayoutManager(
      std::make_unique<views::BoxLayout>(orientation, insets, spacing));
  layout->set_cross_axis_alignment(
      orientation == views::BoxLayout::Orientation::kVertical
          ? views::BoxLayout::CrossAxisAlignment::kStretch
          : views::BoxLayout::CrossAxisAlignment::kCenter);
  return view;
}

// .diag-card: a titled list of key/value rows.
views::View* AddCard(views::View* parent, std::u16string title) {
  auto* card = AddBox(parent, views::BoxLayout::Orientation::kVertical, 6);
  card->SetBackground(FlyBackground(FlyRole::kOverlayMain, 8));
  card->SetBorder(FlyBorder(FlyRole::kBorder, 1, 8, gfx::Insets(14)));
  card->AddChildView(MakeMenuCaption(base::ToUpperASCII(title)));
  return card;
}
void AddRow(views::View* card, std::u16string key, std::u16string value) {
  auto* row = AddBox(card, views::BoxLayout::Orientation::kHorizontal, 12);
  auto* name = AddText(row, std::move(key), 12, FlyLabel::Tone::kDim);
  static_cast<views::BoxLayout*>(row->GetLayoutManager())
      ->SetFlexForView(name, 1);
  AddText(row, std::move(value), 12, FlyLabel::Tone::kText, FlyFace::kMono);
}
FlyButton* AddButton(views::View* parent,
                     std::u16string label,
                     views::Button::PressedCallback callback,
                     bool primary = false) {
  return parent->AddChildView(MakeMenuButton(
      std::move(callback), std::move(label),
      primary ? MenuButtonStyle::kPrimary : MenuButtonStyle::kSecondary));
}
}  // namespace

DiagnosticsHub::DiagnosticsHub(views::Widget* desktop, Delegate delegate)
    : desktop_(desktop),
      delegate_(std::move(delegate)),
      sampler_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE})) {
  desktop_->AddObserver(this);
}

DiagnosticsHub::~DiagnosticsHub() {
  weak_factory_.InvalidateWeakPtrs();
  if (file_dialog_)
    file_dialog_->ListenerDestroyed();
  panes_.clear();
  tabs_ = nullptr;
  badge_ = nullptr;
  recording_ = nullptr;
  log_search_ = nullptr;
  log_list_ = nullptr;
  scrim_ = nullptr;
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}

void DiagnosticsHub::Toggle() {
  if (overlay_) {
    Cancel();
    return;
  }
  if (!desktop_)
    return;
  const gfx::Rect bounds = SurfaceBounds();
  overlay_ = CreateMenuWidget(desktop_, bounds, "FlyDiagnosticsHub");
  auto scrim = std::make_unique<MenuScrim>(
      SK_ColorTRANSPARENT,
      base::BindRepeating(&DiagnosticsHub::Cancel, weak_factory_.GetWeakPtr()));
  auto panel = std::make_unique<MenuPanel>();
  auto* root = panel.get();
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(FlyScaled(12)),
      FlyScaled(10)));
  auto* header = root->AddChildView(
      std::make_unique<MenuHeader>(MenuKind::kDiagnostics, u"Diagnostics"));
  auto* spacer = header->AddTrailing(std::make_unique<views::View>());
  static_cast<views::BoxLayout*>(header->GetLayoutManager())
      ->SetFlexForView(spacer, 1);
  badge_ = header->AddStatus();
  recording_ = header->AddTrailing(std::make_unique<FlyToggle>(
      base::BindRepeating(
          [](base::WeakPtr<DiagnosticsHub> hub) {
            if (hub && hub->recording_ && hub->delegate_.set_recording) {
              hub->delegate_.set_recording.Run(hub->recording_->GetChecked());
              hub->Refresh();
            }
          },
          weak_factory_.GetWeakPtr()),
      u"Record events"));
  header->AddTrailing(std::make_unique<FlyLabel>(
      u"Record", FlyFace::kSans, kMenuSmallTextSize));
  header->AddClose(
      base::BindRepeating(&DiagnosticsHub::Cancel, weak_factory_.GetWeakPtr()));
  tabs_ = root->AddChildView(std::make_unique<views::TabbedPane>(
      views::TabbedPane::Orientation::kHorizontal,
      views::TabbedPane::TabStripStyle::kBorder));
  static_cast<views::BoxLayout*>(root->GetLayoutManager())
      ->SetFlexForView(tabs_, 1);
  panes_.clear();
  for (const auto* title : {u"System & Runtimes", u"Processes & Memory",
                            u"Event Log", u"Bug Report"}) {
    auto* scroll = tabs_->AddTab(title, std::make_unique<views::ScrollView>());
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->ClipHeightTo(0, std::numeric_limits<int>::max());
    scroll->SetBackgroundColor(std::nullopt);
    auto* pane = scroll->SetContents(std::make_unique<views::View>());
    pane->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 12));
    panes_.push_back(pane);
  }
  tabs_->SetListener(this);
  scrim->SetPanel(std::move(panel));
  scrim_ = scrim.get();
  PlacePanel(bounds);
  DisableSubpixelText(scrim.get());
  overlay_->SetContentsView(std::move(scrim));
  Refresh();
  Sample();
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
}

gfx::Rect DiagnosticsHub::SurfaceBounds() const {
  return MenuWidgetBounds(desktop_, delegate_.page_insets
                                        ? delegate_.page_insets.Run()
                                        : gfx::Insets());
}

void DiagnosticsHub::PlacePanel(const gfx::Rect& bounds) {
  const int margin = FlyScaled(16);
  scrim_->SetPanelBounds(gfx::Rect(margin, margin,
                                   std::max(0, bounds.width() - 2 * margin),
                                   std::max(0, bounds.height() - 2 * margin)));
}

void DiagnosticsHub::Cancel() {
  if (!overlay_)
    return;
  scrim_ = nullptr;
  if (tabs_)
    tabs_->SetListener(nullptr);
  panes_.clear();
  tabs_ = nullptr;
  badge_ = nullptr;
  recording_ = nullptr;
  log_search_ = nullptr;
  log_list_ = nullptr;
  auto closing = std::move(overlay_);
  closing->CloseNow();
}

void DiagnosticsHub::Refresh() {
  if (!overlay_ || !tabs_)
    return;
  const auto session =
      delegate_.session ? delegate_.session.Run() : DiagnosticsSession();
  badge_->SetText(session.recording ? u"● Recording in memory"
                                    : u"Not recording");
  recording_->SetChecked(session.recording);
  const size_t index = tabs_->GetSelectedTabIndex();
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (i != index)
      continue;
    auto* pane = panes_[i].get();
    // The log search field survives refreshes while typing.
    const bool keep_search = i == 2 && log_search_ && log_search_->HasFocus();
    if (keep_search) {
      BuildLogList();
      continue;
    }
    log_search_ = nullptr;
    log_list_ = nullptr;
    pane->RemoveAllChildViews();
    if (i == 0)
      BuildSystem(pane);
    else if (i == 1)
      BuildProcesses(pane);
    else if (i == 2)
      BuildLogs(pane);
    else
      BuildReport(pane);
    DisableSubpixelText(pane);
    pane->InvalidateLayout();
  }
  DisableSubpixelText(overlay_->GetContentsView());
}

void DiagnosticsHub::BuildSystem(views::View* pane) {
  if (!snapshot_) {
    AddText(pane, u"Collecting system details…", 12, FlyLabel::Tone::kDim);
    return;
  }
  const auto& snapshot = *snapshot_;
  auto* grid = AddBox(pane, views::BoxLayout::Orientation::kVertical, 12);
  auto* top = AddBox(grid, views::BoxLayout::Orientation::kHorizontal, 12);
  auto* bottom = AddBox(grid, views::BoxLayout::Orientation::kHorizontal, 12);
  auto* runtimes = AddCard(top, u"APPLICATION RUNTIMES");
  AddRow(runtimes, u"Fly Browser",
         u"v" + base::UTF8ToUTF16(std::string(kFlyVersion)));
  AddRow(runtimes, u"Chromium Engine",
         u"v" + base::UTF8ToUTF16(std::string(version_info::GetVersionNumber())));
  AddRow(runtimes, u"V8 Engine", u"v" + base::UTF8ToUTF16(std::string_view(V8_VERSION_STRING)));
  AddRow(runtimes, u"Channel",
         base::UTF8ToUTF16(std::string(version_info::GetChannelString(
             version_info::Channel::STABLE))));
  auto* environment = AddCard(top, u"OPERATING ENVIRONMENT");
  AddRow(environment, u"Operating System",
         base::UTF8ToUTF16(snapshot.os_name + " (" + snapshot.arch + ")"));
  AddRow(environment, u"Kernel Release",
         base::UTF8ToUTF16(snapshot.os_version));
  AddRow(environment, u"Desktop Session", base::UTF8ToUTF16(DesktopSession()));
  AddRow(environment, u"Window Protocol", base::UTF8ToUTF16(SessionType()));
  const auto uptime = snapshot.uptime;
  AddRow(environment, u"System Uptime",
         base::NumberToString16(uptime.InHours()) + u"h " +
             base::NumberToString16(uptime.InMinutes() % 60) + u"m");
  auto* hardware = AddCard(bottom, u"HARDWARE RESOURCES");
  const auto total = snapshot.total_mb;
  const auto free = snapshot.free_mb;
  AddRow(hardware, u"Processor Model", base::UTF8ToUTF16(snapshot.cpu_model));
  AddRow(hardware, u"Logical Cores",
         base::NumberToString16(snapshot.cores) +
             u" threads");
  AddRow(hardware, u"Total Physical RAM",
         base::NumberToString16(total) + u" MB");
  AddRow(hardware, u"Free Available RAM", base::NumberToString16(free) + u" MB");
  AddRow(hardware, u"Memory Utilisation",
         base::NumberToString16(total ? (total - free) * 100 / total : 0) +
             u"% (" + base::NumberToString16(total - free) + u" MB)");
  auto* gpu = AddCard(bottom, u"GPU ACCELERATION PIPELINE");
  for (const auto& [name, status] : GpuFeatures())
    AddRow(gpu,
           base::UTF8ToUTF16(base::ToUpperASCII(name == "2d_canvas"
                                                     ? std::string("2D canvas")
                                                     : name)),
           base::UTF8ToUTF16(status));
  for (auto* row : {top, bottom})
    for (views::View* card : row->children())
      static_cast<views::BoxLayout*>(row->GetLayoutManager())
          ->SetFlexForView(card, 1);
}

// Lives on a blocking sequence; keeps per-process CPU counters between
// samples.
class DiagnosticsHub::Sampler {
 public:
  Snapshot Sample(std::vector<ProcessRow> processes) {
    Snapshot snapshot;
    snapshot.os_name = base::SysInfo::OperatingSystemName();
    snapshot.os_version = base::SysInfo::OperatingSystemVersion();
    snapshot.arch = base::SysInfo::OperatingSystemArchitecture();
    snapshot.cpu_model = base::SysInfo::CPUModelName();
    snapshot.cores = base::SysInfo::NumberOfProcessors();
    snapshot.total_mb = base::SysInfo::AmountOfTotalPhysicalMemory().InMiB();
    snapshot.free_mb =
        base::SysInfo::AmountOfAvailablePhysicalMemory().InMiB();
    snapshot.uptime = base::SysInfo::Uptime();
    std::map<base::ProcessId, std::unique_ptr<base::ProcessMetrics>> next;
    for (auto& row : processes) {
      auto found = metrics_.find(row.pid);
      auto metrics = found != metrics_.end()
                         ? std::move(found->second)
                         : base::ProcessMetrics::CreateProcessMetrics(row.pid);
      if (const auto info = metrics->GetMemoryInfo(); info.has_value())
        row.resident = info->resident_set_bytes;
      if (const auto cpu = metrics->GetPlatformIndependentCPUUsage();
          cpu.has_value())
        row.cpu = *cpu;
      next[row.pid] = std::move(metrics);
    }
    metrics_ = std::move(next);
    snapshot.processes = std::move(processes);
    return snapshot;
  }

 private:
  std::map<base::ProcessId, std::unique_ptr<base::ProcessMetrics>> metrics_;
};

void DiagnosticsHub::Sample() {
  std::vector<ProcessRow> processes;
  processes.push_back({base::Process::Current().Pid(), u"Browser"});
  for (auto it = content::RenderProcessHost::AllHostsIterator(); !it.IsAtEnd();
       it.Advance()) {
    auto* host = it.GetCurrentValue();
    if (host->GetProcess().IsValid())
      processes.push_back({host->GetProcess().Pid(), u"Renderer"});
  }
  for (content::BrowserChildProcessHostIterator it; !it.Done(); ++it) {
    const auto& data = it.GetData();
    const auto& process = it.GetProcess();
    if (!process.IsValid())
      continue;
    processes.push_back(
        {process.Pid(), data.process_type == content::PROCESS_TYPE_GPU ? u"GPU"
                        : data.process_type == content::PROCESS_TYPE_UTILITY
                            ? u"Utility: " + data.name
                            : data.name});
  }
  sampler_.AsyncCall(&Sampler::Sample)
      .WithArgs(std::move(processes))
      .Then(base::BindOnce(&DiagnosticsHub::OnSample,
                           weak_factory_.GetWeakPtr()));
}

void DiagnosticsHub::OnSample(Snapshot snapshot) {
  snapshot_ = std::move(snapshot);
  Refresh();
}

void DiagnosticsHub::BuildProcesses(views::View* pane) {
  const auto session =
      delegate_.session ? delegate_.session.Run() : DiagnosticsSession();
  if (!snapshot_) {
    AddText(pane, u"Collecting process metrics…", 12, FlyLabel::Tone::kDim);
    return;
  }
  const auto& rows = snapshot_->processes;
  uint64_t total = 0;
  uint64_t browser = 0;
  for (const auto& row : rows) {
    total += row.resident;
    if (row.type == u"Browser")
      browser = row.resident;
  }
  auto* stats = AddBox(pane, views::BoxLayout::Orientation::kHorizontal, 12);
  for (const auto& [label, value] :
       {std::pair{u"ACTIVE PROCESSES", base::NumberToString16(rows.size())},
        std::pair{u"AGGREGATE MEMORY (RSS)",
                  base::NumberToString16(total / (1024 * 1024)) + u" MB"},
        std::pair{u"BROWSER PROCESS",
                  base::NumberToString16(browser / (1024 * 1024)) + u" MB"},
        std::pair{u"SUSPENDED TABS",
                  base::NumberToString16(session.suspended) + u" / " +
                      base::NumberToString16(session.tabs)}}) {
    auto* card = AddCard(stats, std::u16string(label));
    AddText(card, value, 18, FlyLabel::Tone::kText, FlyFace::kMono,
            gfx::Font::Weight::SEMIBOLD);
    static_cast<views::BoxLayout*>(stats->GetLayoutManager())
        ->SetFlexForView(card, 1);
  }
  auto* actions = AddBox(pane, views::BoxLayout::Orientation::kHorizontal, 8);
  auto* hint = AddText(
      actions,
      u"Workspaces: " + base::NumberToString16(session.workspaces) +
          u" | Current: " + session.active_workspace +
          u" | Active Frames: " + base::NumberToString16(session.loaded),
      11, FlyLabel::Tone::kDim);
  static_cast<views::BoxLayout*>(actions->GetLayoutManager())
      ->SetFlexForView(hint, 1);
  AddButton(actions, u"Compact RAM",
            base::BindRepeating(
                [](base::WeakPtr<DiagnosticsHub> hub) {
                  // Asks every process to release what it can.
                  base::MemoryPressureListener::NotifyMemoryPressure(
                      base::MEMORY_PRESSURE_LEVEL_CRITICAL);
                  if (hub)
                    hub->Sample();
                },
                weak_factory_.GetWeakPtr()));
  AddButton(actions, u"Refresh",
            base::BindRepeating(&DiagnosticsHub::Sample,
                                weak_factory_.GetWeakPtr()));
  auto* table = AddBox(pane, views::BoxLayout::Orientation::kVertical, 2);
  table->SetBackground(FlyBackground(FlyRole::kOverlaySidebar, 8));
  table->SetBorder(FlyBorder(FlyRole::kBorder, 1, 8, gfx::Insets(10)));
  const auto add_row = [&](std::u16string pid, std::u16string type,
                           std::u16string cpu, std::u16string memory,
                           bool head) {
    auto* row = AddBox(table, views::BoxLayout::Orientation::kHorizontal, 12);
    const auto tone = head ? FlyLabel::Tone::kAccent : FlyLabel::Tone::kText;
    auto* pid_label = AddText(row, std::move(pid), 11, tone, FlyFace::kMono);
    pid_label->SetPreferredSize(gfx::Size(90, 18));
    auto* type_label = AddText(row, std::move(type), 11, tone);
    static_cast<views::BoxLayout*>(row->GetLayoutManager())
        ->SetFlexForView(type_label, 1);
    auto* cpu_label = AddText(row, std::move(cpu), 11, tone, FlyFace::kMono);
    cpu_label->SetPreferredSize(gfx::Size(70, 18));
    auto* memory_label =
        AddText(row, std::move(memory), 11, tone, FlyFace::kMono);
    memory_label->SetPreferredSize(gfx::Size(110, 18));
  };
  add_row(u"PID", u"Subsystem Type", u"CPU", u"Memory (RSS)", true);
  for (const auto& row : rows)
    add_row(base::NumberToString16(row.pid), row.type,
            base::UTF8ToUTF16(base::StringPrintf("%.1f%%", row.cpu)),
            base::NumberToString16(row.resident / (1024 * 1024)) + u" MB",
            false);
}

void DiagnosticsHub::BuildLogs(views::View* pane) {
  const auto session =
      delegate_.session ? delegate_.session.Run() : DiagnosticsSession();
  if (!session.recording) {
    auto* card = AddCard(pane, u"DIAGNOSTIC RECORDING IS CURRENTLY DISABLED");
    auto* text = AddText(
        card,
        u"Turn it on to keep a log of browser events in memory. Nothing is "
        u"written to disk or sent anywhere; the log is lost when Fly closes.",
        12, FlyLabel::Tone::kDim);
    text->SetMultiLine(true);
    AddButton(card, u"Enable Diagnostics Recording",
              base::BindRepeating(
                  [](base::WeakPtr<DiagnosticsHub> hub) {
                    if (hub && hub->delegate_.set_recording) {
                      hub->delegate_.set_recording.Run(true);
                      hub->Refresh();
                    }
                  },
                  weak_factory_.GetWeakPtr()),
              true);
    return;
  }
  const auto count = [&](std::string_view level) {
    return std::ranges::count_if(session.events, [&](const auto& event) {
      return level == "all" || event.level == level;
    });
  };
  auto* bar = AddBox(pane, views::BoxLayout::Orientation::kHorizontal, 8);
  for (const auto& [filter, label] :
       {std::pair{"all", u"All (" + base::NumberToString16(count("all")) + u")"},
        std::pair{"error", std::u16string(u"Errors")},
        std::pair{"warn", std::u16string(u"Warnings")},
        std::pair{"info", std::u16string(u"Info")}}) {
    auto* chip = AddButton(
        bar, label,
        base::BindRepeating(
            [](base::WeakPtr<DiagnosticsHub> hub, std::string filter) {
              if (!hub)
                return;
              hub->log_filter_ = filter;
              hub->Refresh();
            },
            weak_factory_.GetWeakPtr(), std::string(filter)));
    chip->SetSelected(log_filter_ == filter);
  }
  log_search_ = bar->AddChildView(std::make_unique<views::Textfield>());
  log_search_->SetPlaceholderText(u"Filter messages");
  log_search_->GetViewAccessibility().SetName(u"Filter log messages");
  log_search_->SetText(log_query_);
  log_search_->set_controller(this);
  static_cast<views::BoxLayout*>(bar->GetLayoutManager())
      ->SetFlexForView(log_search_, 1);
  AddButton(bar, u"Clear",
            base::BindRepeating(
                [](base::WeakPtr<DiagnosticsHub> hub) {
                  if (hub && hub->delegate_.clear_events) {
                    hub->delegate_.clear_events.Run();
                    hub->Refresh();
                  }
                },
                weak_factory_.GetWeakPtr()));
  log_list_ = AddBox(pane, views::BoxLayout::Orientation::kVertical, 4);
  BuildLogList();
}

// Only the rows change while filtering: the search field sending the edit
// stays alive.
void DiagnosticsHub::BuildLogList() {
  if (!log_list_)
    return;
  auto* list = log_list_.get();
  list->RemoveAllChildViews();
  const auto session =
      delegate_.session ? delegate_.session.Run() : DiagnosticsSession();
  const auto query = base::ToLowerASCII(log_query_);
  int shown = 0;
  for (const auto& event : base::Reversed(session.events)) {
    if (log_filter_ != "all" && event.level != log_filter_)
      continue;
    if (!query.empty() &&
        base::ToLowerASCII(event.message).find(query) == std::u16string::npos &&
        base::ToLowerASCII(base::UTF8ToUTF16(event.category)).find(query) ==
            std::u16string::npos)
      continue;
    ++shown;
    auto* row = AddBox(list, views::BoxLayout::Orientation::kHorizontal, 10);
    AddText(row, base::TimeFormatTimeOfDayWithMilliseconds(event.time), 11,
            FlyLabel::Tone::kDim, FlyFace::kMono);
    AddText(row, base::UTF8ToUTF16(base::ToUpperASCII(event.level)), 11,
            event.level == "error"  ? FlyLabel::Tone::kAccent
            : event.level == "warn" ? FlyLabel::Tone::kLink
                                    : FlyLabel::Tone::kDim,
            FlyFace::kMono, gfx::Font::Weight::SEMIBOLD);
    AddText(row, base::UTF8ToUTF16(event.category), 11, FlyLabel::Tone::kDim,
            FlyFace::kMono);
    auto* message = AddText(row, event.message, 12);
    message->SetMultiLine(true);
    static_cast<views::BoxLayout*>(row->GetLayoutManager())
        ->SetFlexForView(message, 1);
  }
  if (!shown)
    AddText(list, u"No events match.", 12, FlyLabel::Tone::kDim);
  DisableSubpixelText(list);
  list->InvalidateLayout();
  if (list->parent())
    list->parent()->InvalidateLayout();
}

std::string DiagnosticsHub::MarkdownReport() {
  const auto session =
      delegate_.session ? delegate_.session.Run() : DiagnosticsSession();
  std::string md = "### Fly Browser Diagnostic Report\n";
  md += "_Generated: " +
        base::TimeFormatAsIso8601(base::Time::Now()) +
        "_\n";
  md += "_Privacy status: 100% local, scrubbed of user paths, auth tokens, and "
        "private IPs._\n\n";
  md += "#### 1. Software Environment\n";
  md += "- **Fly Version**: v" + std::string(kFlyVersion) + "\n";
  md += "- **Chromium**: v" + std::string(version_info::GetVersionNumber()) +
        "\n";
  md += "- **V8**: v" + std::string(V8_VERSION_STRING) + "\n";
  const Snapshot snapshot = snapshot_.value_or(Snapshot());
  md += "- **OS**: " + snapshot.os_name + " (" + snapshot.arch +
        ") - Kernel " + snapshot.os_version + "\n";
  md += "- **Desktop / Protocol**: " + DesktopSession() + " (" +
        SessionType() + ")\n\n";
  md += "#### 2. Hardware & GPU Pipeline\n";
  const auto total = snapshot.total_mb;
  const auto free = snapshot.free_mb;
  md += "- **CPU**: " + snapshot.cpu_model + " (" +
        base::NumberToString(snapshot.cores) +
        " cores)\n";
  md += "- **Physical Memory**: Total " + base::NumberToString(total) +
        " MB | Free " + base::NumberToString(free) + " MB | Utilisation " +
        base::NumberToString(total ? (total - free) * 100 / total : 0) + "%\n";
  for (const auto& [name, status] : GpuFeatures())
    md += "- **" + name + "**: " + status + "\n";
  md += "\n#### 3. Session & Process State\n";
  const auto& processes = snapshot.processes;
  md += "- **Workspaces**: " + base::NumberToString(session.workspaces) + "\n";
  md += "- **Total Tabs**: " + base::NumberToString(session.tabs) +
        " (Suspended: " + base::NumberToString(session.suspended) +
        ", Loaded: " + base::NumberToString(session.loaded) + ")\n";
  md += "- **Total Chromium Processes**: " +
        base::NumberToString(processes.size()) + "\n";
  if (!session.events.empty()) {
    md += "\n#### 4. Recent Sanitised Operational Logs\n```\n";
    const size_t start =
        session.events.size() > 40 ? session.events.size() - 40 : 0;
    for (size_t i = start; i < session.events.size(); ++i) {
      const auto& event = session.events[i];
      md += "[" +
            base::UTF16ToUTF8(
                base::TimeFormatTimeOfDayWithMilliseconds(event.time)) +
            "] [" + base::ToUpperASCII(event.level) + "] [" + event.category +
            "] " + base::UTF16ToUTF8(event.message) + "\n";
    }
    md += "```\n";
  }
  return Sanitise(std::move(md));
}

std::string DiagnosticsHub::ReportJson() {
  const Snapshot snapshot = snapshot_.value_or(Snapshot());
  const auto session =
      delegate_.session ? delegate_.session.Run() : DiagnosticsSession();
  base::DictValue report;
  report.Set("timestamp",
             base::TimeFormatAsIso8601(base::Time::Now()));
  report.Set("application",
             base::DictValue()
                 .Set("version", kFlyVersion)
                 .Set("chrome", std::string(version_info::GetVersionNumber()))
                 .Set("v8", V8_VERSION_STRING));
  report.Set("system",
             base::DictValue()
                 .Set("platform", snapshot.os_name)
                 .Set("arch", snapshot.arch)
                 .Set("osRelease", snapshot.os_version)
                 .Set("desktopSession", DesktopSession())
                 .Set("sessionType", SessionType()));
  base::DictValue gpu;
  for (const auto& [name, status] : GpuFeatures())
    gpu.Set(name, status);
  report.Set("gpu", base::DictValue().Set("features", std::move(gpu)));
  report.Set("session",
             base::DictValue()
                 .Set("workspaceCount", static_cast<int>(session.workspaces))
                 .Set("totalTabs", session.tabs)
                 .Set("frozenTabs", session.suspended));
  base::ListValue processes;
  for (const auto& row : snapshot.processes)
    processes.Append(base::DictValue()
                         .Set("pid", static_cast<int>(row.pid))
                         .Set("type", row.type)
                         .Set("cpuPercent", row.cpu)
                         .Set("residentSetKB",
                              static_cast<double>(row.resident / 1024)));
  report.Set("processes", std::move(processes));
  base::ListValue logs;
  for (const auto& event : session.events)
    logs.Append(base::DictValue()
                    .Set("time", base::TimeFormatAsIso8601(event.time))
                    .Set("level", event.level)
                    .Set("category", event.category)
                    .Set("message", event.message));
  report.Set("logs", std::move(logs));
  std::string json;
  base::JSONWriter::WriteWithOptions(
      report, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return Sanitise(std::move(json));
}

void DiagnosticsHub::BuildReport(views::View* pane) {
  const auto report = MarkdownReport();
  auto* banner = AddCard(pane, u"SANITISED BUG REPORT GENERATOR");
  auto* text = AddText(
      banner,
      u"Copy this report directly into a GitHub Issue. All file paths, "
      u"usernames, query strings, and local IPs have been pre-scrubbed.",
      12, FlyLabel::Tone::kDim);
  text->SetMultiLine(true);
  auto* actions = AddBox(banner, views::BoxLayout::Orientation::kHorizontal, 8);
  AddButton(actions, u"Copy Markdown Report",
            base::BindRepeating(
                [](base::WeakPtr<DiagnosticsHub> hub) {
                  if (!hub)
                    return;
                  ui::ScopedClipboardWriter writer(
                      ui::ClipboardBuffer::kCopyPaste);
                  writer.WriteText(base::UTF8ToUTF16(hub->MarkdownReport()));
                },
                weak_factory_.GetWeakPtr()),
            true);
  AddButton(actions, u"Export JSON",
            base::BindRepeating(
                [](base::WeakPtr<DiagnosticsHub> hub) {
                  if (!hub || hub->file_dialog_ || !hub->desktop_)
                    return;
                  hub->file_dialog_ =
                      ui::SelectFileDialog::Create(hub.get(), nullptr);
                  ui::SelectFileDialog::FileTypeInfo types;
                  types.extensions = {{FILE_PATH_LITERAL("json")}};
                  hub->file_dialog_->SelectFile(
                      ui::SelectFileDialog::SELECT_SAVEAS_FILE,
                      u"Export diagnostics",
                      base::FilePath(
                          FILE_PATH_LITERAL("fly-diagnostics.json")),
                      &types, 1, FILE_PATH_LITERAL("json"),
                      hub->desktop_->GetNativeWindow());
                },
                weak_factory_.GetWeakPtr()));
  AddButton(actions, u"Open Issues Page",
            base::BindRepeating(
                [](base::WeakPtr<DiagnosticsHub> hub) {
                  if (!hub || !hub->delegate_.open_url)
                    return;
                  auto open = hub->delegate_.open_url;
                  hub->Cancel();
                  open.Run(GURL(kIssuesPage));
                },
                weak_factory_.GetWeakPtr()));
  auto* preview = pane->AddChildView(std::make_unique<views::Textarea>());
  preview->SetText(base::UTF8ToUTF16(report));
  preview->SetReadOnly(true);
  preview->SetFontList(FlyFont(FlyFace::kMono, 11));
  preview->SetPreferredSize(gfx::Size(0, 360));
  preview->GetViewAccessibility().SetName(u"Diagnostic report");
}

void DiagnosticsHub::FileSelected(const ui::SelectedFileInfo& file, int) {
  file_dialog_.reset();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath path, std::string json) {
            base::WriteFile(path, json);
          },
          file.path(), ReportJson()));
}

void DiagnosticsHub::FileSelectionCanceled() {
  file_dialog_.reset();
}

void DiagnosticsHub::TabSelectedAt(int) {
  Refresh();
}

void DiagnosticsHub::ContentsChanged(views::Textfield* sender,
                                     const std::u16string& contents) {
  if (sender != log_search_)
    return;
  log_query_ = contents;
  BuildLogList();
}

bool DiagnosticsHub::HandleOverlayKey(const ui::KeyEvent& event) {
  if (!overlay_ || event.type() != ui::EventType::kKeyPressed)
    return false;
  if (event.key_code() == ui::VKEY_ESCAPE) {
    Cancel();
    return true;
  }
  if (event.key_code() == ui::VKEY_TAB && !event.IsControlDown())
    return CycleMenuFocus(overlay_->GetContentsView(), event.IsShiftDown());
  if (tabs_ && event.IsControlDown() &&
      (event.key_code() == ui::VKEY_NEXT || event.key_code() == ui::VKEY_PRIOR)) {
    const size_t count = tabs_->GetTabCount();
    tabs_->SelectTabAt((tabs_->GetSelectedTabIndex() +
                        (event.key_code() == ui::VKEY_NEXT ? 1 : count - 1)) %
                       count);
    return true;
  }
  return false;
}

// The window moved or was resized: the hub follows it and keeps its tab,
// log search and scroll position.
void DiagnosticsHub::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  if (!overlay_ || !scrim_)
    return;
  const gfx::Rect bounds = SurfaceBounds();
  overlay_->SetBounds(bounds);
  PlacePanel(bounds);
}

void DiagnosticsHub::OnWidgetDestroying(views::Widget*) {
  Cancel();
  desktop_ = nullptr;
}

void DiagnosticsHub::SelectTabForTesting(size_t index) {
  if (tabs_)
    tabs_->SelectTabAt(index);
}

std::u16string DiagnosticsHub::tab_text_for_testing() const {
  std::u16string text;
  const std::function<void(const views::View*)> collect =
      [&](const views::View* view) {
        if (const auto* label = views::AsViewClass<views::Label>(view))
          text += std::u16string(label->GetText()) + u"\n";
        for (const views::View* child : view->children())
          collect(child);
      };
  if (tabs_)
    collect(panes_[tabs_->GetSelectedTabIndex()].get());
  return text;
}
}  // namespace fly
