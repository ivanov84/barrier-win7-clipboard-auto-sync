// barriertray -- always-on native Windows tray helper for the Barrier node.
//
// The Barrier GUI (a Qt app) owns the "official" tray icon, but that icon
// only exists while the GUI process is alive and its "Show Log" only ever
// carries a small buffered slice of the node output -- so the log is "little
// and stale".  This helper is a tiny, dependency-free Win32 tray app that:
//   * always shows a persistent tray icon with a live status tooltip,
//   * offers one click "Copy full log to clipboard" -- the complete current
//     node log file (the node always writes a rolling log to
//     %APPDATA%\Barrier\logs), so it can be pasted/sent for diagnostics,
//   * "Show log" opens the full log, "Open config" opens barrier.conf,
//   * "Restart service" restarts the Barrier service (prompts UAC).
// It is started at user log-on via the Startup folder, so the icon is
// present in the interactive session regardless of whether the GUI is open.
//
// Build (no Qt, no third-party deps):
//   cl /EHsc /O2 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /MT /W3 barriertray.cpp
//   link /SUBSYSTEM:WINDOWS user32.lib shell32.lib

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string>

static const wchar_t kWndClass[]    = L"BarrierTrayHelper";
static const wchar_t kToolTipBase[] = L"Barrier";
static const UINT    kTrayId  = 1;
static const UINT    kMsgTray = WM_APP + 1;
static const UINT    kTimerId = 1;

// menu command ids
enum {
    ID_COPY_LOG   = 1001,
    ID_SHOW_LOG   = 1002,
    ID_OPEN_CONF  = 1003,
    ID_RESTART_SV = 1004,
    ID_QUIT       = 1005,
};

static HWND         g_hwnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static std::string  g_lastTip;

static void nodeLogsDir(std::string& out)
{
    out.clear();
    char appData[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("APPDATA", appData, MAX_PATH) > 0) {
        out = std::string(appData) + "\\Barrier\\logs";
    }
}

// path of the largest *.log under the logs dir (barrierc.log / barriers.log)
static bool latestLogFile(std::string& path)
{
    std::string dir;
    nodeLogsDir(dir);
    if (dir.empty()) {
        return false;
    }
    WIN32_FIND_DATAA f;
    std::string pat = dir + "\\*.log";
    HANDLE h = FindFirstFileA(pat.c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER best = {0};
    std::string bestPath;
    do {
        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        LARGE_INTEGER sz;
        sz.HighPart = f.nFileSizeHigh;
        sz.LowPart  = f.nFileSizeLow;
        if (sz.QuadPart > best.QuadPart) {
            best     = sz;
            bestPath = dir + "\\" + f.cFileName;
        }
    } while (FindNextFileA(h, &f));
    FindClose(h);
    path = bestPath;
    return !bestPath.empty();
}

static bool processExists(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W e;
    e.dwSize = sizeof(e);
    bool found = false;
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, exeName) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return found;
}

// copy the full contents of the log file to the clipboard as UTF-16 text
static void copyFullLog()
{
    std::string path;
    if (!latestLogFile(path)) {
        MessageBoxW(g_hwnd, L"Barrier log file not found under\n%APPDATA%\\Barrier\\logs\n\n"
                    L"(Is the node running? Check the status tooltip.)",
                    kToolTipBase, MB_OK | MB_ICONWARNING);
        return;
    }
    FILE* fp = NULL;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || fp == NULL) {
        MessageBoxW(g_hwnd, L"Could not open the log file.",
                    kToolTipBase, MB_OK | MB_ICONERROR);
        return;
    }
    std::string data;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        data.append(buf, n);
    }
    fclose(fp);

    if (!OpenClipboard(g_hwnd)) {
        MessageBoxW(g_hwnd, L"Could not open the clipboard.\n"
                    L"Another program holds it -- try again.",
                    kToolTipBase, MB_OK | MB_ICONERROR);
        return;
    }
    EmptyClipboard();
    int wchars = (int)data.size() + 1; // 1 byte -> >= 1 wchar
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wchars * sizeof(wchar_t));
    if (hmem != NULL) {
        wchar_t* p = (wchar_t*)GlobalLock(hmem);
        int cch = MultiByteToWideChar(CP_UTF8, 0, data.c_str(),
                                      (int)data.size(), p, wchars);
        if (cch <= 0) {
            cch = 0;
        }
        p[cch] = 0;
        GlobalUnlock(hmem);
        SetClipboardData(CF_UNICODETEXT, hmem);
        GlobalFree(hmem);
    }
    CloseClipboard();

    // confirm so the user knows the log is in the clipboard and can
    // paste it to the other machine (or into a message)
    wchar_t tip[256];
    wsprintfW(tip, L"Full log copied to clipboard (%u chars).\n"
              L"File: %hs", (unsigned)wchars, path.c_str());
    MessageBoxW(g_hwnd, tip, kToolTipBase, MB_OK | MB_ICONINFORMATION);
}

static void openFile(const std::string& path)
{
    if (path.empty()) {
        return;
    }
    wchar_t w[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, w, MAX_PATH);
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
}

static void showLog()
{
    std::string path;
    if (latestLogFile(path)) {
        openFile(path);
    } else {
        MessageBoxW(g_hwnd, L"No log file found yet.\n"
                    L"It appears under %APPDATA%\\Barrier\\logs once the node runs.",
                    kToolTipBase, MB_OK | MB_ICONINFORMATION);
    }
}

static void openConfig()
{
    char localAppData[MAX_PATH] = {0};
    std::string cfg;
    if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        cfg = std::string(localAppData) + "\\Barrier\\barrier.conf";
    }
    if (cfg.empty() || (GetFileAttributesA(cfg.c_str()) == INVALID_FILE_ATTRIBUTES)) {
        cfg = "C:\\Program Files\\Barrier\\barrier.conf";
    }
    if (GetFileAttributesA(cfg.c_str()) != INVALID_FILE_ATTRIBUTES) {
        openFile(cfg);
    } else {
        MessageBoxW(g_hwnd, L"Could not find barrier.conf.",
                    kToolTipBase, MB_OK | MB_ICONINFORMATION);
    }
}

static void restartService()
{
    // "runas" will prompt for UAC when not elevated
    HINSTANCE res = ShellExecuteW(NULL, L"runas", L"cmd.exe",
                                  L"/c net stop Barrier >nul 2>&1 & net start Barrier >nul 2>&1",
                                  NULL, SW_HIDE);
    if ((INT_PTR)res <= (INT_PTR)HINSTANCE_ERROR) {
        MessageBoxW(g_hwnd, L"Could not restart the Barrier service\n"
                    L"(user cancelled or not permitted).",
                    kToolTipBase, MB_OK | MB_ICONWARNING);
    }
}

// short status string for the tooltip, rebuilt on a timer
static void updateTooltip()
{
    bool node = processExists(L"barrierc.exe") || processExists(L"barriers.exe");
    bool svc  = processExists(L"barrierd.exe");

    std::wstring tip;
    tip += kToolTipBase;
    tip += L"  |  node: ";
    tip += node ? L"running" : L"NOT RUNNING";
    tip += L"  |  service: ";
    tip += svc ? L"running" : L"stopped";

    int w = WideCharToMultiByte(CP_ACP, 0, tip.c_str(), -1, NULL, 0, NULL, NULL);
    std::string narrow;
    if (w > 0) {
        narrow.resize((size_t)w);
        WideCharToMultiByte(CP_ACP, 0, tip.c_str(), -1, &narrow[0], w, NULL, NULL);
    }
    if (narrow != g_lastTip) {
        g_lastTip = narrow;
        wcsncpy(g_nid.szTip, tip.c_str(), 127);
        g_nid.szTip[127] = 0;
        Shell_NotifyIcon(NIM_MODIFY, &g_nid);
    }
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case kMsgTray:
        switch (l) {
        case WM_RBUTTONUP:
        case WM_LBUTTONUP: {
            POINT p;
            GetCursorPos(&p);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_COPY_LOG,   L"Copy full log to clipboard");
            AppendMenuW(menu, MF_STRING, ID_SHOW_LOG,   L"Show log");
            AppendMenuW(menu, MF_STRING, ID_OPEN_CONF,  L"Open configuration");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_RESTART_SV, L"Restart Barrier service");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit");
            SetForegroundWindow(h);
            int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                     p.x, p.y, 0, h, NULL);
            DestroyMenu(menu);
            switch (cmd) {
            case ID_COPY_LOG:   copyFullLog();    break;
            case ID_SHOW_LOG:   showLog();        break;
            case ID_OPEN_CONF:  openConfig();     break;
            case ID_RESTART_SV: restartService(); break;
            case ID_QUIT:
                Shell_NotifyIcon(NIM_DELETE, &g_nid);
                PostQuitMessage(0);
                break;
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            showLog();
            return 0;
        }
        return 0;
    case WM_TIMER:
        if (w == kTimerId) {
            updateTooltip();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    // message-only window (no visible frame): style 0, zero size
    g_hwnd = CreateWindowExW(0, kWndClass, L"BarrierTrayHelper", 0,
                             0, 0, 0, 0,
                             (HWND)HWND_MESSAGE, NULL, hInst, NULL);
    if (g_hwnd == NULL) {
        return 1;
    }

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = kTrayId;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kMsgTray;
    g_nid.hIcon            = LoadIconW(hInst, MAKEINTRESOURCEW(1));

    // retry adding: the notification area is sometimes not ready right
    // after log-on -- a failed one-shot add is exactly why tray icons are
    // "not always visible" on Windows 10/11.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (Shell_NotifyIcon(NIM_ADD, &g_nid)) {
            break;
        }
        Sleep(1000);
    }

    updateTooltip();
    SetTimer(g_hwnd, kTimerId, 5000, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
