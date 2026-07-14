#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <io.h>
#include <fcntl.h>
#include <string>
#include "pycompiler.h"

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, NULL, NULL);
    return result;
}

void PrintUsage() {
    printf("PyCompiler - Force compile Python source files\n\n");
    printf("Usage:\n");
    printf("  pycompiler <file.py>          Compile with retry\n");
    printf("  pycompiler --force <file.py>  Force compile\n");
    printf("  pycompiler --help             Show this help\n\n");
    printf("On failure, copies source to temp file (BOM stripped) and retries.\n");
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    if (argc < 2) {
        PrintUsage();
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    
    BOOL forceMode = FALSE;
    const wchar_t* filePath = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--force") == 0 || wcscmp(argv[i], L"-f") == 0) {
            forceMode = TRUE;
        } else if (wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0) {
            PrintUsage();
            printf("\n按 Enter 键退出..."); getchar();
            return 0;
        } else {
            filePath = argv[i];
        }
    }
    
    if (!filePath) {
        printf("Error: No .py file specified.\n");
        PrintUsage();
        printf("\n按 Enter 键退出..."); getchar();
        return 1;
    }
    
    wchar_t fullPath[MAX_PATH];
    if (GetFullPathNameW(filePath, MAX_PATH, fullPath, NULL) == 0) {
        wcscpy_s(fullPath, filePath);
    }
    
    printf("Target: %s\n", WtoU8(fullPath).c_str());
    printf("Mode: %s\n\n", forceMode ? "Force (with retry)" : "Standard");
    
    CompileResult result;
    if (forceMode) {
        printf("[1/2] First attempt...\n");
        result = CompilePy(fullPath);
        if (!result.success) {
            printf("[1/2] Failed, retrying with source copy...\n");
            result = ForceCompilePy(fullPath);
        }
    } else {
        result = ForceCompilePy(fullPath);
    }
    
    printf("\n========================================\n");
    if (result.success) {
        printf("SUCCESS (attempts: %d)\n", result.attempts);
        printf("Output: %s\n", WtoU8(result.outputPath).c_str());
    } else {
        printf("FAILED (attempts: %d)\n", result.attempts);
        printf("Error:\n%s\n", WtoU8(result.errorMsg).c_str());
    }
    
    printf("\n按 Enter 键退出..."); getchar();
    return result.success ? 0 : 1;
}
