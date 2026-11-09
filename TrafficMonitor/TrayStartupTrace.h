#pragma once
#include "Common.h"

class CTrayStartupTrace
{
public:
    explicit CTrayStartupTrace(const char* name) : m_name(name) { Write("enter"); }
    ~CTrayStartupTrace() { Write("leave"); }
private:
    void Write(const char* phase)
    {
        char text[256]{};
        sprintf_s(text, "%lu %s %s", GetCurrentThreadId(), phase, m_name);
        CCommon::WriteLog(text, _T("E:\\CodexBackups\\TrafficMonitorTraySpace\\20261109\\startup-regression\\trace.log"));
    }
    const char* m_name;
};
