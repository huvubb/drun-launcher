#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <io.h>
#include <fcntl.h>
#include <cstdlib>

// ====================== Language System ======================
#define LANG_EN 0
#define LANG_CN 1

int g_lang = LANG_EN;

const wchar_t* T(const wchar_t* en, const wchar_t* cn) {
    return g_lang == LANG_CN ? cn : en;
}

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string r(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &r[0], len, NULL, NULL);
    return r;
}

void PrintT(const wchar_t* en, const wchar_t* cn) {
    printf("%s", WtoU8(T(en, cn)).c_str());
}

// Extract resource to file
bool ExtractResource(int resId, const wchar_t* resType, const wchar_t* outPath) {
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resId), resType);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(NULL, hRes);
    void* ptr = LockResource(hData);
    
    HANDLE hFile = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    WriteFile(hFile, ptr, size, &written, NULL);
    CloseHandle(hFile);
    return true;
}

// Add directory to user PATH
bool AddToPath(const wchar_t* dir) {
    WCHAR buf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", buf, 32767);
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) { buf[0] = L'\0'; len = 0; }
    
    std::wstring pathStr(buf, len);
    std::wstring dirStr(dir);
    if (pathStr.find(dirStr) != std::wstring::npos) return false;
    
    if (!pathStr.empty() && pathStr.back() != L';') pathStr += L';';
    pathStr += dirStr;
    
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return false;
    if (RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ, (BYTE*)pathStr.c_str(), (DWORD)((pathStr.size()+1)*sizeof(WCHAR))) != ERROR_SUCCESS) {
        RegCloseKey(hKey); return false;
    }
    RegCloseKey(hKey);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    return true;
}

// Create PowerShell profile
bool CreatePSProfile(const wchar_t* installDir) {
    WCHAR docs[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs) != S_OK) return false;
    
    std::wstring psDir = std::wstring(docs) + L"\\WindowsPowerShell";
    CreateDirectoryW(psDir.c_str(), NULL);
    
    std::wstring profilePath = psDir + L"\\profile.ps1";
    
    // Read existing
    std::string existing;
    HANDLE hFile = CreateFileW(profilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size > 0 && size < 65536) {
            std::vector<char> buf(size + 1);
            DWORD read;
            ReadFile(hFile, &buf[0], size, &read, NULL);
            buf[read] = '\0';
            existing = std::string(&buf[0], read);
            if (!existing.empty() && existing.back() != '\n') existing += "\r\n";
        }
        CloseHandle(hFile);
    }
    
    if (existing.find("function drun") != std::string::npos) return false;
    
    existing += "function drun { & \"";
    existing += WtoU8(installDir);
    existing += "\\drun.exe\" @args }\r\n";
    existing += "function drun-plus { & \"";
    existing += WtoU8(installDir);
    existing += "\\drun-plus.exe\" @args }\r\n";
    
    hFile = CreateFileW(profilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    WriteFile(hFile, existing.c_str(), (DWORD)existing.size(), &written, NULL);
    CloseHandle(hFile);
    return true;
}

// Copy bundled file from beside the exe
bool CopyBundled(const wchar_t* srcName, const wchar_t* destDir) {
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    
    std::wstring src = std::wstring(exeDir) + L"\\" + srcName;
    std::wstring dest = std::wstring(destDir) + L"\\" + srcName;
    
    return CopyFileW(src.c_str(), dest.c_str(), FALSE);
}

void ShowBanner() {
    printf("\n");
    printf("  ╔══════════════════════════════════╗\n");
    printf("  ║        Drun Launcher Setup       ║\n");
    PrintT(L"  ║         Installer v1.0           ║\n", L"  ║         安装程序 v1.0            ║\n");
    printf("  ╚══════════════════════════════════╝\n\n");
}

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Drun Launcher Setup");
    
    // ===== Language Selection =====
    ShowBanner();
    printf("  [1] English\n");
    printf("  [2] 中文\n\n");
    PrintT(L"  Select language / 选择语言 [1-2]: ", L"  Select language / 选择语言 [1-2]: ");
    
    int choice = getchar();
    while (getchar() != '\n'); // flush stdin
    if (choice == '2') g_lang = LANG_CN;
    printf("\n");
    
    // ===== Welcome =====
    PrintT(L"Welcome to Drun Launcher Setup!\n\n", L"欢迎使用 Drun Launcher 安装程序！\n\n");
    PrintT(L"This will install drun - a fast command-line launcher for Windows.\n",
           L"本程序将安装 drun - Windows 极速命令行启动器。\n");
    PrintT(L"After installation, type 'drun <name>' to launch any program.\n\n",
           L"安装后，输入 'drun <名称>' 即可启动任意程序。\n\n");
    
    // ===== Choose install directory =====
    WCHAR defaultDir[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, defaultDir) != S_OK) {
        wcscpy_s(defaultDir, L"C:\\drun");
    }
    std::wstring installPath = std::wstring(defaultDir) + L"\\drun-launcher";
    
    PrintT(L"Install directory", L"安装目录");
    printf(" [%s]: ", WtoU8(installPath.c_str()).c_str());
    
    WCHAR input[MAX_PATH] = {0};
    fgetws(input, MAX_PATH, stdin);
    // Trim newline
    for (int i = 0; input[i]; i++) if (input[i] == L'\n' || input[i] == L'\r') { input[i] = L'\0'; break; }
    
    if (input[0] != L'\0') {
        installPath = input;
    }
    
    // Create directory
    CreateDirectoryW(installPath.c_str(), NULL);
    for (size_t i = 3; i < installPath.size(); i++) {
        if (installPath[i] == L'\\') {
            std::wstring part = installPath.substr(0, i);
            CreateDirectoryW(part.c_str(), NULL);
        }
    }
    CreateDirectoryW(installPath.c_str(), NULL);
    
    printf("\n");
    
    // ===== Install =====
    PrintT(L"Installing...\n\n", L"正在安装...\n\n");
    
    // Copy bundled files
    const wchar_t* files[] = { L"drun.exe", L"drun-plus.exe", L"drun-path.exe" };
    bool allOk = true;
    
    for (int i = 0; i < 3; i++) {
        printf("  [%d/3] ", i + 1);
        if (CopyBundled(files[i], installPath.c_str())) {
            PrintT(L"OK", L"完成");
            printf("  %s\n", WtoU8(files[i]).c_str());
        } else {
            PrintT(L"FAILED - file not found in setup directory\n", L"失败 - 安装目录中未找到文件\n");
            allOk = false;
        }
    }
    
    if (!allOk) {
        PrintT(L"\nERROR: Some files could not be installed.\n",
               L"\n错误: 部分文件安装失败。\n");
        PrintT(L"Make sure drun.exe, drun-plus.exe, drun-path.exe are in the same folder as this installer.\n",
               L"请确保 drun.exe、drun-plus.exe、drun-path.exe 与本安装程序在同一目录。\n");
        PrintT(L"\nPress Enter to exit...", L"\n按 Enter 键退出...");
        getchar();
        return 1;
    }
    
    // Create exe-map.json
    HANDLE hJson = CreateFileW((installPath + L"\\exe-map.json").c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hJson != INVALID_HANDLE_VALUE) {
        const char* emptyJson = "{\n}";
        DWORD w;
        WriteFile(hJson, emptyJson, 3, &w, NULL);
        CloseHandle(hJson);
    }
    
    printf("\n");
    
    // ===== Add to PATH =====
    PrintT(L"Adding to PATH... ", L"正在添加到 PATH... ");
    if (AddToPath(installPath.c_str())) {
        PrintT(L"OK\n", L"完成\n");
    } else {
        PrintT(L"Already in PATH\n", L"已在 PATH 中\n");
    }
    
    // ===== PowerShell Profile =====
    PrintT(L"Setting up PowerShell... ", L"正在配置 PowerShell... ");
    if (CreatePSProfile(installPath.c_str())) {
        PrintT(L"OK\n", L"完成\n");
    } else {
        PrintT(L"Skipped (already configured or no permission)\n", L"跳过（已配置或无权限）\n");
    }
    
    // ===== Done =====
    printf("\n");
    printf("  ╔══════════════════════════════════╗\n");
    PrintT(L"  ║      Installation Complete!     ║\n", L"  ║          安装完成！             ║\n");
    printf("  ╚══════════════════════════════════╝\n\n");
    
    PrintT(L"  Open a NEW terminal and try:\n", L"  打开新终端，试试:\n");
    printf("\n");
    printf("    drun                       ");
    PrintT(L"# List all programs\n", L"# 列出所有程序\n");
    printf("    drun-plus <exe> --name <n> ");
    PrintT(L"# Add a program\n", L"# 添加程序\n");
    printf("    drun-path --check          ");
    PrintT(L"# Check installation\n", L"# 检查安装状态\n");
    printf("\n");
    
    PrintT(L"  Install path: ", L"  安装路径: ");
    printf("%s\n", WtoU8(installPath.c_str()).c_str());
    
    PrintT(L"\n  Press Enter to exit...", L"\n  按 Enter 键退出...");
    getchar();
    return 0;
}
