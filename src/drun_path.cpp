#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <io.h>
#include <fcntl.h>
#include <shlobj.h>
#include <vector>
#include <cstdlib>
#include <cstdarg>

const wchar_t* DEFAULT_DIR = L"D:\\desktop\\\u7cfb\u7edf\u5de5\u5177\\npm-launcher";
wchar_t g_launcherDir[MAX_PATH];
wchar_t g_errLogPath[MAX_PATH];

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string r(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &r[0], len, NULL, NULL);
    return r;
}

void InitErrLog() {
    WCHAR tmp[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, tmp) == S_OK) {
        swprintf_s(g_errLogPath, L"%s\\Temp\\drun_error.log", tmp);
        CreateDirectoryW((std::wstring(tmp) + L"\\Temp").c_str(), NULL);
    } else { wcscpy_s(g_errLogPath, L"D:\\drun_error.log"); }
}

void LogError(const wchar_t* fmt, ...) {
    if (g_errLogPath[0] == 0) InitErrLog();
    WCHAR msg[4096]; va_list args; va_start(args, fmt);
    _vsnwprintf_s(msg, 4096, _TRUNCATE, fmt, args); va_end(args);
    SYSTEMTIME st; GetLocalTime(&st);
    WCHAR line[5120];
    swprintf_s(line, L"[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
    HANDLE h = CreateFileW(g_errLogPath, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(WCHAR)), &w, NULL); CloseHandle(h); }
}

void CheckErrorLog() {
    if (g_errLogPath[0] == 0) InitErrLog();
    HANDLE h = CreateFileW(g_errLogPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(h, NULL);
    if (size == 0 || size > 65536) { CloseHandle(h); return; }
    DWORD rf = size > 4096 ? size - 4096 : 0;
    SetFilePointer(h, rf, NULL, FILE_BEGIN);
    std::vector<char> rbuf(size - rf + 4); DWORD rs;
    if (!ReadFile(h, &rbuf[0], (DWORD)rbuf.size() - 4, &rs, NULL) || rs < 40) { CloseHandle(h); return; }
    CloseHandle(h);
    std::wstring lg((WCHAR*)&rbuf[0], rs / sizeof(WCHAR));
    // Only alert on critical errors (FAILED / Recompile)
    bool critical = lg.find(L"] FAILED") != std::wstring::npos
                 || lg.find(L"] Recompile") != std::wstring::npos;
    if (!critical) return;

    printf("\n  *** \u68c0\u6d4b\u5230\u4e25\u91cd\u9519\u8bef ***\n");
    printf("  \u65e5\u5fd7: %s\n\n", WtoU8(g_errLogPath).c_str());
    printf("  [1] \u5ffd\u7565\uff0c\u7ee7\u7eed\u4f7f\u7528\n");
    printf("  [2] \u53d1\u9001\u9519\u8bef\u62a5\u544a\u7ed9\u5f00\u53d1\u8005 (810372789@qq.com)\n");
    printf("  \u8bf7\u9009\u62e9 (1/2): ");

    char ch = (char)getchar(); while (getchar() != '\n');
    if (ch == '2') {
        printf("\n  \u60a8\u7684\u8054\u7cfb\u65b9\u5f0f (\u5fae\u4fe1/\u90ae\u7bb1): ");
        char contact[256]; fgets(contact, 256, stdin);
        std::string sc(contact); while (!sc.empty() && (sc.back()=='\n'||sc.back()=='\r')) sc.pop_back();

        printf("  \u95ee\u9898\u63cf\u8ff0: ");
        char problem[1024]; fgets(problem, 1024, stdin);
        std::string sp(problem); while (!sp.empty() && (sp.back()=='\n'||sp.back()=='\r')) sp.pop_back();

        // Build mailto URL with pre-filled info
        std::wstring body = L"\u8054\u7cfb\u65b9\u5f0f: " + std::wstring(sc.begin(), sc.end())
            + L"\r\n\u95ee\u9898: " + std::wstring(sp.begin(), sp.end())
            + L"\r\n\u65e5\u5fd7\u4f4d\u7f6e: " + g_errLogPath;
        // URL-encode body
        std::wstring encoded;
        for (wchar_t c : body) {
            if (iswalnum(c) || wcschr(L"._-~/", c)) encoded += c;
            else { WCHAR hex[8]; swprintf_s(hex, L"%%%04X", (unsigned short)c); encoded += hex; }
        }
        std::wstring mailto = L"mailto:810372789@qq.com?subject=%5bDrun%20Error%20Report%5d&body=" + encoded;
        ShellExecuteW(NULL, L"open", mailto.c_str(), NULL, NULL, SW_SHOW);

        printf("  \u5df2\u6253\u5f00\u90ae\u4ef6\u5ba2\u6237\u7aef\uff0c\u8bf7\u70b9\u51fb\u53d1\u9001\u3002\n");
    }
    printf("\n");
}

void LoadConfig() {
    wcscpy_s(g_launcherDir, DEFAULT_DIR);
    WCHAR envBuf[MAX_PATH];
    if (GetEnvironmentVariableW(L"DRUN_INSTALL_DIR", envBuf, MAX_PATH) > 0) { wcscpy_s(g_launcherDir, envBuf); return; }
    const wchar_t* known[] = { L"D:\\desktop\\\u7cfb\u7edf\u5de5\u5177\\npm-launcher\\config.ini" };
    wchar_t cfg[MAX_PATH] = {0};
    for (int i = 0; i < 1; i++) if (GetFileAttributesW(known[i]) != INVALID_FILE_ATTRIBUTES) { wcscpy_s(cfg, known[i]); break; }
    if (cfg[0] == 0) {
        WCHAR lad[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, lad) == S_OK) {
            swprintf_s(cfg, L"%s\\drun-launcher\\config.ini", lad);
            if (GetFileAttributesW(cfg) == INVALID_FILE_ATTRIBUTES) cfg[0] = 0;
        }
    }
    if (cfg[0]) {
        WCHAR buf[MAX_PATH];
        GetPrivateProfileStringW(L"install", L"path", L"", buf, MAX_PATH, cfg);
        if (buf[0]) wcscpy_s(g_launcherDir, buf);
    }
}

bool AddToPath(const wchar_t* dir) {
    WCHAR pathBuf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) { pathBuf[0] = L'\0'; len = 0; }
    std::wstring pathStr(pathBuf, len);
    std::wstring dirStr(dir);
    std::wstring dirWithSlash = dirStr;
    if (!dirWithSlash.empty() && dirWithSlash.back() != L'\\') dirWithSlash += L'\\';
    if (pathStr.find(dirWithSlash) != std::wstring::npos || pathStr.find(dirStr) != std::wstring::npos) {
        printf("drun \u5df2\u5728 PATH \u4e2d: %s\n", WtoU8(dir).c_str());
        return false;
    }
    // Backup PATH before modification
    WCHAR backupPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, backupPath) == S_OK) {
        SYSTEMTIME st; GetLocalTime(&st);
        WCHAR ts[64]; swprintf_s(ts, L"_%04d%02d%02d_%02d%02d%02d_path_backup.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        wcscat_s(backupPath, ts);
        HANDLE hb = CreateFileW(backupPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hb != INVALID_HANDLE_VALUE) { DWORD w2; WriteFile(hb, pathBuf, len * sizeof(WCHAR), &w2, NULL); CloseHandle(hb); }
    }
    std::wstring newPath = pathStr;
    if (!newPath.empty() && newPath.back() != L';') newPath += L';';
    newPath += dirStr;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        printf("\u65e0\u6cd5\u6253\u5f00\u6ce8\u518c\u8868\u3002\n");
        LogError(L"Failed to open registry key");
        return false;
    }
    if (RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ, (BYTE*)newPath.c_str(), (DWORD)((newPath.size() + 1) * sizeof(WCHAR))) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        printf("\u5199\u5165\u6ce8\u518c\u8868\u5931\u8d25\u3002\n");
        LogError(L"Failed to write PATH to registry");
        return false;
    }
    RegCloseKey(hKey);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    printf("\u5df2\u5c06 drun \u6dfb\u52a0\u5230\u7528\u6237 PATH: %s\n", WtoU8(dir).c_str());
    return true;
}

bool CreateProfile() {
    WCHAR docs[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs) != S_OK) {
        printf("\u65e0\u6cd5\u83b7\u53d6\u6587\u6863\u8def\u5f84\u3002\n");
        return false;
    }
    std::wstring psDir = std::wstring(docs) + L"\\WindowsPowerShell";
    CreateDirectoryW(psDir.c_str(), NULL);
    std::wstring pf = psDir + L"\\profile.ps1";
    std::string oldContent;
    HANDLE hf = CreateFileW(pf.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hf, NULL);
        if (sz > 0 && sz < 65536) { std::vector<char> b(sz+1); DWORD rd; ReadFile(hf, &b[0], sz, &rd, NULL); b[rd]='\0'; oldContent = std::string(&b[0], rd); }
        CloseHandle(hf);
    }
    if (oldContent.find("function drun") != std::string::npos) { printf("PowerShell profile \u5df2\u914d\u7f6e\u3002\n"); return false; }
    std::string newContent = oldContent;
    if (!newContent.empty() && newContent.back() != '\n') newContent += "\r\n";
    newContent += "# === Drun Launcher ===\r\n";
    newContent += "function drun { & \"" + WtoU8(g_launcherDir) + "\\drun.exe\" @args }\r\n";
    newContent += "function drun-plus { & \"" + WtoU8(g_launcherDir) + "\\drun-plus.exe\" @args }\r\n";
    hf = CreateFileW(pf.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) { printf("\u65e0\u6cd5\u521b\u5efa PowerShell profile\u3002\n"); LogError(L"Failed to create PS profile"); return false; }
    DWORD w2; WriteFile(hf, newContent.c_str(), (DWORD)newContent.size(), &w2, NULL); CloseHandle(hf);
    printf("\u5df2\u521b\u5efa PowerShell profile: %s\n", WtoU8(pf.c_str()).c_str());
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    LoadConfig();
    CheckErrorLog();
    const wchar_t* targetDir = g_launcherDir;
    if (argc >= 2 && (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0)) {
        printf("\nDrun-Path\n\n");
        printf("  drun-path          \u6dfb\u52a0\u5230 PATH\n");
        printf("  drun-path --check  \u68c0\u67e5\u72b6\u6001\n");
        printf("  drun-path --restore \u6062\u590d PATH\u5907\u4efd\n");
        printf("\n\u6309 Enter \u9000\u51fa..."); getchar(); return 0;
    }
    if (argc >= 2 && wcscmp(argv[1], L"--check") == 0) {
        WCHAR buf[32767]; DWORD len = GetEnvironmentVariableW(L"Path", buf, 32767);
        printf("drun \u5728 PATH \u4e2d: %s\n", std::wstring(buf,len).find(g_launcherDir)!=std::wstring::npos?"\u662f":"\u5426");
        printf("\n\u6309 Enter \u9000\u51fa..."); getchar(); return 0;
    }
    if (argc >= 2) targetDir = argv[1];
    if (GetFileAttributesW(targetDir) == INVALID_FILE_ATTRIBUTES) { printf("\u76ee\u5f55\u4e0d\u5b58\u5728\n"); getchar(); return 1; }
    printf("\n=== Drun-Path ===\n\n");
    AddToPath(targetDir);
    CreateProfile();
    printf("\n\u5b8c\u6210!\n\n\u6309 Enter \u9000\u51fa..."); getchar();
    return 0;
}
