#define PYCOMPILER_EXPORTS
#include "pycompiler.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static WCHAR g_lastError[MAX_ERROR_LEN] = L"";

// Find python.exe: check common locations
static std::wstring FindPython() {
    // Priority: D:\py, then PATH
    const WCHAR* candidates[] = {
        L"D:\\py\\python.exe",
        L"C:\\Python312\\python.exe",
        L"C:\\Python36\\python.exe",
    };
    
    for (int i = 0; i < 3; i++) {
        if (GetFileAttributesW(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
            return candidates[i];
        }
    }
    
    // Search PATH
    WCHAR buf[MAX_PATH];
    DWORD len = SearchPathW(NULL, L"python.exe", NULL, MAX_PATH, buf, NULL);
    if (len > 0) return buf;
    
    return L"python";
}

// Capture output from a process
static BOOL RunPythonCmd(const WCHAR* args, std::string& stdoutStr, std::string& stderrStr, DWORD* exitCode) {
    WCHAR cmdLine[8192];
    std::wstring python = FindPython();
    swprintf_s(cmdLine, L"\"%s\" %s", python.c_str(), args);
    
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hOutRead, hOutWrite, hErrRead, hErrWrite;
    CreatePipe(&hOutRead, &hOutWrite, &sa, 0);
    CreatePipe(&hErrRead, &hErrWrite, &sa, 0);
    
    SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutWrite;
    si.hStdError = hErrWrite;
    
    PROCESS_INFORMATION pi = { 0 };
    BOOL ok = CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hOutWrite);
    CloseHandle(hErrWrite);
    
    if (!ok) {
        swprintf_s(g_lastError, L"Failed to launch Python: %s (code %lu)", python.c_str(), GetLastError());
        CloseHandle(hOutRead); CloseHandle(hErrRead);
        return FALSE;
    }
    
    // Read output
    char buf[4096];
    DWORD bytesRead;
    stdoutStr.clear();
    stderrStr.clear();
    
    while (ReadFile(hOutRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        stdoutStr += buf;
    }
    while (ReadFile(hErrRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        stderrStr += buf;
    }
    
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hOutRead);
    CloseHandle(hErrRead);
    
    return TRUE;
}

// Generate .pyc output path
static std::wstring PycPath(const WCHAR* pyPath) {
    std::wstring result(pyPath);
    result += L"c"; // .py -> .pyc
    return result;
}

// Generate temp path for retry
static std::wstring TempPyPath(const WCHAR* original) {
    WCHAR tempDir[MAX_PATH], tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    
    // Extract filename
    const WCHAR* name = wcsrchr(original, L'\\');
    if (!name) name = original;
    else name++;
    
    swprintf_s(tempFile, L"%s_pycompile_%lu.py", name, GetCurrentProcessId());
    
    std::wstring result = tempDir;
    result += tempFile;
    return result;
}

static void SetError(const WCHAR* msg) {
    wcsncpy_s(g_lastError, msg, MAX_ERROR_LEN - 1);
}

PYCOMPILER_API CompileResult CompilePy(const WCHAR* pyPath) {
    CompileResult result = { 0 };
    result.attempts = 0;
    
    // Check file exists and is .py
    if (GetFileAttributesW(pyPath) == INVALID_FILE_ATTRIBUTES) {
        result.success = FALSE;
        swprintf_s(result.errorMsg, L"File not found: %s", pyPath);
        return result;
    }
    
    size_t len = wcslen(pyPath);
    if (len < 4 || _wcsicmp(pyPath + len - 3, L".py") != 0) {
        result.success = FALSE;
        swprintf_s(result.errorMsg, L"Not a .py file: %s", pyPath);
        return result;
    }
    
    // Build command: python -m py_compile "file.py"
    WCHAR args[4096];
    swprintf_s(args, L"-m py_compile \"%s\"", pyPath);
    
    std::string out, err;
    DWORD exitCode = 0;
    result.attempts = 1;
    
    if (!RunPythonCmd(args, out, err, &exitCode)) {
        result.success = FALSE;
        wcsncpy_s(result.errorMsg, g_lastError, MAX_ERROR_LEN - 1);
        return result;
    }
    
    if (exitCode == 0) {
        result.success = TRUE;
        std::wstring pyc = PycPath(pyPath);
        wcsncpy_s(result.outputPath, pyc.c_str(), MAX_PATH_LEN - 1);
    } else {
        result.success = FALSE;
        int len = MultiByteToWideChar(CP_UTF8, 0, err.c_str(), -1, NULL, 0);
        if (len > MAX_ERROR_LEN) len = MAX_ERROR_LEN;
        MultiByteToWideChar(CP_UTF8, 0, err.c_str(), -1, result.errorMsg, len);
        if (result.errorMsg[0] == L'\0') {
            len = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, NULL, 0);
            if (len > MAX_ERROR_LEN) len = MAX_ERROR_LEN;
            MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, result.errorMsg, len);
        }
    }
    
    return result;
}

PYCOMPILER_API CompileResult ForceCompilePy(const WCHAR* pyPath) {
    // Step 1: Normal compile
    CompileResult r1 = CompilePy(pyPath);
    if (r1.success) return r1;
    
    // Step 2: Copy source to temp, re-encode as UTF-8, retry
    SetError(L"First attempt failed, retrying with source copy...");
    
    // Read original source
    HANDLE hFile = CreateFileW(pyPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        CompileResult fail = { 0 };
        fail.success = FALSE;
        fail.attempts = 1;
        swprintf_s(fail.errorMsg, L"Cannot open source for retry: %s (code %lu)", pyPath, GetLastError());
        return fail;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize > 10 * 1024 * 1024) { // 10MB limit
        CloseHandle(hFile);
        CompileResult fail = { 0 };
        fail.success = FALSE;
        fail.attempts = 1;
        swprintf_s(fail.errorMsg, L"Source file too large or unreadable: %lu bytes", fileSize);
        return fail;
    }
    
    std::vector<char> content(fileSize + 1);
    DWORD bytesRead;
    ReadFile(hFile, &content[0], fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    content[bytesRead] = '\0';
    
    // Write to temp file (strip BOM if present, force UTF-8 without BOM)
    std::wstring tempPath = TempPyPath(pyPath);
    HANDLE hTemp = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTemp == INVALID_HANDLE_VALUE) {
        CompileResult fail = { 0 };
        fail.success = FALSE;
        fail.attempts = 1;
        swprintf_s(fail.errorMsg, L"Cannot create temp file: %s (code %lu)", tempPath.c_str(), GetLastError());
        return fail;
    }
    
    // Skip BOM (0xEF 0xBB 0xBF) if present
    char* start = &content[0];
    DWORD size = bytesRead;
    if (size >= 3 && (unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) {
        start += 3;
        size -= 3;
    }
    
    DWORD written;
    WriteFile(hTemp, start, size, &written, NULL);
    CloseHandle(hTemp);
    
    // Retry compile
    WCHAR args[4096];
    swprintf_s(args, L"-m py_compile \"%s\"", tempPath.c_str());
    
    std::string out, err;
    DWORD exitCode = 0;
    
    CompileResult result = { 0 };
    result.attempts = 2;
    
    if (!RunPythonCmd(args, out, err, &exitCode)) {
        result.success = FALSE;
        wcsncpy_s(result.errorMsg, g_lastError, MAX_ERROR_LEN - 1);
        DeleteFileW(tempPath.c_str());
        return result;
    }
    
    DeleteFileW(tempPath.c_str()); // Clean up temp
    
    if (exitCode == 0) {
        result.success = TRUE;
        wcsncpy_s(result.outputPath, r1.outputPath, MAX_PATH_LEN - 1); // Original .pyc path
    } else {
        result.success = FALSE;
        // Show both errors
        swprintf_s(result.errorMsg, L"[Attempt 1] %s\n[Attempt 2] %S", r1.errorMsg, err.c_str());
    }
    
    return result;
}

PYCOMPILER_API void GetLastCompileError(WCHAR* buf, int bufSize) {
    wcsncpy_s(buf, bufSize, g_lastError, bufSize - 1);
}
