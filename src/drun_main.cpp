#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include "drun_data.h"

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, NULL, NULL);
    return result;
}

void PrintAll() {
    printf("\n===== 可用命令 (共 %d 个) =====\n\n", g_exeCount);
    for (int i = 0; i < g_exeCount; i++) {
        printf("  %s\n", WtoU8(g_exeMap[i].name).c_str());
        printf("    -> %s\n", WtoU8(g_exeMap[i].path).c_str());
    }
    printf("\n");
}

void PrintMatches(const std::vector<const ExeEntry*>& matches) {
    if (matches.empty()) { printf("未找到匹配的程序。\n"); return; }
    printf("\n找到 %zu 个匹配:\n\n", matches.size());
    for (const auto* entry : matches) {
        printf("  %s\n", WtoU8(entry->name).c_str());
        printf("    -> %s\n", WtoU8(entry->path).c_str());
    }
    printf("\n");
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    if (argc < 2) { PrintAll(); return 0; }
    
    const wchar_t* query = argv[1];
    const wchar_t* path = FindExe(query);
    if (path) {
        printf("正在启动: %s\n", WtoU8(path).c_str());
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = path;
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess) CloseHandle(sei.hProcess);
            printf("已启动!\n");
        } else {
            printf("启动失败 (错误代码: %lu)\n", GetLastError());
        }
        return 0;
    }
    
    auto matches = FuzzyFind(query);
    if (matches.size() == 1) {
        path = matches[0]->path;
        printf("正在启动: %s\n", WtoU8(path).c_str());
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = path;
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess) CloseHandle(sei.hProcess);
            printf("已启动!\n");
        } else {
            printf("启动失败 (错误代码: %lu)\n", GetLastError());
        }
    } else {
        PrintMatches(matches);
    }
    return 0;
}
