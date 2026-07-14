#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <io.h>
#include <fcntl.h>
#include <shlobj.h>
#include <vector>
#include <cstdlib>

#define LAUNCHER_DIR L"D:\\desktop\\系统工具\\npm-launcher"

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string r(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &r[0], len, NULL, NULL);
    return r;
}

// Add directory to user PATH
bool AddToPath(const wchar_t* dir) {
    // Read current user PATH
    WCHAR pathBuf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        pathBuf[0] = L'\0';
        len = 0;
    }
    
    std::wstring pathStr(pathBuf, len);
    
    // Check if already in PATH
    std::wstring dirStr(dir);
    if (pathStr.find(dirStr) != std::wstring::npos) {
        printf("drun 已在 PATH 中: %s\n", WtoU8(dir).c_str());
        return false; // Already there, no change needed
    }
    
    // Append to PATH
    std::wstring newPath = pathStr;
    if (!newPath.empty() && newPath.back() != L';') newPath += L';';
    newPath += dirStr;
    
    // Write back to registry (persistent)
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        printf("无法打开注册表。\n");
        return false;
    }
    
    if (RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ, (BYTE*)newPath.c_str(), (DWORD)((newPath.size() + 1) * sizeof(WCHAR))) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        printf("写入注册表失败。\n");
        return false;
    }
    RegCloseKey(hKey);
    
    // Broadcast change
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    
    printf("已将 drun 添加到用户 PATH: %s\n", WtoU8(dir).c_str());
    return true;
}

// Create PowerShell profile with drun function
bool CreateProfile() {
    // Get profile path
    WCHAR profileDir[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, profileDir) != S_OK) {
        printf("无法获取文档路径。\n");
        return false;
    }
    
    std::wstring psDir = std::wstring(profileDir) + L"\\WindowsPowerShell";
    CreateDirectoryW(psDir.c_str(), NULL);
    
    std::wstring profilePath = psDir + L"\\profile.ps1";
    
    // Check if already has drun
    HANDLE hFile = CreateFileW(profilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size > 0) {
            std::vector<char> buf(size + 1);
            DWORD read;
            ReadFile(hFile, &buf[0], size, &read, NULL);
            CloseHandle(hFile);
            buf[read] = '\0';
            if (strstr(&buf[0], "function drun") != NULL) {
                printf("PowerShell profile 已包含 drun 函数。\n");
                return false;
            }
        } else {
            CloseHandle(hFile);
        }
    }
    
    // Write profile content
    std::string content;
    
    // Read existing if any
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        std::vector<char> buf(size + 1);
        DWORD read;
        ReadFile(hFile, &buf[0], size, &read, NULL);
        CloseHandle(hFile);
        buf[read] = '\0';
        content = std::string(&buf[0], read);
        if (!content.empty() && content.back() != '\n') content += "\r\n";
    }
    
    content += "function drun { & \"";
    content += WtoU8(LAUNCHER_DIR);
    content += "\\drun.exe\" @args }\r\n";
    content += "function drun-plus { & \"";
    content += WtoU8(LAUNCHER_DIR);
    content += "\\drun-plus.exe\" @args }\r\n";
    
    hFile = CreateFileW(profilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("无法创建 PowerShell profile。\n");
        return false;
    }
    
    DWORD written;
    WriteFile(hFile, content.c_str(), (DWORD)content.size(), &written, NULL);
    CloseHandle(hFile);
    
    printf("已创建 PowerShell profile: %s\n", WtoU8(profilePath.c_str()).c_str());
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    const wchar_t* targetDir = LAUNCHER_DIR;
    
    if (argc >= 2 && (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0)) {
        printf("\nDrun-Path - 一键将 drun 加入系统 PATH\n\n");
        printf("用法:\n");
        printf("  drun-path              添加默认目录到 PATH\n");
        printf("  drun-path <目录>       添加指定目录到 PATH\n");
        printf("  drun-path --check      检查当前 PATH 状态\n\n");
        printf("默认目录: %s\n", WtoU8(LAUNCHER_DIR).c_str());
        printf("\n按 Enter 键退出..."); getchar();
        return 0;
    }
    
    if (argc >= 2 && wcscmp(argv[1], L"--check") == 0) {
        WCHAR buf[32767];
        DWORD len = GetEnvironmentVariableW(L"Path", buf, 32767);
        std::wstring pathStr(buf, len);
        
        bool found = pathStr.find(LAUNCHER_DIR) != std::wstring::npos;
        printf("drun 目录在 PATH 中: %s\n", found ? "是" : "否");
        
        // Check profile
        WCHAR docs[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);
        std::wstring profilePath = std::wstring(docs) + L"\\WindowsPowerShell\\profile.ps1";
        bool hasProfile = GetFileAttributesW(profilePath.c_str()) != INVALID_FILE_ATTRIBUTES;
        printf("PowerShell profile: %s\n", hasProfile ? "已配置" : "未配置");
        
        printf("\n按 Enter 键退出..."); getchar();
        return 0;
    }
    
    if (argc >= 2) {
        targetDir = argv[1];
    }
    
    printf("\n=== Drun-Path 安装 ===\n\n");
    
    // Check if dir exists
    if (GetFileAttributesW(targetDir) == INVALID_FILE_ATTRIBUTES) {
        printf("错误: 目录不存在: %s\n", WtoU8(targetDir).c_str());
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    
    // Step 1: Add to PATH
    bool pathAdded = AddToPath(targetDir);
    
    // Step 2: Create PowerShell profile
    bool profileCreated = CreateProfile();
    
    printf("\n========================\n");
    if (pathAdded || profileCreated) {
        printf("安装完成! 请重新打开终端。\n\n");
        printf("然后就可以使用:\n");
        printf("  drun              列出所有程序\n");
        printf("  drun <名称>        启动程序\n");
        printf("  drun-plus <exe>   添加新程序\n");
    } else {
        printf("drun 已安装，无需重复操作。\n");
    }
    
    printf("\n按 Enter 键退出..."); getchar();
    return 0;
}
