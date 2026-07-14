#pragma once
#include <windows.h>
#include <cwchar>

#ifdef PYCOMPILER_EXPORTS
#define PYCOMPILER_API __declspec(dllexport)
#else
#define PYCOMPILER_API __declspec(dllimport)
#endif

#define MAX_ERROR_LEN 4096
#define MAX_PATH_LEN  1024

// Compilation result
typedef struct {
    BOOL  success;                // TRUE if compilation succeeded
    WCHAR errorMsg[MAX_ERROR_LEN]; // Error message on failure
    WCHAR outputPath[MAX_PATH_LEN];// Output .pyc path on success
    int   attempts;               // Number of attempts made
} CompileResult;

// Standard compile: python -m py_compile <file>
PYCOMPILER_API CompileResult CompilePy(const WCHAR* pyPath);

// Force compile with retry: on failure, copies source to new file and retries
PYCOMPILER_API CompileResult ForceCompilePy(const WCHAR* pyPath);

// Get last error as string
PYCOMPILER_API void GetLastCompileError(WCHAR* buf, int bufSize);
