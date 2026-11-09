# Drawing & Taskbar

> Rendering and taskbar embedding. Two distinct surfaces (main floating window
> vs. taskbar window) with separate code paths but a shared drawing abstraction.

---

## Drawing abstraction

`IDrawCommon` (`TrafficMonitor/IDrawCommon.h`) is the single drawing interface.
It declares stretch mode, alignment, and the core primitives (`DrawWindowText`,
`FillRect`, `DrawRectOutLine`, `SetDrawRect`, …). Two implementations:

| Impl | Used by | File |
|------|---------|------|
| `CDrawCommon final : public IDrawCommon` | Main floating window (GDI) | `TrafficMonitor/DrawCommon.h` |
| `CTaskBarDlgDrawCommon final : public IDrawCommon` | Taskbar window (GDI/D2D) | `TrafficMonitor/TaskBarDlgDrawCommon.h` |

D2D / DirectComposition support lives in `TrafficMonitor/TaskBarDlgDrawCommon.*`
and the support classes `CTaskBarDlgDrawCommonSupport`,
`CTaskBarDlgDrawCommonWindowSupport`, plus `CTaskBarDlgDrawBuffer` /
`CTaskBarDlgDrawBufferUseDComposition`. The D2D global dependency is wrapped in
`CLazyConstructable<CTaskBarDlgDrawCommonSupport>` on `CTrafficMonitorApp`
(`TrafficMonitor/TrafficMonitor.h:110`) so it initializes only when D2D
rendering is active.

DXGI/D2D/D3D10 plumbing: `TrafficMonitor/Dxgi1Support2.*`,
`TrafficMonitor/D2D1Support.*`, `TrafficMonitor/D3D10Support1.*`,
`TrafficMonitor/DCompositionSupport.*`.

**Do not introduce a second drawing abstraction.** Extend `IDrawCommon` (add a
virtual, then implement in both `CDrawCommon` and `CTaskBarDlgDrawCommon`) when
you need a new primitive.

## Main window drawing

Main-window rendering uses `CSkinFile` (`TrafficMonitor/SkinFile.*`) and the
`CDrawCommon` path. Skins ship under `TrafficMonitor/skins/`; each skin is a
subfolder with `background.bmp/png`, `background_l.bmp/png`, and `skin.ini`
(INI) or `skin.xml` (XML, supports temperature display). Skin authoring is
documented in `皮肤制作教程.md`.

## Taskbar window variants

`CTaskBarDlg` (`TrafficMonitor/TaskBarDlg.h`, derives from `CDialogEx`) is the
base. There are three environment-specific variants, each derived from it:

| Variant | When | File |
|---------|------|------|
| `CClassicalTaskbarDlg` | Classic (Win10 and earlier) taskbar | `TrafficMonitor/ClassicalTaskbarDlg.h` |
| `CWin11TaskbarDlg` | Windows 11 taskbar | `TrafficMonitor/Win11TaskbarDlg.h` |
| `CWineTaskbarDlg` | Wine | `TrafficMonitor/WineTaskbarDlg.h` |

Forking behavior (current focus areas) includes Win11 multi-monitor taskbar
behavior and DPI/display-change handling. Constants related to taskbar embedding
attempts and transparent colors live in `TrafficMonitor/stdafx.h` (e.g.
`MAX_INSERT_TO_TASKBAR_CNT`, `TASKBAR_TRANSPARENT_COLOR1/2`).

When reopening the taskbar window (after settings change, DPI change, or display
reconfiguration), use the existing reopen path — `WM_REOPEN_TASKBAR_WND`
(`stdafx.h:72`) and `RESTART_TASKBAR_TIMER` (1240). Do not create a parallel
recreate path.

## Taskbar item colors

Taskbar items are drawn in two graph modes, and both resolve color through the
same seam:

| Mode | Function | Setting |
|------|----------|---------|
| Bar / status bar | `CTaskBarDlg::TryDrawStatusBar(drawer, rect_bar, item, usage_percent)` | `show_status_bar`, `show_netspeed_figure` |
| Plot / history | `CTaskBarDlg::TryDrawGraph(drawer, value_rect, item_type)` | graph display mode |

`TaskBarSettingData` owns the colors (`TrafficMonitor/CommonData.h`):

- `COLORREF GetUsageGraphColor() const` — the global/effective graph color,
  including the `graph_color_following_system` accent resolution.
- `COLORREF GetUsageGraphColor(CommonDisplayItem item) const` — per-item
  override, falling back to the no-argument overload when
  `specify_each_item_graph_color` is off or the item has no entry.

Rules:

- **Resolve color inside the two draw functions, not at the call sites.** This
  is why `CClassicalTaskbarDlg` / `CWin11TaskbarDlg` / `CWineTaskbarDlg` need no
  per-variant change — a new variant inherits the behavior automatically.
- Do not branch on `specify_each_item_graph_color` in drawing code; the
  item-aware `GetUsageGraphColor` overload is the single decision point.
- Both built-in and plugin items convert implicitly to `CommonDisplayItem`, so
  one signature serves both — do not add parallel `DisplayItem`/`IPluginItem`
  overloads.
- The dashed-box outline (`show_graph_dashed_box`) uses the same resolved color
  as the fill.
- Items without a configured color must fall back to the global color, never to
  an uninitialized `COLORREF`.

Analogous behavior applies to per-item *text* colors (`text_colors`), which
predate graph colors and follow the same item-keyed map shape.

Reference files:
- `TrafficMonitor/IDrawCommon.h`, `TrafficMonitor/DrawCommon.h`,
  `TrafficMonitor/TaskBarDlgDrawCommon.h`
- `TrafficMonitor/TaskBarDlg.h`, `TrafficMonitor/ClassicalTaskbarDlg.h`,
  `TrafficMonitor/Win11TaskbarDlg.h`, `TrafficMonitor/WineTaskbarDlg.h`
- `TrafficMonitor/SkinFile.h`, `TrafficMonitor/SkinManager.h`
- `TrafficMonitor/CommonData.h` (`TaskBarSettingData`),
  `TrafficMonitor/TaskBarSettingsDlg.h`, `TrafficMonitor/TaskbarColorDlg.h`

## Windows 11 tray space reservation

Opt-in `TaskBarSettingData::reserve_taskbar_space` reserves actual tray slots through
`CTaskbarTrayReserve` (`TaskbarButtonSpacer.*`), ported from upstream PR #2387
through commit `991f80c8d2aa36dd9fe93d4fad4f5219ce755897`. This adds transparent
`Shell_NotifyIcon` entries and marks only those entries always visible. A cached UI
Automation query on a worker thread locates their actual bounds; the readout sits
inside that measured region. No Explorer injection or XAML style is used.

- Default is off; load/save and the Win11 settings checkbox share the existing settings path.
- Reservation supports the primary horizontal taskbar; secondary and vertical placement
  retain the existing placement path. Disabling reservation preserves the fork's original
  alignment, coordinate conversion and secondary-display clock fallback.
- DPI changes reset the measured tray pitch. Explorer restarts re-register the slots.
- A foreign icon inside the block makes the readout hide until the obstruction is removed.
- Normal exit removes live icons but retains the fixed per-icon shell state for reuse;
  switching the option off purges only this feature's GUID-prefixed settings.
- `WM_SPACER_LAYOUT_CHANGED` uses `WM_USER+1012`; `WM_USER+1011` remains owned by
  the fork's network reinitialization message. Checkbox ID 1224 avoids existing controls.
- Placement compares the desired rectangle with the actual child-window rectangle so
  horizontal/vertical offset settings apply even when the reserved region stays still.

Validate full and Lite x64 via the branch-only `taskbar-reservation.yml` workflow
on windows-2022. Runtime acceptance requires an enabled reservation at 3840x2160
with 150% scaling, no task-button or foreign-icon intersection, and working offsets,
close/reopen and Explorer-restart recovery. Record any scenario left untested.

### Startup responsiveness

UI Automation must start at the Explorer-owned
`Windows.UI.Composition.DesktopWindowContentBridge` child, not `Shell_TrayWnd`.
The latter also contains TrafficMonitor's foreign-process MFC child: descendant
enumeration can synchronously call back into its UI thread while that thread
waits in `Shell_NotifyIcon`. A captured startup wait chain showed Explorer waiting
on a send-message to TrafficMonitor; stopping TrafficMonitor recovered the same
Explorer process. Keep fixed-GUID notification settings on startup as well as
normal exit; only explicitly disabling the feature purges them.

After changes, verify repeated launches with a bounded taskbar response probe.
The probe must stop only the test TrafficMonitor on failure, preserve the wait
chain, and confirm Explorer recovers without a restart. Also inspect that the
readout still occupies the transparent tray slots at the current DPI.
