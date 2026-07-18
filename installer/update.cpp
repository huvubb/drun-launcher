#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <io.h>
#include <fcntl.h>
#include <cstdlib>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

std::string g_langCode = "en";
std::wstring g_installPath;
std::wstring g_langIniPath;
std::string g_currentVer = "1.0.0";
std::string g_latestVer;

std::string WtoU8(const wchar_t* w) {
    if (!w||!*w) return "";
    int l=WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL);
    if(l<=1)return "";
    std::string r(l-1,'\0');
    WideCharToMultiByte(CP_UTF8,0,w,-1,&r[0],l,NULL,NULL);
    return r;
}

std::string T(const char* key) {
    WCHAR buf[4096];
    std::wstring sec(g_langCode.begin(), g_langCode.end());
    GetPrivateProfileStringW(sec.c_str(),
        std::wstring(key, key+strlen(key)).c_str(),
        L"", buf, 4096, g_langIniPath.c_str());
    if (buf[0]) return WtoU8(buf);
    GetPrivateProfileStringW(L"en",
        std::wstring(key, key+strlen(key)).c_str(),
        L"", buf, 4096, g_langIniPath.c_str());
    return buf[0] ? WtoU8(buf) : key;
}

// Read config.ini from install dir
bool LoadConfig() {
    WCHAR cfg[MAX_PATH];
    swprintf_s(cfg, L"%s\\config.ini", g_installPath.c_str());
    if (GetFileAttributesW(cfg) == INVALID_FILE_ATTRIBUTES) return false;
    
    WCHAR buf[MAX_PATH];
    GetPrivateProfileStringW(L"install", L"lang", L"en", buf, MAX_PATH, cfg);
    g_langCode = WtoU8(buf);
    GetPrivateProfileStringW(L"install", L"version", L"0", buf, MAX_PATH, cfg);
    g_currentVer = WtoU8(buf);
    return true;
}

// Find install dir from PATH or known locations
std::wstring FindInstallDir() {
    // Check known location
    WCHAR localAppData[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
        std::wstring p = std::wstring(localAppData) + L"\\drun-launcher";
        if (GetFileAttributesW((p + L"\\config.ini").c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;
    }
    
    // Check known drun locations
    const wchar_t* knownDirs[] = {
    };
    // Check exe's own directory
    WCHAR selfPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, selfPath, MAX_PATH)) {
        WCHAR* slash = wcsrchr(selfPath, L'\\');
        if (slash) *slash = 0;
        std::wstring p = std::wstring(selfPath) + L"\\config.ini";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            return selfPath;
    }
    
    // Check PATH
    WCHAR pathBuf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
    if (len > 0) {
        std::wstring ps(pathBuf, len);
        size_t pos = 0;
        while (pos < ps.size()) {
            size_t semi = ps.find(L';', pos);
            if (semi == std::wstring::npos) semi = ps.size();
            std::wstring dir = ps.substr(pos, semi - pos);
            if (GetFileAttributesW((dir + L"\\config.ini").c_str()) != INVALID_FILE_ATTRIBUTES)
                return dir;
            pos = semi + 1;
        }
    }
    return L"";
}

// HTTP GET request using WinHTTP
std::string HttpGet(const wchar_t* server, const wchar_t* path, bool https = true) {
    HINTERNET hSession = WinHttpOpen(L"Drun Updater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    
    HINTERNET hConnect = WinHttpConnect(hSession, server,
        https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
        NULL, NULL, NULL, https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
    
    // Add headers
    const wchar_t* headers = L"User-Agent: drun-updater\r\nAccept: application/json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, (DWORD)wcslen(headers),
        WINHTTP_ADDREQ_FLAG_ADD);
    
    if (!WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return "";
    }
    
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return "";
    }
    
    std::string result;
    DWORD bytesRead;
    char buf[4096];
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        result.append(buf, bytesRead);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// Parse version from GitHub API response
std::string ParseVersion(const std::string& json) {
    size_t pos = json.find("\"tag_name\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    std::string tag = json.substr(pos + 1, end - pos - 1);
    if (tag.size() > 1 && tag[0] == 'v') tag = tag.substr(1);
    return tag;
}

bool IsNewer(const std::string& current, const std::string& latest) {
    auto parse = [](const std::string& v) {
        std::vector<int> parts;
        std::string cur;
        for (char c : v) {
            if (c == '.') { if (!cur.empty()) { parts.push_back(atoi(cur.c_str())); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) parts.push_back(atoi(cur.c_str()));
        return parts;
    };
    auto cp = parse(current), lp = parse(latest);
    for (size_t i = 0; i < (std::max)(cp.size(), lp.size()); i++) {
        int cv = i < cp.size() ? cp[i] : 0;
        int lv = i < lp.size() ? lp[i] : 0;
        if (lv > cv) return true;
        if (lv < cv) return false;
    }
    return false;
}

bool DownloadFile(const wchar_t* url, const wchar_t* destPath) {
    return URLDownloadToFileW(NULL, url, destPath, 0, NULL) == S_OK;
}

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Drun Updater");
    
    printf("\n  Drun Updater\n  ============\n\n");
    
    g_installPath = FindInstallDir();
    if (g_installPath.empty()) {
        printf("  %s\n", T("update_not_found").c_str());
        printf("\n  %s...", T("press_enter").c_str()); getchar();
        return 1;
    }
    
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* ls = wcsrchr(exeDir, L'\\'); if (ls) *ls = L'\0';
    g_langIniPath = std::wstring(exeDir) + L"\\lang.ini";
    if (GetFileAttributesW(g_langIniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        g_langIniPath = g_installPath + L"\\lang.ini";
    if (GetFileAttributesW(g_langIniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("  %s\n", T("update_lang_missing").c_str());
        printf("\n  %s...", T("press_enter").c_str()); getchar();
        return 1;
    }
    
    if (!LoadConfig()) {
        printf("  %s\n", T("update_config_missing").c_str());
        printf("\n  %s...", T("press_enter").c_str()); getchar();
        return 1;
    }
    
    printf("  %s: %s\n", T("install_path_label").c_str(), WtoU8(g_installPath.c_str()).c_str());
    printf("  %s: v%s\n\n", T("update_current").c_str(), g_currentVer.c_str());
    
    printf("  %s...\n", T("update_checking").c_str());
    std::string apiResp = HttpGet(L"api.github.com", L"/repos/huvubb/drun-launcher/releases/latest");
    
    if (apiResp.empty()) {
        printf("  %s\n", T("update_check_failed").c_str());
        printf("\n  %s...", T("press_enter").c_str()); getchar();
        return 1;
    }
    
    g_latestVer = ParseVersion(apiResp);
    if (g_latestVer.empty()) {
        printf("  %s\n", T("update_parse_failed").c_str());
        printf("\n  %s...", T("press_enter").c_str()); getchar();
        return 1;
    }
    
    printf("  %s: v%s\n\n", T("update_latest").c_str(), g_latestVer.c_str());
    
    if (!IsNewer(g_currentVer, g_latestVer)) {
        printf("  %s\n", T("update_uptodate").c_str());
        printf("\n  %s...", T("press_enter").c_str()); getchar();
        return 0;
    }
    
    printf("  %s\n\n", T("update_new_version").c_str());
    
    const wchar_t* files[] = { L"drun.exe", L"drun-plus.exe", L"drun-path.exe", L"lang.ini" };
    bool ok = true;
    
    for (int i = 0; i < 4; i++) {
        WCHAR url[512];
        swprintf_s(url, L"https://github.com/huvubb/drun-launcher/releases/latest/download/%s", files[i]);
        
        WCHAR tmp[MAX_PATH];
        swprintf_s(tmp, L"%s\\%s.new", g_installPath.c_str(), files[i]);
        
        printf("  [%d/4] %s %s...", i + 1, T("update_downloading").c_str(), WtoU8(files[i]).c_str());
        if (DownloadFile(url, tmp)) {
            printf(" %s\n", T("ok").c_str());
        } else {
            printf(" %s\n", T("failed").c_str());
            ok = false;
        }
    }
    
    if (!ok) {
        printf("\n  %s\n", T("update_dl_failed").c_str());
        printf("  %s...", T("press_enter").c_str()); getchar();
        return 1;
    }
    
    printf("\n  %s...\n", T("installing").c_str());
    for (int i = 0; i < 4; i++) {
        WCHAR tmp[MAX_PATH], dest[MAX_PATH];
        swprintf_s(tmp, L"%s\\%s.new", g_installPath.c_str(), files[i]);
        swprintf_s(dest, L"%s\\%s", g_installPath.c_str(), files[i]);
        DeleteFileW(dest);
        MoveFileW(tmp, dest);
        printf("  [%d/4] %s %s\n", i + 1, T("ok").c_str(), WtoU8(files[i]).c_str());
    }
    
    std::string cfg = "[install]\r\npath=" + WtoU8(g_installPath.c_str()) + 
                      "\r\nlang=" + g_langCode + "\r\nversion=" + g_latestVer + "\r\n";
    HANDLE hcfg = CreateFileW((g_installPath + L"\\config.ini").c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hcfg != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(hcfg, cfg.c_str(), (DWORD)cfg.size(), &w, NULL); CloseHandle(hcfg); }
    
    printf("\n  %s v%s!\n", T("update_done").c_str(), g_latestVer.c_str());
    printf("\n  %s...", T("press_enter").c_str()); getchar();
    return 0;
}

