#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <io.h>
#include <fcntl.h>
#include <shlobj.h>
#include <vector>
#include <cstdlib>

// Default fallback
const wchar_t* DEFAULT_DIR = L"D:\\desktop\\\u7cfb\u7edf\u5de5\u5177\\npm-launcher";
wchar_t g_launcherDir[MAX_PATH];

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string r(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &r[0], len, NULL, NULL);
    return r;
}

// === Config loader (Issue #1: configurable paths) ===
void LoadConfig() {
    wcscpy_s(g_launcherDir, DEFAULT_DIR);

    const wchar_t* knownConfigs[] = {
        L"D:\\desktop\\\u7cfb\u7edf\u5de5\u5177\\npm-launcher\\config.ini",
    };
    wchar_t cfgPath[MAX_PATH] = {0};
    for (int i = 0; i < 1; i++) {
        if (GetFileAttributesW(knownConfigs[i]) != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(cfgPath, knownConfigs[i]);
            break;
        }
    }
    if (cfgPath[0] == 0) {
        WCHAR localAppData[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
            swprintf_s(cfgPath, L"%s\\drun-launcher\\config.ini", localAppData);
            if (GetFileAttributesW(cfgPath) == INVALID_FILE_ATTRIBUTES) cfgPath[0] = 0;
        }
    }
    if (cfgPath[0] == 0) return;

    WCHAR buf[MAX_PATH];
    GetPrivateProfileStringW(L"install", L"path", L"", buf, MAX_PATH, cfgPath);
    if (buf[0]) wcscpy_s(g_launcherDir, buf);
}

// === PATH safety with backup (Issue #4: merge + rollback) ===
bool BackupPathToFile(const wchar_t* backupPath) {
    WCHAR pathBuf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        pathBuf[0] = L'\0'; len = 0;
    }
    HANDLE h = CreateFileW(backupPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w;
    WriteFile(h, pathBuf, len * sizeof(WCHAR), &w, NULL);
    CloseHandle(h);
    return true;
}

std::wstring ReadPathBackup(const wchar_t* backupPath) {
    HANDLE h = CreateFileW(backupPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return L"";
    DWORD size = GetFileSize(h, NULL);
    if (size == 0 || size > 65536) { CloseHandle(h); return L""; }
    std::vector<WCHAR> buf(size/2 + 1);
    DWORD read;
    ReadFile(h, &buf[0], size, &read, NULL);
    CloseHandle(h);
    buf[read/2] = L'\0';
    return std::wstring(&buf[0]);
}

bool AddToPath(const wchar_t* dir) {
    WCHAR pathBuf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        pathBuf[0] = L'\0'; len = 0;
    }

    std::wstring pathStr(pathBuf, len);
    std::wstring dirStr(dir);

    // Normalize: ensure trailing backslash for comparison
    std::wstring dirWithSlash = dirStr;
    if (!dirWithSlash.empty() && dirWithSlash.back() != L'\\') dirWithSlash += L'\\';

    if (pathStr.find(dirWithSlash) != std::wstring::npos ||
        pathStr.find(dirStr) != std::wstring::npos) {
        printf("drun \u5df2\u5728 PATH \u4e2d: %s\n", WtoU8(dir).c_str());
        return false;
    }

    // === Backup PATH before modification ===
    WCHAR backupPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, backupPath) == S_OK) {
        wcscat_s(backupPath, L"\\drun_path_backup.txt");
        BackupPathToFile(backupPath);
    }

    // Merge (append if not present)
    std::wstring newPath = pathStr;
    if (!newPath.empty() && newPath.back() != L';') newPath += L';';
    newPath += dirStr;

    // Write to registry
    HKEY hKey;
    LONG regResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE | KEY_READ, &hKey);
    if (regResult != ERROR_SUCCESS) {
        printf("\u65e0\u6cd5\u6253\u5f00\u6ce8\u518c\u8868 (\u9519\u8bef\u7801: %d)\u3002\n", regResult);
        return false;
    }

    regResult = RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ,
        (BYTE*)newPath.c_str(), (DWORD)((newPath.size() + 1) * sizeof(WCHAR)));
    if (regResult != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        printf("\u5199\u5165\u6ce8\u518c\u8868\u5931\u8d25 (\u9519\u8bef\u7801: %d)\u3002\u5df2\u5907\u4efd\u539f PATH\u3002\n", regResult);
        return false;
    }
    RegCloseKey(hKey);

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);

    printf("\u5df2\u5c06 drun \u6dfb\u52a0\u5230\u7528\u6237 PATH: %s\n", WtoU8(dir).c_str());
    return true;
}

// === PowerShell profile creation with merge (Issue #4) ===
bool CreateProfile() {
    WCHAR profileDir[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, profileDir) != S_OK) {
        printf("\u65e0\u6cd5\u83b7\u53d6\u6587\u6863\u8def\u5f84\u3002\n");
        return false;
    }

    std::wstring psDir = std::wstring(profileDir) + L"\\WindowsPowerShell";
    CreateDirectoryW(psDir.c_str(), NULL);
    std::wstring profilePath = psDir + L"\\profile.ps1";

    // Read existing content
    std::string existingContent;
    HANDLE hFile = CreateFileW(profilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size > 0 && size < 65536) {
            std::vector<char> buf(size + 1);
            DWORD read;
            ReadFile(hFile, &buf[0], size, &read, NULL);
            buf[read] = '\0';
            existingContent = std::string(&buf[0], read);
        }
        CloseHandle(hFile);
    }

    // Check if already configured
    if (existingContent.find("function drun") != std::string::npos) {
        printf("PowerShell profile \u5df2\u5305\u542b drun \u51fd\u6570\u3002\n");
        return false;
    }

    // Merge: keep existing content, append drun functions
    std::string newContent = existingContent;
    if (!newContent.empty() && newContent.back() != '\n') newContent += "\r\n";

    std::string drunFunc = "function drun { & \"" + WtoU8(g_launcherDir) + "\\drun.exe\" @args }\r\n";
    std::string plusFunc = "function drun-plus { & \"" + WtoU8(g_launcherDir) + "\\drun-plus.exe\" @args }\r\n";

    newContent += "# === Drun Launcher ===\r\n";
    newContent += drunFunc;
    newContent += plusFunc;

    hFile = CreateFileW(profilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("\u65e0\u6cd5\u521b\u5efa PowerShell profile\u3002\n");
        return false;
    }

    DWORD written;
    WriteFile(hFile, newContent.c_str(), (DWORD)newContent.size(), &written, NULL);
    CloseHandle(hFile);

    printf("\u5df2\u521b\u5efa PowerShell profile: %s\n", WtoU8(profilePath.c_str()).c_str());
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    LoadConfig();

    const wchar_t* targetDir = g_launcherDir;

    if (argc >= 2 && (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0)) {
        printf("\nDrun-Path - \u4e00\u952e\u5c06 drun \u52a0\u5165\u7cfb\u7edf PATH\n\n");
        printf("\u7528\u6cd5:\n");
        printf("  drun-path              \u6dfb\u52a0\u9ed8\u8ba4\u76ee\u5f55\u5230 PATH\n");
        printf("  drun-path <\u76ee\u5f55>       \u6dfb\u52a0\u6307\u5b9a\u76ee\u5f55\u5230 PATH\n");
        printf("  drun-path --check      \u68c0\u67e5\u5f53\u524d PATH \u72b6\u6001\n");
        printf("  drun-path --restore    \u6062\u590d PATH \u5907\u4efd\n\n");
        printf("\u9ed8\u8ba4\u76ee\u5f55: %s\n", WtoU8(g_launcherDir).c_str());
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 0;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--check") == 0) {
        WCHAR buf[32767];
        DWORD len = GetEnvironmentVariableW(L"Path", buf, 32767);
        std::wstring pathStr(buf, len);
        bool found = pathStr.find(g_launcherDir) != std::wstring::npos;
        printf("drun \u76ee\u5f55\u5728 PATH \u4e2d: %s\n", found ? "\u662f" : "\u5426");

        WCHAR docs[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);
        std::wstring profilePath = std::wstring(docs) + L"\\WindowsPowerShell\\profile.ps1";
        bool hasProfile = GetFileAttributesW(profilePath.c_str()) != INVALID_FILE_ATTRIBUTES;
        printf("PowerShell profile: %s\n", hasProfile ? "\u5df2\u914d\u7f6e" : "\u672a\u914d\u7f6e");

        WCHAR backupPath[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, backupPath) == S_OK) {
            wcscat_s(backupPath, L"\\drun_path_backup.txt");
            if (GetFileAttributesW(backupPath) != INVALID_FILE_ATTRIBUTES)
                printf("PATH \u5907\u4efd: \u5df2\u5b58\u5728 (%s)\n", WtoU8(backupPath).c_str());
        }

        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 0;
    }

    // Restore PATH from backup
    if (argc >= 2 && wcscmp(argv[1], L"--restore") == 0) {
        WCHAR backupPath[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, backupPath) == S_OK) {
            wcscat_s(backupPath, L"\\drun_path_backup.txt");
            std::wstring savedPath = ReadPathBackup(backupPath);
            if (savedPath.empty()) {
                printf("\u672a\u627e\u5230 PATH \u5907\u4efd\u3002\n");
                printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
                return 1;
            }

            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
                printf("\u65e0\u6cd5\u6253\u5f00\u6ce8\u518c\u8868\u3002\n");
                printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
                return 1;
            }
            RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ,
                (BYTE*)savedPath.c_str(), (DWORD)((savedPath.size() + 1) * sizeof(WCHAR)));
            RegCloseKey(hKey);
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
            printf("PATH \u5df2\u6062\u590d\u4e3a\u5907\u4efd\u7248\u672c\u3002\n");
        }
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 0;
    }

    if (argc >= 2) {
        targetDir = argv[1];
    }

    printf("\n=== Drun-Path \u5b89\u88c5 ===\n\n");

    if (GetFileAttributesW(targetDir) == INVALID_FILE_ATTRIBUTES) {
        printf("\u9519\u8bef: \u76ee\u5f55\u4e0d\u5b58\u5728: %s\n", WtoU8(targetDir).c_str());
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }

    bool pathAdded = AddToPath(targetDir);
    bool profileCreated = CreateProfile();

    printf("\n========================\n");
    if (pathAdded || profileCreated) {
        printf("\u5b89\u88c5\u5b8c\u6210! \u8bf7\u91cd\u65b0\u6253\u5f00\u7ec8\u7aef\u3002\n\n");
        printf("\u7136\u540e\u5c31\u53ef\u4ee5\u4f7f\u7528:\n");
        printf("  drun              \u5217\u51fa\u6240\u6709\u7a0b\u5e8f\n");
        printf("  drun <\u540d\u79f0>        \u542f\u52a8\u7a0b\u5e8f\n");
        printf("  drun-plus <exe>   \u6dfb\u52a0\u65b0\u7a0b\u5e8f\n");
    } else {
        printf("drun \u5df2\u5b89\u88c5\uff0c\u65e0\u9700\u91cd\u590d\u64cd\u4f5c\u3002\n");
    }

    printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
    return 0;
}
