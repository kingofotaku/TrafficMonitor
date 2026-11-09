#include "stdafx.h"
#include "Win11TaskbarDlg.h"
#include "WindowsSettingHelper.h"

namespace
{
    constexpr int SECONDARY_TASKBAR_RIGHT_SPACE_WIN11 = 88;
}

void CWin11TaskbarDlg::AdjustTaskbarWndPos(bool force_adjust)
{
    m_rcNotify.SetRectEmpty();
    if (::GetWindowRect(m_hNotify, m_rcNotify) && !m_rcNotify.IsRectEmpty())
        m_rcNotify.MoveToXY(m_rcNotify.left - m_rcTaskbar.left, m_rcNotify.top - m_rcTaskbar.top);
    m_rcStart.SetRectEmpty();
    if (::GetWindowRect(m_hStart, m_rcStart) && !m_rcStart.IsRectEmpty())
        m_rcStart.MoveToXY(m_rcStart.left - m_rcTaskbar.left, m_rcStart.top - m_rcTaskbar.top);

    //Window size
    m_rect.right = m_rect.left + m_window_width;
    m_rect.bottom = m_rect.top + m_window_height;

    //任何一项与摆放有关的设置发生变化，都要强制重新摆放一次，否则设置看起来不起作用
    //Any change to a placement-related setting forces a re-place, or the setting appears inert
    const PlacementSettings current{
        theApp.m_taskbar_data.reserve_taskbar_space,
        theApp.m_taskbar_data.avoid_overlap_with_widgets,
        theApp.m_taskbar_data.tbar_wnd_on_left,
        theApp.m_taskbar_data.tbar_wnd_snap,
        theApp.m_taskbar_data.taskbar_left_space_win11,
        theApp.m_taskbar_data.window_offset_left,
        theApp.m_taskbar_data.window_offset_top,
        GetDPI() };
    if (!m_settings_captured || current != m_last_settings)
    {
        m_last_settings = current;
        m_settings_captured = true;
        force_adjust = true;
    }

    //竖向任务栏不支持预留空间。竖着放时通知区域是按每行几个的网格排布的，
    //把托盘"拉宽"根本不会在水平方向上腾出任何空间，占位图标一点作用都没有，
    //连续的一段也凑不出一块干净的矩形，反而会把别人的图标圈进去。
    //这种情况下彻底不做预留，回到普通的摆放方式；设置里的开关也会相应置灰并说明原因。
    //Reserving space is not supported on a vertical taskbar. Docked vertically, the tray is laid
    //out as a grid several icons wide, so widening it frees no horizontal space at all and the
    //placeholders accomplish nothing; a contiguous run also cannot form a clean rectangle there
    //and would enclose other icons. Skip the reservation entirely and fall back to ordinary
    //placement; the settings checkbox is greyed out and says why.
    if (UseCompactOuterMargins())
    {
        //重新打开了，下次关闭时要再清理一遍 / re-armed: sweep again next time it is switched off
        m_purged_for_off = false;
        //Start the timer that tracks the reserved region
        if (!m_tray_timer_started && GetSafeHwnd() != nullptr)
        {
            SetTimer(SPACER_ADJUST_TIMER, SPACER_ADJUST_INTERVAL, nullptr);
            m_tray_timer_started = true;
        }
        //Maintenance must run even when a missing slot has made the block unusable.
        if (UpdateTrayReserve())
            return;
        if (IsWindowVisible())
            ShowWindow(SW_HIDE);
        return;
    }
    else
    {
        //用户主动关掉开关时，连注册表里的占位条目一起清掉；
        //只是因为任务栏竖着放而暂时用不了的话，那些条目要留着，摆回上下时才能立刻恢复。
        //Purge the registry entries only when the user actually switched the option off. If it is
        //merely unavailable because the taskbar is vertical, keep them so it recovers instantly
        //when the bar goes back to top or bottom.
        const bool user_turned_off = !theApp.m_taskbar_data.reserve_taskbar_space;
        if (m_tray_reserve.IsInited())
            m_tray_reserve.Destroy(user_turned_off);

        //开关是关着的时候，无论本次运行有没有添加过图标，都要把上一次留下的注册表条目清掉。
        //程序启动时开关就已经关着的话，根本不会走添加图标那条路，IsInited()始终为false，
        //上面那句Destroy压根不会执行——上一次运行留下的条目就一直躺在
        //"设置－个性化－任务栏－其它系统托盘图标"里没人管。
        //用一个标志保证只清一次，否则每秒十次地翻整个注册表纯属浪费。
        //While the option is off, clear entries left by a previous run whether or not this run
        //ever added icons. If the app starts with the option already off it never takes the
        //add path, IsInited() stays false, and the Destroy above never runs - so the previous
        //run's entries just sit in Settings > Personalisation > Taskbar > "Other system tray
        //icons" with nothing to remove them. The flag keeps it to one sweep; re-reading the whole
        //registry ten times a second would be pure waste.
        if (user_turned_off && !m_purged_for_off)
        {
            m_purged_for_off = true;
            CTaskbarTrayReserve::PurgeOurKeys();
        }
        if (m_tray_timer_started)
        {
            KillTimer(SPACER_ADJUST_TIMER);
            m_tray_timer_started = false;
        }
        m_last_reserved_rect.SetRectEmpty();
    }

    //Make sure the window is visible
    if (!IsWindowVisible())
        ShowWindow(SW_SHOWNA);

    if (force_adjust || m_rcNotify.left != m_last_notify_left || m_rcNotify.Width() != m_last_notify_width || m_rcStart.left != m_last_start_pos)   //如果任务栏关键锚点改变了，重新设置任务栏窗口的位置
    {
        m_last_notify_left = m_rcNotify.left;
        m_last_notify_width = m_rcNotify.Width();
        m_last_start_pos = m_rcStart.left;
        //任务窗口显示在右侧时，或者Windows11下任务栏左对齐时
        //（Windows11下，如果任务栏设置为左对齐，即使在“任务栏窗口设置”中设置了任务窗口显示在左边，窗口仍然显示在右边）
        if (!theApp.m_taskbar_data.tbar_wnd_on_left || !CWindowsSettingHelper::IsTaskbarCenterAlign())
        {
            ////靠近任务栏图标的情况
            //if (theApp.m_taskbar_data.tbar_wnd_snap && IsTaskbarCloseToIconEnable(theApp.m_taskbar_data.tbar_wnd_on_left))
            //{
            //    m_rect.MoveToX(m_rcMin.right + 2);
            //}
            ////靠近通知区的情况
            //else
            //{
            //通知区窗口的水平位置
            int notify_x_pos = m_rcNotify.left;
            //没有获取到通知区位置的情况
            if (m_rcNotify.IsRectEmpty())
            {
                //副屏没有通知区和任务栏图标时，只需要避开系统时间区域。
                const int right_space = m_is_secondary_display
                    ? SECONDARY_TASKBAR_RIGHT_SPACE_WIN11
                    : theApp.m_taskbar_data.taskbar_right_space_win11;
                notify_x_pos = m_rcTaskbar.Width() - DPI(right_space);
            }
            //如果显示了小组件，并且任务栏靠左显示，则留出小组件的位置
            if (theApp.m_taskbar_data.avoid_overlap_with_widgets && CWindowsSettingHelper::IsTaskbarWidgetsBtnShown() && !CWindowsSettingHelper::IsTaskbarCenterAlign())
                m_rect.MoveToX(notify_x_pos - m_rect.Width() + 2 - DPI(theApp.m_taskbar_data.taskbar_left_space_win11));
            else
                m_rect.MoveToX(notify_x_pos - m_rect.Width() + 2);
            //}
        }
        //任务栏窗口显示在左侧时
        else
        {
            //靠近“开始”按钮
            if (theApp.m_taskbar_data.tbar_wnd_snap
                && !m_is_secondary_display
                && !m_rcStart.IsRectEmpty())
            {
                m_rect.MoveToX(m_rcStart.left - m_rect.Width() - 2);
            }
            //靠近最左侧
            else
            {
                if (CWindowsSettingHelper::IsTaskbarWidgetsBtnShown())
                    m_rect.MoveToX(2 + DPI(theApp.m_taskbar_data.taskbar_left_space_win11));
                else
                    m_rect.MoveToX(2);
            }
        }
        //水平偏移
        m_rect.MoveToX(m_rect.left + DPI(theApp.m_taskbar_data.window_offset_left));
        ////确保水平方向不超出屏幕边界
        //if (m_rect.left < 0)
        //    m_rect.MoveToX(0);
        //if (m_rcTaskbar.Width() > m_rect.Width() && m_rect.right > m_rcTaskbar.Width())
        //    m_rect.MoveToX(m_rcTaskbar.Width() - m_rect.Width());

        //设置任务栏窗口的垂直位置
        //注：这里加上(m_rcTaskbar.Height() - rcStart.Height())用于修正Windows11 build 22621版本后触屏设备任务栏窗口位置不正确的问题。
        //在这种情况下m_rcTaskbar的高度要大于m_rcBar的高度，正常情况下，它们的高度相同
        //但是当任务栏上没有任何图标时，m_rcBar的高度会变为0，因此使用rcStart代替
        m_rect.MoveToY((m_rcStart.Height() - m_rect.Height()) / 2 + (m_rcTaskbar.Height() - m_rcStart.Height()) + DPI(theApp.m_taskbar_data.window_offset_top));
        ////确保垂直方向不超出屏幕边界
        //if (m_rect.top < 0)
        //    m_rect.MoveToY(0);
        //if (m_rcTaskbar.Height() > m_rect.Height() && m_rect.bottom > m_rcTaskbar.Height())
        //    m_rect.MoveToY(m_rcTaskbar.Height() - m_rect.Height());

        MoveWindow(m_rect);
    }
}

bool CWin11TaskbarDlg::UpdateTrayReserve()
{
    //副显示器没有自己的通知区域，无法在上面预留空间
    //Secondary displays have no tray of their own, so nothing can be reserved there
    if (m_is_secondary_display)
    {
        if (m_tray_reserve.IsInited())
            m_tray_reserve.Destroy();
        return false;
    }

    m_tray_reserve.SetNotifyWindow(GetSafeHwnd());
    //把当前显示器的DPI交给它。接扩展坞、拔扩展坞、把窗口挪到缩放比例不同的屏上时，
    //托盘图标的间距会跟着变，估计值必须重新量过。
    //Hand it this monitor's DPI. Docking, undocking or moving to a differently scaled screen
    //changes the tray icon pitch, and the estimate has to be re-measured.
    m_tray_reserve.SetDpi(GetDPI());
    //仅按窗口实际需要的宽度预留，不添加外围空白。
    //这里用的是实际算出来的窗口宽度，因此无论配置了多少个显示项目都能自动适应。
    //Reserve the readout width; whole-slot rounding provides any remaining space.
    m_tray_reserve.SetReservedWidth(m_window_width);
    if (m_tray_reserve.IsObstructed())
        return false;

    CRect rc_reserved{ 0, 0, 0, 0 };    //CRect's default constructor does not zero it
    const bool have_rect = m_tray_reserve.GetReservedRect(rc_reserved);
    if (!have_rect)
        return false;                   //placeholders not in the tray yet
    if (rc_reserved.Width() < m_window_width)
        return false;                   //region still smaller than the window; wait for it

    if (!IsWindowVisible())
        ShowWindow(SW_SHOWNA);

    //Left-align the readout so the tray overflow button is adjacent to its left edge.
    CRect rc_target{ rc_reserved };
    rc_target.OffsetRect(-m_rcTaskbar.left, -m_rcTaskbar.top);
    const int x = rc_target.left
        + DPI(theApp.m_taskbar_data.window_offset_left);
    const int y = rc_target.top + (rc_target.Height() - m_window_height) / 2
        + DPI(theApp.m_taskbar_data.window_offset_top);

    m_rect.MoveToXY(x, y);
    CRect current_rect;
    GetWindowRect(current_rect);
    current_rect.OffsetRect(-m_rcTaskbar.left, -m_rcTaskbar.top);
    if (rc_reserved != m_last_reserved_rect || current_rect != m_rect)
    {
        m_last_reserved_rect = rc_reserved;
        MoveWindow(m_rect);
    }
    return true;
}

bool CWin11TaskbarDlg::UseCompactOuterMargins() const
{
    //Count only the item rects and inter-item gaps when reserving tray space.
    return theApp.m_taskbar_data.reserve_taskbar_space && m_taskbar_on_top_or_bottom
        && !m_is_secondary_display;
}

void CWin11TaskbarDlg::OnCancel()
{
    //Drop the placeholder icons before the window goes away
    if (m_tray_timer_started && GetSafeHwnd() != nullptr)
    {
        KillTimer(SPACER_ADJUST_TIMER);
        m_tray_timer_started = false;
    }
    m_tray_reserve.Destroy();
    CTaskBarDlg::OnCancel();
}

BEGIN_MESSAGE_MAP(CWin11TaskbarDlg, CTaskBarDlg)
    ON_WM_TIMER()
    ON_MESSAGE(WM_SPACER_LAYOUT_CHANGED, &CWin11TaskbarDlg::OnSpacerLayoutChanged)
END_MESSAGE_MAP()

LRESULT CWin11TaskbarDlg::OnSpacerLayoutChanged(WPARAM wParam, LPARAM lParam)
{
    //The reserved region moved - follow it immediately instead of waiting for the timer
    if (theApp.m_taskbar_data.reserve_taskbar_space && !m_menu_popuped)
        AdjustWindowPos();
    return 0;
}

void CWin11TaskbarDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == SPACER_ADJUST_TIMER)
    {
        //Backstop for the notification above. Skipped while a context menu is up so the
        //window does not move out from under it.
        if (theApp.m_taskbar_data.reserve_taskbar_space && !m_menu_popuped)
            AdjustWindowPos();
        return;
    }
    CTaskBarDlg::OnTimer(nIDEvent);
}

void CWin11TaskbarDlg::InitTaskbarWnd()
{
    m_hNotify = ::FindWindowEx(m_hTaskbar, 0, L"TrayNotifyWnd", NULL);
    m_hStart = ::FindWindowEx(m_hTaskbar, nullptr, L"Start", NULL);
    m_rcNotify.SetRectEmpty();
    ::GetWindowRect(m_hNotify, m_rcNotify);
}

void CWin11TaskbarDlg::ResetTaskbarPos()
{
}

HWND CWin11TaskbarDlg::GetParentHwnd()
{
    return m_hTaskbar;
}

bool CWin11TaskbarDlg::IsTaskbarVertical()
{
    CRect rc_taskbar;
    HWND taskbar = ::FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar == nullptr || !::GetWindowRect(taskbar, rc_taskbar) || rc_taskbar.IsRectEmpty())
        return false;
    return rc_taskbar.Height() > rc_taskbar.Width();
}

void CWin11TaskbarDlg::CheckTaskbarOnTopOrBottom()
{
    //Do not assume the taskbar is horizontal: third-party tools (and newer Windows 11
    //builds) can dock it to the left or right edge, and every horizontal calculation then
    //puts the window off-screen, which looks like it has vanished. Judge by its shape.
    CRect rc_taskbar;
    if (m_hTaskbar != nullptr && ::GetWindowRect(m_hTaskbar, rc_taskbar) && !rc_taskbar.IsRectEmpty())
        m_taskbar_on_top_or_bottom = (rc_taskbar.Width() >= rc_taskbar.Height());
    else
        m_taskbar_on_top_or_bottom = true;
}
