#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <io.h>
#include <fcntl.h>

// Paths
#define LAUNCHER_DIR  L"D:\\desktop\\系统工具\\npm-launcher"
#define JSON_PATH     L"D:\\desktop\\系统工具\\npm-launcher\\exe-map.json"
#define CPP_PATH      L"D:\\desktop\\系统工具\\npm-launcher\\drun_data.cpp"
#define DRUN_EXE      L"D:\\desktop\\系统工具\\npm-launcher\\drun.exe"
#define GPP_PATH      L"C:\\mingw64-tool\\mingw64\\bin\\g++.exe"

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, NULL, NULL);
    return result;
}

std::wstring Sanitize(const std::wstring& raw) {
    std::wstring result;
    for (wchar_t ch : raw) {
        if (wcschr(L" :：\\/:*?\"<>|()（）[]【】&@#$%^+={};,!，。、；！", ch)) {
            if (result.empty() || result.back() != L'-') result += L'-';
        } else {
            result += ch;
        }
    }
    // Trim leading/trailing hyphens, collapse multiple
    while (!result.empty() && result.front() == L'-') result.erase(0, 1);
    while (!result.empty() && result.back() == L'-') result.pop_back();
    std::wstring clean;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == L'-' && i > 0 && result[i-1] == L'-') continue;
        clean += result[i];
    }
    return clean.empty() ? L"unknown" : clean;
}

struct ExeEntry {
    std::wstring name;
    std::wstring path;
};

bool ReadJson(const wchar_t* path, std::vector<ExeEntry>& entries) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD size = GetFileSize(hFile, NULL);
    std::vector<char> buf(size + 1);
    DWORD read;
    ReadFile(hFile, &buf[0], size, &read, NULL);
    CloseHandle(hFile);
    buf[read] = '\0';
    
    // Simple JSON parser for our format: {"name1":"path1","name2":"path2",...}
    std::string json(buf.begin(), buf.begin() + read);
    
    // Find opening { and closing }
    size_t start = json.find('{');
    size_t end = json.rfind('}');
    if (start == std::string::npos || end == std::string::npos) return false;
    
    std::string inner = json.substr(start + 1, end - start - 1);
    
    // Parse key-value pairs
    size_t pos = 0;
    while (pos < inner.size()) {
        // Skip whitespace
        while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == '\n' || inner[pos] == '\r' || inner[pos] == '\t')) pos++;
        if (pos >= inner.size()) break;
        
        // Find key (between quotes)
        size_t keyStart = inner.find('"', pos);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = inner.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        
        std::string key = inner.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        // Find colon
        size_t colon = inner.find(':', keyEnd + 1);
        if (colon == std::string::npos) break;
        
        // Find value (between quotes)
        size_t valStart = inner.find('"', colon + 1);
        if (valStart == std::string::npos) break;
        size_t valEnd = inner.find('"', valStart + 1);
        if (valEnd == std::string::npos) break;
        
        std::string value = inner.substr(valStart + 1, valEnd - valStart - 1);
        
        // Convert to wstring
        int wlen = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, NULL, 0);
        std::wstring wkey(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, &wkey[0], wlen);
        
        wlen = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, NULL, 0);
        std::wstring wval(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &wval[0], wlen);
        
        // Fix double-escaped backslashes (JSON \\ -> \)
        size_t bp = 0;
        while ((bp = wval.find(L"\\\\", bp)) != std::wstring::npos) {
            wval.replace(bp, 2, L"\\");
            bp++;
        }
        
        entries.push_back({wkey, wval});
        pos = valEnd + 1;
    }
    return true;
}

bool WriteJson(const wchar_t* path, const std::vector<ExeEntry>& entries) {
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    std::string out = "{\n";
    for (size_t i = 0; i < entries.size(); i++) {
        std::string name = WtoU8(entries[i].name.c_str());
        std::string pathStr = WtoU8(entries[i].path.c_str());
        
        // Escape backslashes for JSON
        std::string escapedPath;
        for (char c : pathStr) {
            if (c == '\\') escapedPath += "\\\\";
            else if (c == '"') escapedPath += "\\\"";
            else escapedPath += c;
        }
        
        out += "    \"" + name + "\": \"" + escapedPath + "\"";
        if (i < entries.size() - 1) out += ",";
        out += "\n";
    }
    out += "}";
    
    DWORD written;
    WriteFile(hFile, out.c_str(), (DWORD)out.size(), &written, NULL);
    CloseHandle(hFile);
    return true;
}

bool WriteCpp(const wchar_t* path, const std::vector<ExeEntry>& entries) {
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    std::string out;
    out += "#include \"drun_data.h\"\r\n";
    out += "#include <cwchar>\r\n";
    out += "#include <algorithm>\r\n\r\n";
    out += "const ExeEntry g_exeMap[] = {\r\n";
    
    for (const auto& e : entries) {
        std::string name = WtoU8(e.name.c_str());
        std::string pathStr = WtoU8(e.path.c_str());
        
        // Escape for C++ string: \ -> \\, " -> \"
        std::string escapedPath;
        for (char c : pathStr) {
            if (c == '\\') escapedPath += "\\\\";
            else if (c == '"') escapedPath += "\\\"";
            else escapedPath += c;
        }
        std::string escapedName;
        for (char c : name) {
            if (c == '"') escapedName += "\\\"";
            else escapedName += c;
        }
        
        out += "    { L\"" + escapedName + "\", L\"" + escapedPath + "\" },\r\n";
    }
    
    out += "};\r\n";
    out += "const int g_exeCount = sizeof(g_exeMap) / sizeof(g_exeMap[0]);\r\n";
    out += "const wchar_t* FindExe(const wchar_t* name) {\r\n";
    out += "    for (int i = 0; i < g_exeCount; i++) {\r\n";
    out += "        if (_wcsicmp(g_exeMap[i].name, name) == 0) return g_exeMap[i].path;\r\n";
    out += "    }\r\n    return nullptr;\r\n}\r\n";
    out += "std::vector<const ExeEntry*> FuzzyFind(const wchar_t* partial) {\r\n";
    out += "    std::vector<const ExeEntry*> results;\r\n";
    out += "    std::wstring plower(partial);\r\n";
    out += "    std::transform(plower.begin(), plower.end(), plower.begin(), ::towlower);\r\n";
    out += "    for (int i = 0; i < g_exeCount; i++) {\r\n";
    out += "        std::wstring name(g_exeMap[i].name);\r\n";
    out += "        std::wstring nlower = name;\r\n";
    out += "        std::transform(nlower.begin(), nlower.end(), nlower.begin(), ::towlower);\r\n";
    out += "        if (nlower.find(plower) != std::wstring::npos) results.push_back(&g_exeMap[i]);\r\n";
    out += "    }\r\n    return results;\r\n}\r\n";
    
    DWORD written;
    WriteFile(hFile, out.c_str(), (DWORD)out.size(), &written, NULL);
    CloseHandle(hFile);
    return true;
}

bool RecompileDrun() {
    // (progress bar shows status)
    
    // Need a temp dir without Chinese chars
    WCHAR cmdLine[2048];
    swprintf_s(cmdLine, 
        L"cmd /c \"set TMP=D:\\desktop\\tmp&& set TEMP=D:\\desktop\\tmp&& "
        L"cd /d %s&& "
        L"\"%s\" -o drun.exe drun_main.cpp drun_data.cpp -static -municode -mconsole -O2 -s\"",
        LAUNCHER_DIR, GPP_PATH);
    
    CreateDirectoryW(L"D:\\desktop\\tmp", NULL);
    
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        printf("无法启动 g++ (错误: %lu)\n", GetLastError());
        return false;
    }
    
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return exitCode == 0;
}

// Pip-style progress bar
void ShowProgress(int step, int total, const char* msg) {
    const int barWidth = 30;
    int filled = (step * barWidth) / total;
    int percent = (step * 100) / total;
    printf("\r  %3d%%\x1b[38;5;33m│", percent);
    for (int i = 0; i < barWidth; i++) {
        if (i < filled) printf("\x1b[38;5;33m█");
        else if (i == filled && step < total) printf("\x1b[38;5;240m▌");
        else printf("\x1b[38;5;240m ");
    }
    printf("\x1b[38;5;33m│\x1b[0m %d/%d %s", step, total, msg);
    fflush(stdout);
}

void PrintUsage() {
    printf("\nDrun-Plus - 添加 exe 到 drun 启动器\n\n");
    printf("用法:\n");
    printf("  drun-plus <exe路径>              添加 exe (自动命名)\n");
    printf("  drun-plus <exe路径> --name <名>  添加 exe (自定义名称)\n");
    printf("  drun-plus --list                 列出所有已注册的 exe\n");
    printf("  drun-plus --remove <名称>        移除已注册的 exe\n");
    printf("  drun-plus --help                 显示帮助\n\n");
    printf("示例:\n");
    printf("  drun-plus D:\\tools\\myapp.exe\n");
    printf("  drun-plus D:\\tools\\myapp.exe --name myapp\n");
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    if (argc < 2) {
        PrintUsage();
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    
    // --help
    if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0) {
        PrintUsage();
        printf("\n按 Enter 键退出..."); getchar();
        return 0;
    }
    
    // --list
    if (wcscmp(argv[1], L"--list") == 0 || wcscmp(argv[1], L"-l") == 0) {
        std::vector<ExeEntry> entries;
        if (!ReadJson(JSON_PATH, entries)) {
            printf("无法读取 %s\n", WtoU8(JSON_PATH).c_str());
            printf("\n按 Enter 键退出..."); getchar();
            return 1;
        }
        printf("\n已注册 %zu 个程序:\n\n", entries.size());
        for (const auto& e : entries) {
            printf("  %s\n    -> %s\n", WtoU8(e.name.c_str()).c_str(), WtoU8(e.path.c_str()).c_str());
        }
        printf("\n按 Enter 键退出..."); getchar();
        return 0;
    }
    
    // --remove
    if ((wcscmp(argv[1], L"--remove") == 0 || wcscmp(argv[1], L"-r") == 0) && argc >= 3) {
        std::vector<ExeEntry> entries;
        if (!ReadJson(JSON_PATH, entries)) {
            printf("无法读取 JSON。\n");
            printf("\n按 Enter 键退出..."); getchar();
            return 1;
        }
        
        std::wstring target = argv[2];
        auto it = std::remove_if(entries.begin(), entries.end(), [&](const ExeEntry& e) {
            return _wcsicmp(e.name.c_str(), target.c_str()) == 0;
        });
        
        if (it == entries.end()) {
            printf("未找到: %s\n", WtoU8(target.c_str()).c_str());
            printf("\n按 Enter 键退出..."); getchar();
            return 1;
        }
        entries.erase(it, entries.end());
        
        if (!WriteJson(JSON_PATH, entries)) {
            printf("写入 JSON 失败。\n");
            printf("\n按 Enter 键退出..."); getchar();
            return 1;
        }
        
        if (!WriteCpp(CPP_PATH, entries)) {
            printf("写入 CPP 失败。\n");
            printf("\n按 Enter 键退出..."); getchar();
            return 1;
        }
        
        ShowProgress(0, 3, "removing...");
        if (RecompileDrun()) {
            ShowProgress(3, 3, "drun.exe recompiled");
            printf("\n\n已移除: %s\n", WtoU8(target.c_str()).c_str());
        } else {
            printf("\n警告: drun.exe 重新编译失败。\n");
        }
        
        printf("\n按 Enter 键退出..."); getchar();
        return 0;
    }
    
    // Add mode: first arg is the exe path
    std::wstring exePath = argv[1];
    std::wstring customName;
    
    // Parse --name flag
    for (int i = 2; i < argc; i++) {
        if ((wcscmp(argv[i], L"--name") == 0 || wcscmp(argv[i], L"-n") == 0) && i + 1 < argc) {
            customName = argv[++i];
        }
    }
    
    // Validate file
    if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("文件不存在: %s\n", WtoU8(exePath.c_str()).c_str());
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    
    // Must be .exe
    size_t len = exePath.size();
    if (len < 5 || _wcsicmp(exePath.c_str() + len - 4, L".exe") != 0) {
        printf("不是 .exe 文件: %s\n", WtoU8(exePath.c_str()).c_str());
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    
    // Get full path
    WCHAR fullPath[MAX_PATH];
    if (GetFullPathNameW(exePath.c_str(), MAX_PATH, fullPath, NULL) > 0) {
        exePath = fullPath;
    }
    
    // Generate name
    std::wstring name;
    if (!customName.empty()) {
        name = Sanitize(customName);
    } else {
        // Extract filename without extension
        size_t slash = exePath.find_last_of(L"\\/");
        std::wstring filename = (slash != std::wstring::npos) ? exePath.substr(slash + 1) : exePath;
        size_t dot = filename.rfind(L'.');
        if (dot != std::wstring::npos) filename = filename.substr(0, dot);
        name = Sanitize(filename);
        
        // If duplicate, prefix with parent folder
        std::wstring parentFolder;
        if (slash != std::wstring::npos) {
            size_t prevSlash = exePath.find_last_of(L"\\/", slash - 1);
            parentFolder = (prevSlash != std::wstring::npos) ? 
                exePath.substr(prevSlash + 1, slash - prevSlash - 1) : 
                exePath.substr(0, slash);
            parentFolder = Sanitize(parentFolder);
        }
        
        // Read existing entries to check for duplicates
        std::vector<ExeEntry> existing;
        if (ReadJson(JSON_PATH, existing)) {
            bool dup = false;
            for (const auto& e : existing) {
                if (_wcsicmp(e.name.c_str(), name.c_str()) == 0) { dup = true; break; }
            }
            if (dup && !parentFolder.empty()) {
                name = parentFolder + L"-" + name;
            }
        }
    }
    
    // Final uniqueness check
    {
        std::vector<ExeEntry> existing;
        if (ReadJson(JSON_PATH, existing)) {
            std::wstring finalName = name;
            int suffix = 2;
            bool dup;
            do {
                dup = false;
                for (const auto& e : existing) {
                    if (_wcsicmp(e.name.c_str(), finalName.c_str()) == 0) {
                        dup = true;
                        finalName = name + L"-" + std::to_wstring(suffix++);
                        break;
                    }
                }
            } while (dup);
            name = finalName;
        }
    }
    
    // Read existing entries
    std::vector<ExeEntry> entries;
    ReadJson(JSON_PATH, entries); // If fails, start fresh
    
    // Check if path already exists
    for (const auto& e : entries) {
        if (_wcsicmp(e.path.c_str(), exePath.c_str()) == 0) {
            printf("该 exe 已注册为: %s\n", WtoU8(e.name.c_str()).c_str());
            printf("\n按 Enter 键退出..."); getchar();
            return 0;
        }
    }
    
    entries.push_back({name, exePath});
    
    printf("添加: %s\n  -> %s\n\n", WtoU8(name.c_str()).c_str(), WtoU8(exePath.c_str()).c_str());
    
    ShowProgress(0, 3, "starting...");
    // Write JSON
    if (!WriteJson(JSON_PATH, entries)) {
        printf("写入 JSON 失败。\n");
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    ShowProgress(1, 3, "exe-map.json updated"); Sleep(100);
    
    // Write CPP
    if (!WriteCpp(CPP_PATH, entries)) {
        printf("写入 CPP 失败。\n");
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    ShowProgress(2, 3, "drun_data.cpp generated"); Sleep(100); ShowProgress(2, 3, "compiling drun.exe ...");
    
    // Recompile drun.exe
    if (RecompileDrun()) {
        ShowProgress(3, 3, "drun.exe recompiled"); printf("\n");
    } else {
        printf("\n警告: drun.exe 重新编译失败，请手动重编。\n");
        printf("命令: g++ -o drun.exe drun_main.cpp drun_data.cpp -static -municode -mconsole -O2 -s\n");
    }
    
    printf("\n现在可以用 drun %s 启动!\n", WtoU8(name.c_str()).c_str());
    printf("\n按 Enter 键退出..."); getchar();
    return 0;
}
