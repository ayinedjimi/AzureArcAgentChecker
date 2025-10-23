// AzureArcAgentChecker.cpp - Verificateur d'agent Azure Arc
// (c) 2025 Ayi NEDJIMI Consultants - Tous droits reserves
// Verifie etat agent Azure Arc, expiration tokens, extensions installees

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winevt.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "advapi32.lib")

// ======================== RAII AutoHandle ========================
class AutoHandle {
    HANDLE h;
public:
    explicit AutoHandle(HANDLE handle = INVALID_HANDLE_VALUE) : h(handle) {}
    ~AutoHandle() { if (h != INVALID_HANDLE_VALUE && h != NULL) CloseHandle(h); }
    operator HANDLE() const { return h; }
    HANDLE* operator&() { return &h; }
    AutoHandle(const AutoHandle&) = delete;
    AutoHandle& operator=(const AutoHandle&) = delete;
};

// ======================== Globals ========================
HWND g_hMainWnd = NULL;
HWND g_hListView = NULL;
HWND g_hStatusBar = NULL;
HWND g_hBtnCheckAgent = NULL;
HWND g_hBtnListExtensions = NULL;
HWND g_hBtnExport = NULL;
HWND g_hProgressBar = NULL;

std::mutex g_logMutex;
std::wstring g_logFilePath;
bool g_scanning = false;

enum class StatusLevel { OK, WARNING, ERROR_LEVEL };

struct ArcComponentInfo {
    std::wstring component;
    std::wstring status;
    std::wstring version;
    std::wstring expiration;
    std::wstring details;
    std::wstring alerts;
    StatusLevel level;
};

std::vector<ArcComponentInfo> g_components;

// ======================== Logging ========================
void InitLog() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    g_logFilePath = std::wstring(tempPath) + L"WinTools_AzureArcAgentChecker_log.txt";

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wofstream log(g_logFilePath, std::ios::app);
    log << L"\n========== AzureArcAgentChecker - " << std::chrono::system_clock::now().time_since_epoch().count() << L" ==========\n";
}

void Log(const std::wstring& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wofstream log(g_logFilePath, std::ios::app);
    log << msg << L"\n";
}

// ======================== Utilities ========================
std::wstring GetCurrentTimeStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

void SetStatus(const std::wstring& msg) {
    if (g_hStatusBar) {
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
    }
    Log(msg);
}

void ShowProgress(bool show) {
    if (g_hProgressBar) {
        ShowWindow(g_hProgressBar, show ? SW_SHOW : SW_HIDE);
        if (show) {
            SendMessageW(g_hProgressBar, PBM_SETMARQUEE, TRUE, 0);
        }
    }
}

void EnableButtons(bool enable) {
    EnableWindow(g_hBtnCheckAgent, enable);
    EnableWindow(g_hBtnListExtensions, enable);
    EnableWindow(g_hBtnExport, enable);
}

// ======================== Simple JSON Parser ========================
std::wstring ExtractJsonValue(const std::wstring& json, const std::wstring& key) {
    // Simple extraction: find "key":"value" or "key":value
    std::wstring searchKey = L"\"" + key + L"\":";
    size_t pos = json.find(searchKey);
    if (pos == std::wstring::npos) return L"";

    pos += searchKey.length();
    while (pos < json.length() && (json[pos] == L' ' || json[pos] == L'\t')) pos++;

    if (pos >= json.length()) return L"";

    bool isString = (json[pos] == L'\"');
    if (isString) pos++; // Skip opening quote

    std::wstring value;
    while (pos < json.length()) {
        if (isString && json[pos] == L'\"') break;
        if (!isString && (json[pos] == L',' || json[pos] == L'}' || json[pos] == L'\n')) break;
        value += json[pos++];
    }

    return value;
}

std::wstring ReadFileToString(const std::wstring& path) {
    std::wifstream file(path, std::ios::binary);
    if (!file) return L"";

    std::wstringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ======================== Process Detection ========================
bool IsProcessRunning(const std::wstring& processName, DWORD* pidOut = nullptr) {
    AutoHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(snap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, processName.c_str()) == 0) {
                if (pidOut) *pidOut = pe32.th32ProcessID;
                return true;
            }
        } while (Process32NextW(snap, &pe32));
    }

    return false;
}

std::wstring GetProcessPath(DWORD pid) {
    AutoHandle hProc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (hProc == NULL) return L"";

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        return path;
    }
    return L"";
}

// ======================== Azure Arc Configuration ========================
ArcComponentInfo ReadArcConfig() {
    ArcComponentInfo info;
    info.component = L"Configuration Agent";

    std::wstring configPath = L"C:\\ProgramData\\AzureConnectedMachineAgent\\Config\\agentconfig.json";
    std::wstring jsonContent = ReadFileToString(configPath);

    if (jsonContent.empty()) {
        info.status = L"Non trouve";
        info.level = StatusLevel::ERROR_LEVEL;
        info.alerts = L"Fichier config manquant";
        return info;
    }

    info.status = L"Configuration trouvee";
    info.level = StatusLevel::OK;

    // Extract key values
    std::wstring resourceId = ExtractJsonValue(jsonContent, L"resourceId");
    std::wstring location = ExtractJsonValue(jsonContent, L"location");
    std::wstring tenantId = ExtractJsonValue(jsonContent, L"tenantId");

    if (!resourceId.empty()) {
        info.details = L"Resource: " + resourceId;
        if (!location.empty()) info.details += L" | Region: " + location;
        if (!tenantId.empty()) info.details += L" | Tenant: " + tenantId.substr(0, 8) + L"...";
    }

    // Check token expiration (metadata service)
    std::wstring metadataPath = L"C:\\ProgramData\\AzureConnectedMachineAgent\\Tokens\\metadata.json";
    std::wstring metadataContent = ReadFileToString(metadataPath);

    if (!metadataContent.empty()) {
        std::wstring expiresOn = ExtractJsonValue(metadataContent, L"expiresOn");
        if (!expiresOn.empty()) {
            info.expiration = expiresOn;
            // Simple check: if expiration looks like a number (Unix timestamp)
            if (!expiresOn.empty() && iswdigit(expiresOn[0])) {
                try {
                    long long expireTime = std::stoll(expiresOn);
                    long long currentTime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    if (expireTime < currentTime) {
                        info.alerts = L"TOKEN EXPIRE!";
                        info.level = StatusLevel::ERROR_LEVEL;
                    } else if (expireTime - currentTime < 86400) { // < 24h
                        info.alerts = L"Token expire bientot";
                        info.level = StatusLevel::WARNING;
                    }
                } catch (...) {}
            }
        }
    }

    return info;
}

// ======================== Process Checks ========================
void CheckArcProcesses() {
    // Check himds.exe
    DWORD pid = 0;
    if (IsProcessRunning(L"himds.exe", &pid)) {
        ArcComponentInfo info;
        info.component = L"Service HIMDS";
        info.status = L"En cours d'execution";
        info.level = StatusLevel::OK;
        info.details = L"PID: " + std::to_wstring(pid);

        std::wstring path = GetProcessPath(pid);
        if (!path.empty()) {
            info.version = path;
        }

        g_components.push_back(info);
    } else {
        ArcComponentInfo info;
        info.component = L"Service HIMDS";
        info.status = L"Non actif";
        info.level = StatusLevel::ERROR_LEVEL;
        info.alerts = L"Processus non demarre";
        g_components.push_back(info);
    }

    // Check azcmagent.exe
    pid = 0;
    if (IsProcessRunning(L"azcmagent.exe", &pid)) {
        ArcComponentInfo info;
        info.component = L"Agent Azure Arc";
        info.status = L"En cours d'execution";
        info.level = StatusLevel::OK;
        info.details = L"PID: " + std::to_wstring(pid);

        std::wstring path = GetProcessPath(pid);
        if (!path.empty()) {
            info.version = path;
        }

        g_components.push_back(info);
    } else {
        ArcComponentInfo info;
        info.component = L"Agent Azure Arc";
        info.status = L"Non actif";
        info.level = StatusLevel::WARNING;
        info.alerts = L"Processus non demarre";
        g_components.push_back(info);
    }
}

// ======================== Extensions Enumeration ========================
void EnumerateExtensions() {
    std::wstring pluginsPath = L"C:\\Packages\\Plugins";

    WIN32_FIND_DATAW findData;
    AutoHandle hFind(FindFirstFileW((pluginsPath + L"\\Microsoft.Azure.*").c_str(), &findData));

    if (hFind == INVALID_HANDLE_VALUE) {
        ArcComponentInfo info;
        info.component = L"Extensions Azure";
        info.status = L"Aucune trouvee";
        info.level = StatusLevel::WARNING;
        info.alerts = L"Dossier Plugins vide ou absent";
        g_components.push_back(info);
        return;
    }

    int extCount = 0;
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
                ArcComponentInfo info;
                info.component = L"Extension";
                info.status = L"Installee";
                info.level = StatusLevel::OK;
                info.details = findData.cFileName;

                // Try to read version from status file
                std::wstring statusPath = pluginsPath + L"\\" + findData.cFileName + L"\\status\\*.status";
                WIN32_FIND_DATAW statusFind;
                AutoHandle hStatusFind(FindFirstFileW(statusPath.c_str(), &statusFind));
                if (hStatusFind != INVALID_HANDLE_VALUE) {
                    info.version = L"Status present";
                }

                g_components.push_back(info);
                extCount++;
            }
        }
    } while (FindNextFileW(hFind, &findData));

    if (extCount == 0) {
        ArcComponentInfo info;
        info.component = L"Extensions Azure";
        info.status = L"Aucune trouvee";
        info.level = StatusLevel::WARNING;
        g_components.push_back(info);
    }
}

// ======================== Event Log Query ========================
void QueryArcEventLog() {
    const wchar_t* channelPath = L"Microsoft-AzureArc-Agent/Operational";
    const wchar_t* query = L"*[System[Provider[@Name='Microsoft-AzureArc-Agent'] and (Level=1 or Level=2 or Level=3)]]";

    EVT_HANDLE hResults = EvtQuery(NULL, channelPath, query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!hResults) {
        Log(L"Impossible d'interroger Event Log Azure Arc (peut ne pas exister)");
        return;
    }

    EVT_HANDLE events[10];
    DWORD returned = 0;

    if (EvtNext(hResults, 10, events, INFINITE, 0, &returned)) {
        for (DWORD i = 0; i < returned && i < 3; i++) { // Limit to 3 recent events
            DWORD bufferUsed = 0;
            DWORD propertyCount = 0;

            if (!EvtRender(NULL, events[i], EvtRenderEventXml, 0, NULL, &bufferUsed, &propertyCount)) {
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    std::vector<wchar_t> buffer(bufferUsed / sizeof(wchar_t) + 1);
                    if (EvtRender(NULL, events[i], EvtRenderEventXml, bufferUsed, buffer.data(), &bufferUsed, &propertyCount)) {
                        std::wstring eventXml(buffer.data());

                        // Extract basic info (simplified)
                        size_t levelPos = eventXml.find(L"Level>");
                        if (levelPos != std::wstring::npos) {
                            ArcComponentInfo info;
                            info.component = L"Event Log";
                            info.status = L"Event recent";
                            info.level = StatusLevel::WARNING;

                            if (eventXml.find(L"Level>1") != std::wstring::npos) {
                                info.alerts = L"Erreur critique detectee";
                                info.level = StatusLevel::ERROR_LEVEL;
                            } else if (eventXml.find(L"Level>2") != std::wstring::npos) {
                                info.alerts = L"Erreur detectee";
                            } else {
                                info.alerts = L"Avertissement detecte";
                            }

                            info.details = L"Event recents dans le journal";
                            g_components.push_back(info);
                            break; // Only add one summary entry
                        }
                    }
                }
            }

            EvtClose(events[i]);
        }
    }

    EvtClose(hResults);
}

// ======================== ListView Management ========================
void InitListView() {
    ListView_DeleteAllItems(g_hListView);

    // Remove old columns
    while (ListView_DeleteColumn(g_hListView, 0));

    // Add columns
    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;

    lvc.cx = 150; lvc.pszText = const_cast<LPWSTR>(L"Composant");
    ListView_InsertColumn(g_hListView, 0, &lvc);

    lvc.cx = 120; lvc.pszText = const_cast<LPWSTR>(L"Etat");
    ListView_InsertColumn(g_hListView, 1, &lvc);

    lvc.cx = 200; lvc.pszText = const_cast<LPWSTR>(L"Version/Chemin");
    ListView_InsertColumn(g_hListView, 2, &lvc);

    lvc.cx = 150; lvc.pszText = const_cast<LPWSTR>(L"Expiration Token");
    ListView_InsertColumn(g_hListView, 3, &lvc);

    lvc.cx = 250; lvc.pszText = const_cast<LPWSTR>(L"Details");
    ListView_InsertColumn(g_hListView, 4, &lvc);

    lvc.cx = 200; lvc.pszText = const_cast<LPWSTR>(L"Alertes");
    ListView_InsertColumn(g_hListView, 5, &lvc);
}

void UpdateListView() {
    ListView_DeleteAllItems(g_hListView);

    int idx = 0;
    for (const auto& comp : g_components) {
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT;
        lvi.iItem = idx;

        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(comp.component.c_str());
        ListView_InsertItem(g_hListView, &lvi);

        ListView_SetItemText(g_hListView, idx, 1, const_cast<LPWSTR>(comp.status.c_str()));
        ListView_SetItemText(g_hListView, idx, 2, const_cast<LPWSTR>(comp.version.c_str()));
        ListView_SetItemText(g_hListView, idx, 3, const_cast<LPWSTR>(comp.expiration.c_str()));
        ListView_SetItemText(g_hListView, idx, 4, const_cast<LPWSTR>(comp.details.c_str()));
        ListView_SetItemText(g_hListView, idx, 5, const_cast<LPWSTR>(comp.alerts.c_str()));

        idx++;
    }
}

// ======================== Scanning Operations ========================
void PerformAgentCheck() {
    g_scanning = true;
    EnableButtons(false);
    ShowProgress(true);
    SetStatus(L"Verification de l'agent Azure Arc en cours...");

    g_components.clear();

    // Check processes
    CheckArcProcesses();

    // Read configuration
    ArcComponentInfo config = ReadArcConfig();
    g_components.push_back(config);

    // Query event log
    QueryArcEventLog();

    UpdateListView();

    ShowProgress(false);
    EnableButtons(true);
    SetStatus(L"Verification terminee - " + std::to_wstring(g_components.size()) + L" composants analyses");
    g_scanning = false;
}

void PerformExtensionsScan() {
    g_scanning = true;
    EnableButtons(false);
    ShowProgress(true);
    SetStatus(L"Enumeration des extensions Azure Arc...");

    g_components.clear();
    EnumerateExtensions();

    UpdateListView();

    ShowProgress(false);
    EnableButtons(true);
    SetStatus(L"Enumeration terminee - " + std::to_wstring(g_components.size()) + L" extensions trouvees");
    g_scanning = false;
}

// ======================== Export CSV ========================
void ExportToCSV() {
    wchar_t filename[MAX_PATH] = L"AzureArcAgent_Report.csv";

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFilter = L"Fichiers CSV (*.csv)\0*.csv\0Tous les fichiers (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"csv";

    if (!GetSaveFileNameW(&ofn)) return;

    std::wofstream csv(filename, std::ios::binary);
    if (!csv) {
        MessageBoxW(g_hMainWnd, L"Impossible de creer le fichier CSV", L"Erreur", MB_ICONERROR);
        return;
    }

    // UTF-8 BOM
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    csv.write(reinterpret_cast<wchar_t*>(bom), sizeof(bom) / sizeof(wchar_t));

    csv << L"Composant,Etat,Version/Chemin,Expiration Token,Details,Alertes\n";

    for (const auto& comp : g_components) {
        csv << L"\"" << comp.component << L"\",";
        csv << L"\"" << comp.status << L"\",";
        csv << L"\"" << comp.version << L"\",";
        csv << L"\"" << comp.expiration << L"\",";
        csv << L"\"" << comp.details << L"\",";
        csv << L"\"" << comp.alerts << L"\"\n";
    }

    csv.close();

    std::wstring msg = L"Rapport exporte vers:\n" + std::wstring(filename);
    MessageBoxW(g_hMainWnd, msg.c_str(), L"Export reussi", MB_ICONINFORMATION);
    SetStatus(L"Export CSV termine");
}

// ======================== Window Procedure ========================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            InitLog();

            // ListView
            g_hListView = CreateWindowExW(
                0, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                10, 10, 960, 400,
                hwnd, (HMENU)1, GetModuleHandle(NULL), NULL
            );
            ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            InitListView();

            // Buttons
            g_hBtnCheckAgent = CreateWindowExW(
                0, L"BUTTON", L"Verifier Agent",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 420, 150, 30,
                hwnd, (HMENU)2, GetModuleHandle(NULL), NULL
            );

            g_hBtnListExtensions = CreateWindowExW(
                0, L"BUTTON", L"Lister Extensions",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                170, 420, 150, 30,
                hwnd, (HMENU)3, GetModuleHandle(NULL), NULL
            );

            g_hBtnExport = CreateWindowExW(
                0, L"BUTTON", L"Exporter CSV",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                330, 420, 150, 30,
                hwnd, (HMENU)4, GetModuleHandle(NULL), NULL
            );

            // Progress bar
            g_hProgressBar = CreateWindowExW(
                0, PROGRESS_CLASSW, NULL,
                WS_CHILD | PBS_MARQUEE,
                490, 425, 200, 20,
                hwnd, (HMENU)5, GetModuleHandle(NULL), NULL
            );

            // Status bar
            g_hStatusBar = CreateWindowExW(
                0, STATUSCLASSNAMEW, NULL,
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0,
                hwnd, (HMENU)6, GetModuleHandle(NULL), NULL
            );

            SetStatus(L"Pret - Azure Arc Agent Checker (c) Ayi NEDJIMI Consultants");
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case 2: // Check Agent
                    std::thread(PerformAgentCheck).detach();
                    break;

                case 3: // List Extensions
                    std::thread(PerformExtensionsScan).detach();
                    break;

                case 4: // Export
                    ExportToCSV();
                    break;
            }
            break;
        }

        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);

            SetWindowPos(g_hListView, NULL, 10, 10, rc.right - 20, rc.bottom - 100, SWP_NOZORDER);
            SetWindowPos(g_hBtnCheckAgent, NULL, 10, rc.bottom - 80, 150, 30, SWP_NOZORDER);
            SetWindowPos(g_hBtnListExtensions, NULL, 170, rc.bottom - 80, 150, 30, SWP_NOZORDER);
            SetWindowPos(g_hBtnExport, NULL, 330, rc.bottom - 80, 150, 30, SWP_NOZORDER);
            SetWindowPos(g_hProgressBar, NULL, 490, rc.bottom - 75, 200, 20, SWP_NOZORDER);

            SendMessageW(g_hStatusBar, WM_SIZE, 0, 0);
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ======================== Main ========================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AzureArcAgentCheckerClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassExW(&wc);

    // Create window
    g_hMainWnd = CreateWindowExW(
        0,
        L"AzureArcAgentCheckerClass",
        L"Azure Arc Agent Checker - (c) Ayi NEDJIMI Consultants",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 550,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) return 1;

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
