#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <winhttp.h>
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
#include <cstdlib>
#include <ctime>
#include <cstdarg>
#include <cstdarg>

// === Default fallback paths (overridden by config.ini) ===
const wchar_t* DEFAULT_GPP  = L"C:\\mingw64-tool\\mingw64\\bin\\g++.exe";

void GetDefaultDir(WCHAR* out, DWORD size) {
    // Default to exe's own directory
    if (GetModuleFileNameW(NULL, out, size) > 0) {
        WCHAR* slash = wcsrchr(out, L'\\');
        if (slash) *slash = 0;
    } else {
        wcscpy_s(out, size, L".");
    }
}

wchar_t g_launcherDir[MAX_PATH];
wchar_t g_jsonPath[MAX_PATH];
wchar_t g_cppPath[MAX_PATH];
wchar_t g_drunExe[MAX_PATH];
wchar_t g_gppPath[MAX_PATH];
wchar_t g_logPath[MAX_PATH];

std::string WtoU8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, NULL, NULL);
    return result;
}

std::string ACPtoUTF8(const char* acp) {
    if (!acp || !*acp) return "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, acp, -1, NULL, 0);
    if (wlen <= 1) return "";
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, acp, -1, &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (ulen <= 1) return "";
    std::string result(ulen - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], ulen, NULL, NULL);
    return result;
}

std::wstring Sanitize(const std::wstring& raw) {
    std::wstring result;
    for (wchar_t ch : raw) {
        if (wcschr(L" :\uff1a\\/:*?\"<>|()\uff08\uff09[]\u3010\u3011&@#$%^+={};!,\uff0c\u3002\u3001\uff1b\uff01", ch)) {
            if (result.empty() || result.back() != L'-') result += L'-';
        } else {
            result += ch;
        }
    }
    while (!result.empty() && result.front() == L'-') result.erase(0, 1);
    while (!result.empty() && result.back() == L'-') result.pop_back();
    std::wstring clean;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == L'-' && i > 0 && result[i-1] == L'-') continue;
        clean += result[i];
    }
    return clean.empty() ? L"unknown" : clean;
}


// === Error logging ===
wchar_t g_errLogPath[MAX_PATH];
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

// === Auto SMTP sender (Winsock) ===
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

bool SendMailAuto(const char* subject, const char* body) {
    wchar_t pass[128] = {0};
    WCHAR cfgPath[MAX_PATH];
    WCHAR lad[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, lad) == S_OK) {
        swprintf_s(cfgPath, L"%s\drun-launcher\config.ini", lad);
        GetPrivateProfileStringW(L"install", L"smtp_pass", L"", pass, 128, cfgPath);
    }
    if (pass[0] == 0) {
        WCHAR exeDir[MAX_PATH];
        GetModuleFileNameW(NULL, exeDir, MAX_PATH);
        WCHAR* slash = wcsrchr(exeDir, L'\\');
        if (slash) *slash = 0;
        swprintf_s(cfgPath, L"%s\config.ini", exeDir);
        GetPrivateProfileStringW(L"install", L"smtp_pass", L"", pass, 128, cfgPath);
    }
    if (pass[0] == 0) wcscpy_s(pass, L"umoaffelouwobdhi");

    std::string sb(body);
    size_t pos = 0;
    while ((pos = sb.find("'", pos)) != std::string::npos) { sb.insert(pos, "''"); pos += 2; }
    pos = 0;
    while ((pos = sb.find("$", pos)) != std::string::npos) { sb.insert(pos, "`"); pos += 2; }
    std::string ss(subject);
    pos = 0;
    while ((pos = ss.find("'", pos)) != std::string::npos) { ss.insert(pos, "''"); pos += 2; }
    pos = 0;
    while ((pos = ss.find("$", pos)) != std::string::npos) { ss.insert(pos, "`"); pos += 2; }

    // Use PowerShell here-string to avoid newline issues
    std::string ps;
    ps += "$s=New-Object Net.Mail.SmtpClient('smtp.qq.com',587);";
    ps += "$s.EnableSsl=$true;";
    ps += "$s.Credentials=New-Object Net.NetworkCredential('810372789@qq.com','" + WtoU8(pass) + "');";
    ps += "$m=New-Object Net.Mail.MailMessage;";
    ps += "$m.From='810372789@qq.com';";
    ps += "$m.To.Add('810372789@qq.com');";
    ps += "$m.Subject='" + ss + "';";
    ps += "$m.Body=@'\n" + sb + "\n'@;";
    ps += "$m.SubjectEncoding=[Text.Encoding]::UTF8;";
    ps += "$m.BodyEncoding=[Text.Encoding]::UTF8;";
    ps += "try{$s.Send($m);exit 0}catch{exit 1}";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, ps.c_str(), -1, NULL, 0);
    if (wlen <= 1) return false;
    std::wstring wps(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, ps.c_str(), -1, &wps[0], wlen);

    DWORD b64len = 0;
    CryptBinaryToStringW((const BYTE*)wps.c_str(), (DWORD)((wlen-1)*sizeof(WCHAR)),
                         0x40000001, NULL, &b64len);
    if (b64len == 0) return false;
    std::wstring b64(b64len, L'\0');
    if (!CryptBinaryToStringW((const BYTE*)wps.c_str(), (DWORD)((wlen-1)*sizeof(WCHAR)),
                              0x40000001, &b64[0], &b64len)) return false;

    WCHAR cmd[4096];
    swprintf_s(cmd, L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand %s", b64.c_str());
    int ret = _wsystem(cmd);
    return ret == 0;
}

// === System info helpers ===
std::string GetIP() {
    HINTERNET hSes = WinHttpOpen(L"Drun/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSes) return "Unknown";
    HINTERNET hCon = WinHttpConnect(hSes, L"api.ipify.org", INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hCon) { WinHttpCloseHandle(hSes); return "Unknown"; }
    HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", L"/", NULL, NULL, NULL, 0);
    if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return "Unknown"; }
    std::string ip = "Unknown";
    if (WinHttpSendRequest(hReq, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(hReq, NULL)) {
        char buf[64]; DWORD r;
        if (WinHttpReadData(hReq, buf, sizeof(buf)-1, &r) && r > 0) { buf[r] = 0; ip = buf; while(!ip.empty()&&(ip.back()=='\n'||ip.back()=='\r'))ip.pop_back(); }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes);
    return ip;
}

std::string GetDeviceInfo() {
    std::string info;
    // Computer name
    WCHAR cn[MAX_COMPUTERNAME_LENGTH+1]; DWORD sz = MAX_COMPUTERNAME_LENGTH+1;
    if (GetComputerNameW(cn, &sz)) info += "PC: " + WtoU8(cn) + "\r\n";
    // OS version
    OSVERSIONINFOEXW vi = {sizeof(vi)};
    #pragma warning(suppress:4996)
    if (GetVersionExW((OSVERSIONINFOW*)&vi)) {
        char os[128]; snprintf(os, sizeof(os), "OS: Windows %lu.%lu Build %lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
        info += os + std::string("\r\n");
    }
    // Username
    WCHAR un[256]; DWORD usz = 256;
    if (GetUserNameW(un, &usz)) info += "User: " + WtoU8(un) + "\r\n";
    return info;
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
    bool critical = lg.find(L"] FAILED") != std::wstring::npos
                 || lg.find(L"] Recompile") != std::wstring::npos;
    if (!critical) { DeleteFileW(g_errLogPath); return; }

    printf("\n  *** \u68c0\u6d4b\u5230\u4e25\u91cd\u9519\u8bef ***\n");
    printf("  \u65e5\u5fd7: %s\n\n", WtoU8(g_errLogPath).c_str());
    printf("  [1] \u5ffd\u7565\uff0c\u4e0d\u518d\u63d0\u793a\n");
    printf("  [2] \u81ea\u52a8\u53d1\u9001\u62a5\u544a\u7ed9\u5f00\u53d1\u8005\n");
    printf("  \u8bf7\u9009\u62e9 (1/2): ");

    char ch = (char)getchar(); while (getchar() != '\n');
    if (ch == '2') {
        printf("\n  \u60a8\u7684\u8054\u7cfb\u65b9\u5f0f: ");
        char contact[256]; fgets(contact, 256, stdin);
        std::string sc = ACPtoUTF8(contact); while (!sc.empty() && (sc.back()=='\n'||sc.back()=='\r')) sc.pop_back();
        if (sc.empty()) sc = "\u672a\u586b\u5199";

        printf("  \u95ee\u9898\u63cf\u8ff0: ");
        char problem[1024]; fgets(problem, 1024, stdin);
        std::string sp = ACPtoUTF8(problem); while (!sp.empty() && (sp.back()=='\n'||sp.back()=='\r')) sp.pop_back();
        if (sp.empty()) sp = "\u672a\u586b\u5199";

        std::string ip = GetIP();
        std::string devInfo = GetDeviceInfo();

        std::string subject8 = "[Drun Error] " + sc;
        std::string body8 = "\u8054\u7cfb\u65b9\u5f0f: " + sc
            + "\r\n\u95ee\u9898: " + sp
            + "\r\n\r\n=== \u8bbe\u5907\u4fe1\u606f ===\r\nIP: " + ip
            + "\r\n" + devInfo
            + "\r\n=== \u9519\u8bef\u65e5\u5fd7 ===\r\n" + WtoU8(g_errLogPath)
            + "\r\n---\r\n" + WtoU8(lg.c_str());

        printf("  \u6b63\u5728 SMTP \u53d1\u9001...");
        bool sent = SendMailAuto(subject8.c_str(), body8.c_str());

        // Save report to desktop as backup
        WCHAR desk[MAX_PATH];
        std::wstring deskReport;
        if (SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, desk) == S_OK)
            deskReport = std::wstring(desk) + L"\\drun-error-report.txt";
        else 
            deskReport = L"drun-error-report.txt";
        HANDLE hr = CreateFileW(deskReport.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hr != INVALID_HANDLE_VALUE) {
            int bl = WideCharToMultiByte(CP_UTF8, 0, (std::wstring(body8.begin(),body8.end())).c_str(), -1, NULL, 0, NULL, NULL);
            if (bl > 1) { std::vector<char> b8(bl); WideCharToMultiByte(CP_UTF8, 0, (std::wstring(body8.begin(),body8.end())).c_str(), -1, &b8[0], bl, NULL, NULL);
                DWORD w2; WriteFile(hr, &b8[0], bl-1, &w2, NULL); }
            CloseHandle(hr);
        }

        if (sent) {
            printf(" \u2714 \u5df2\u53d1\u9001\u81f3 810372789@qq.com\n");
        } else {
            printf(" \u2716 SMTP \u5931\u8d25\n");
            printf("  \u2714 \u62a5\u544a\u5df2\u4fdd\u5b58: %s\n", WtoU8(deskReport.c_str()).c_str());
            printf("  \u8bf7\u5c06\u684c\u9762 drun-error-report.txt \u53d1\u7ed9 810372789@qq.com\n");
        }
    }
    DeleteFileW(g_errLogPath);
    printf("\n");
}

// === Config loader (Issue #1: configurable paths) ===
// === Config loader (Issue #1: configurable paths) ===
void LoadConfig() {
    // Priority 0: DRUN_INSTALL_DIR env var (avoids Chinese path garbling)
    WCHAR envBuf[MAX_PATH];
    if (GetEnvironmentVariableW(L"DRUN_INSTALL_DIR", envBuf, MAX_PATH) > 0) {
        wcscpy_s(g_launcherDir, envBuf);
        wcscpy_s(g_gppPath, DEFAULT_GPP);
        swprintf_s(g_jsonPath, L"%s\exe-map.json", g_launcherDir);
        swprintf_s(g_cppPath,  L"%s\drun_data.cpp", g_launcherDir);
        swprintf_s(g_drunExe,  L"%s\drun.exe", g_launcherDir);
        swprintf_s(g_logPath,  L"%s\compile.log", g_launcherDir);
        return;
    }
    GetDefaultDir(g_launcherDir, MAX_PATH);
    wcscpy_s(g_gppPath, DEFAULT_GPP);

    WCHAR exeCfg[MAX_PATH];
    swprintf_s(exeCfg, L"%s\\config.ini", g_launcherDir);
    wchar_t cfgPath[MAX_PATH] = {0};
    if (GetFileAttributesW(exeCfg) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(cfgPath, exeCfg);
    }
    if (cfgPath[0] == 0) {
        WCHAR localAppData[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
            swprintf_s(cfgPath, L"%s\\drun-launcher\\config.ini", localAppData);
            if (GetFileAttributesW(cfgPath) == INVALID_FILE_ATTRIBUTES) cfgPath[0] = 0;
        }
    }
    // Search PATH for config.ini
    if (cfgPath[0] == 0) {
        WCHAR pathBuf[32767];
        DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
        if (len > 0) {
            std::wstring ps(pathBuf, len);
            size_t pos = 0;
            while (pos < ps.size()) {
                size_t semi = ps.find(L';', pos);
                if (semi == std::wstring::npos) semi = ps.size();
                std::wstring dir = ps.substr(pos, semi - pos);
                swprintf_s(cfgPath, L"%s\\config.ini", dir.c_str());
                if (GetFileAttributesW(cfgPath) != INVALID_FILE_ATTRIBUTES) break;
                cfgPath[0] = 0;
                pos = semi + 1;
            }
        }
    }
    if (cfgPath[0] == 0) {
        swprintf_s(g_jsonPath, L"%s\\exe-map.json", g_launcherDir);
        swprintf_s(g_cppPath,  L"%s\\drun_data.cpp", g_launcherDir);
        swprintf_s(g_drunExe,  L"%s\\drun.exe", g_launcherDir);
        swprintf_s(g_logPath,  L"%s\\compile.log", g_launcherDir);
        return;
    }

    // Priority 1: DRUN_INSTALL_DIR (already checked above)
    if (GetEnvironmentVariableW(L"DRUN_INSTALL_DIR", NULL, 0) > 0) {}
    if (GetEnvironmentVariableW(L"DRUN_GPP_PATH", envBuf, MAX_PATH) > 0)
        wcscpy_s(g_gppPath, envBuf);

    // Priority 2: config.ini
    WCHAR buf[MAX_PATH];
    GetPrivateProfileStringW(L"install", L"path", L"", buf, MAX_PATH, cfgPath);
    if (buf[0] && GetEnvironmentVariableW(L"DRUN_INSTALL_DIR", NULL, 0) == 0)
        wcscpy_s(g_launcherDir, buf);

    GetPrivateProfileStringW(L"install", L"gpp_path", L"", buf, MAX_PATH, cfgPath);
    if (buf[0] && GetEnvironmentVariableW(L"DRUN_GPP_PATH", NULL, 0) == 0)
        wcscpy_s(g_gppPath, buf);

    swprintf_s(g_jsonPath, L"%s\\exe-map.json", g_launcherDir);
    swprintf_s(g_cppPath,  L"%s\\drun_data.cpp", g_launcherDir);
    swprintf_s(g_drunExe,  L"%s\\drun.exe", g_launcherDir);
    swprintf_s(g_logPath,  L"%s\\compile.log", g_launcherDir);
}

void EnsureConfig() {
    if (g_launcherDir[0] == 0) LoadConfig();
    if (g_launcherDir[0] == 0) {
        GetDefaultDir(g_launcherDir, MAX_PATH);
        wcscpy_s(g_gppPath, DEFAULT_GPP);
        swprintf_s(g_jsonPath, L"%s\\exe-map.json", g_launcherDir);
        swprintf_s(g_cppPath,  L"%s\\drun_data.cpp", g_launcherDir);
        swprintf_s(g_drunExe,  L"%s\\drun.exe", g_launcherDir);
        swprintf_s(g_logPath,  L"%s\\compile.log", g_launcherDir);
    }
}

struct ExeEntry {
    std::wstring name;
    std::wstring path;
};

// === Enhanced JSON parser (Issue #2: robustness) ===
bool ReadJson(const wchar_t* path, std::vector<ExeEntry>& entries) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false; // No file yet, not an error
    }
    DWORD size = GetFileSize(hFile, NULL);
    if (size == 0) { CloseHandle(hFile); return true; } // Empty file = no entries
    if (size > 1024 * 1024) { CloseHandle(hFile); return false; } // >1MB, refuse

    std::vector<char> buf(size + 1);
    DWORD read;
    if (!ReadFile(hFile, &buf[0], size, &read, NULL) || read == 0) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    buf[read] = '\0';
    std::string json(buf.begin(), buf.begin() + read);

    size_t start = json.find('{');
    size_t end = json.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return false;
    }
    std::string inner = json.substr(start + 1, end - start - 1);
    size_t pos = 0;

    while (pos < inner.size()) {
        while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == '\n' || inner[pos] == '\r' || inner[pos] == '\t' || inner[pos] == ','))
            pos++;
        if (pos >= inner.size()) break;

        size_t keyStart = inner.find('"', pos);
        if (keyStart == std::string::npos) break;

        // Handle escaped quotes inside key
        size_t keyEnd = keyStart + 1;
        while (keyEnd < inner.size()) {
            if (inner[keyEnd] == '"' && inner[keyEnd - 1] != '\\') break;
            keyEnd++;
        }
        if (keyEnd >= inner.size()) break;

        std::string key = inner.substr(keyStart + 1, keyEnd - keyStart - 1);
        if (key.empty()) { pos = keyEnd + 1; continue; }

        size_t colon = inner.find(':', keyEnd + 1);
        if (colon == std::string::npos) break;

        size_t valStart = inner.find('"', colon + 1);
        if (valStart == std::string::npos) break;

        size_t valEnd = valStart + 1;
        while (valEnd < inner.size()) {
            if (inner[valEnd] == '"' && inner[valEnd - 1] != '\\') break;
            valEnd++;
        }
        if (valEnd >= inner.size()) break;

        std::string value = inner.substr(valStart + 1, valEnd - valStart - 1);

        // Unescape JSON: \\ -> \, \" -> "
        for (size_t i = 0; i + 1 < value.size(); ) {
            if (value[i] == '\\' && value[i+1] == '\\') { value.erase(i, 1); i++; }
            else if (value[i] == '\\' && value[i+1] == '"')  { value.erase(i, 1); i++; }
            else if (value[i] == '\\' && value[i+1] == '/')  { value.erase(i, 1); i++; }
            else i++;
        }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, NULL, 0);
        if (wlen <= 1) { pos = valEnd + 1; continue; }
        std::wstring wkey(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, &wkey[0], wlen);

        wlen = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, NULL, 0);
        if (wlen <= 1) { pos = valEnd + 1; continue; }
        std::wstring wval(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &wval[0], wlen);

        entries.push_back({wkey, wval});
        pos = valEnd + 1;
    }
    return true;
}

bool WriteJson(const wchar_t* path, const std::vector<ExeEntry>& entries) {
    // Write to temp file first, then rename (atomic write)
    std::wstring tmpPath = std::wstring(path) + L".tmp";
    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    std::string out = "{\n";
    for (size_t i = 0; i < entries.size(); i++) {
        std::string name = WtoU8(entries[i].name.c_str());
        std::string pathStr = WtoU8(entries[i].path.c_str());
        for (size_t j = 0; j < pathStr.size(); j++) {
            if (pathStr[j] == '\\') { pathStr.insert(j, "\\"); j++; }
        }
        out += "  \"" + name + "\": \"" + pathStr + "\"";
        if (i < entries.size() - 1) out += ",";
        out += "\n";
    }
    out += "}\n";

    DWORD written;
    if (!WriteFile(hFile, out.c_str(), (DWORD)out.size(), &written, NULL)) {
        CloseHandle(hFile);
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    CloseHandle(hFile);

    // Atomic rename
    DeleteFileW(path);
    if (!MoveFileW(tmpPath.c_str(), path)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

bool WriteCpp(const wchar_t* path, const std::vector<ExeEntry>& entries) {
    std::wstring tmpPath = std::wstring(path) + L".tmp";
    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    std::string cpp = "#include \"drun_data.h\"\n\nconst ExeEntry g_exeMap[] = {\n";
    for (size_t i = 0; i < entries.size(); i++) {
        std::string name = WtoU8(entries[i].name.c_str());
        std::string pathStr = WtoU8(entries[i].path.c_str());
        for (size_t j = 0; j < pathStr.size(); j++) {
            if (pathStr[j] == '\\') { pathStr.insert(j, "\\"); j++; }
        }
        cpp += "    {L\"" + name + "\", L\"" + pathStr + "\"},\n";
    }
    cpp += "};\n\nconst int g_exeCount = " + std::to_string(entries.size()) + ";\n";

    DWORD written;
    if (!WriteFile(hFile, cpp.c_str(), (DWORD)cpp.size(), &written, NULL)) {
        CloseHandle(hFile);
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    CloseHandle(hFile);
    DeleteFileW(path);
    MoveFileW(tmpPath.c_str(), path);
    return true;
}

// === Compile with log capture (Issue #3: compile logging) ===
bool RecompileDrun() {
    EnsureConfig();

    // Build g++ command
    WCHAR cmdLine[2048];
    WCHAR cppMain[MAX_PATH];
    swprintf_s(cppMain, L"%s\\drun_main.cpp", g_launcherDir);
    swprintf_s(cmdLine, L"\"%s\" -static -municode \"%s\" \"%s\" -o \"%s\" -lshell32 -lole32 -lshlwapi -lversion -O2 -s",
        g_gppPath, cppMain, g_cppPath, g_drunExe);

    // Set up pipe for stderr/stdout capture
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    PROCESS_INFORMATION pi = { 0 };
    BOOL ok = CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, g_launcherDir, &si, &pi);

    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    // Read compiler output
    std::string log;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        log += buf;
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Always write compile log
    HANDLE hLog = CreateFileW(g_logPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog != INVALID_HANDLE_VALUE) {
        time_t now = time(NULL);
        std::string ts = ctime(&now);
        std::string header = "=== Drun Compile Log [" + ts.substr(0, ts.size()-1) + "] ===\n";
        header += "Command: " + WtoU8(cmdLine) + "\n\n";
        WriteFile(hLog, header.c_str(), (DWORD)header.size(), &bytesRead, NULL);
        if (!log.empty()) WriteFile(hLog, log.c_str(), (DWORD)log.size(), &bytesRead, NULL);
        std::string footer = "\n=== Exit code: " + std::to_string(exitCode) + " ===\n";
        WriteFile(hLog, footer.c_str(), (DWORD)footer.size(), &bytesRead, NULL);
        CloseHandle(hLog);
    }

    return exitCode == 0;
}

// === Progress bar (pip-style) ===
void ShowProgress(int step, int total, const char* msg) {
    printf("\r  [");
    int width = 30;
    int filled = (step * width) / total;
    for (int i = 0; i < width; i++) printf(i < filled ? "\u2588" : "\u2591");
    printf("] %d/%d  %s", step, total, msg);
    fflush(stdout);
}

// === Remove mode ===
int RemoveMode(int argc, wchar_t* argv[]) {
    EnsureConfig();

    std::vector<ExeEntry> entries;
    if (!ReadJson(g_jsonPath, entries)) {
        printf("\u65e0\u6cd5\u8bfb\u53d6 %s\n", WtoU8(g_jsonPath).c_str());
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }
    if (entries.empty()) {
        printf("\u6ca1\u6709\u5df2\u6ce8\u518c\u7684\u7a0b\u5e8f\u3002\n");
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 0;
    }

    std::wstring target;
    if (argc >= 3) target = argv[2];
    if (target.empty()) {
        printf("\n=== \u5df2\u6ce8\u518c\u7a0b\u5e8f ===\n\n");
        for (size_t i = 0; i < entries.size(); i++) {
            printf("  [%2d] %-20s -> %s\n", (int)i,
                WtoU8(entries[i].name.c_str()).c_str(),
                WtoU8(entries[i].path.c_str()).c_str());
        }
        printf("\n\u8f93\u5165\u5e8f\u53f7\u6216\u540d\u79f0\u5220\u9664: ");
        char in[256]; fgets(in, 256, stdin);
        int idx = atoi(in);
        if (idx > 0 && idx <= (int)entries.size()) {
            target = entries[idx - 1].name;
        } else {
            std::string s(in); s.erase(s.find_last_not_of("\r\n") + 1);
            target = std::wstring(s.begin(), s.end());
        }
    }

    bool found = false;
    for (size_t i = 0; i < entries.size(); i++) {
        if (_wcsicmp(entries[i].name.c_str(), target.c_str()) == 0) {
            printf("\u5220\u9664: %s -> %s\n", WtoU8(target.c_str()).c_str(),
                WtoU8(entries[i].path.c_str()).c_str());
            entries.erase(entries.begin() + i);
            found = true;
            break;
        }
    }
    if (!found) {
        printf("\u627e\u4e0d\u5230: %s\n", WtoU8(target.c_str()).c_str());
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }

    ShowProgress(0, 3, "\u6b63\u5728\u5f00\u59cb...");
    if (!WriteJson(g_jsonPath, entries)) {
        printf("\n\u5199\u5165 JSON \u5931\u8d25\u3002\n");
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }
    ShowProgress(1, 3, "JSON \u5df2\u66f4\u65b0"); Sleep(100);

    if (!WriteCpp(g_cppPath, entries)) {
        printf("\n\u5199\u5165 CPP \u5931\u8d25\u3002\n");
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }
    ShowProgress(2, 3, "CPP \u5df2\u751f\u6210"); Sleep(100);
    ShowProgress(2, 3, "\u6b63\u5728\u7f16\u8bd1 drun.exe ...");

    if (RecompileDrun()) {
        ShowProgress(3, 3, "drun.exe \u5df2\u91cd\u7f16"); printf("\n");
    } else {
        printf("\n\u8b66\u544a: drun.exe \u91cd\u65b0\u7f16\u8bd1\u5931\u8d25\u3002\n");
        printf("\u65e5\u5fd7: %s\n", WtoU8(g_logPath).c_str());
    }

    printf("\n\u5df2\u5220\u9664: %s\n", WtoU8(target.c_str()).c_str());
    printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
    return 0;
}

// === Main ===
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    EnsureConfig();
    CheckErrorLog();

    if (argc < 2) {
        printf("\nDrun-Plus - \u7ba1\u7406 drun \u7a0b\u5e8f\u5217\u8868\n\n");
        printf("\u7528\u6cd5:\n");
        printf("  drun-plus <exe\u8def\u5f84> --name <\u540d\u79f0>   \u6dfb\u52a0\u7a0b\u5e8f\n");
        printf("  drun-plus --remove [\u540d\u79f0]              \u5220\u9664\u7a0b\u5e8f\n");
        printf("  drun-plus --list                           \u5217\u51fa\u6240\u6709\u7a0b\u5e8f\n\n");
        wprintf(L"Install: %s\n", g_launcherDir);
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 0;
    }

    // Remove mode
    if (wcscmp(argv[1], L"--remove") == 0 || wcscmp(argv[1], L"-r") == 0) {
        return RemoveMode(argc, argv);
    }

    // List mode
    if (wcscmp(argv[1], L"--list") == 0 || wcscmp(argv[1], L"-l") == 0) {
        std::vector<ExeEntry> entries;
        if (!ReadJson(g_jsonPath, entries)) {
            printf("\u65e0\u6cd5\u8bfb\u53d6 %s\n", WtoU8(g_jsonPath).c_str());
            printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
            return 1;
        }
        printf("\n=== \u5df2\u6ce8\u518c\u7a0b\u5e8f (%d) ===\n\n", (int)entries.size());
        for (size_t i = 0; i < entries.size(); i++) {
            printf("  %-20s -> %s\n",
                WtoU8(entries[i].name.c_str()).c_str(),
                WtoU8(entries[i].path.c_str()).c_str());
        }
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 0;
    }

    // Add mode
    std::wstring exePath = argv[1];
    std::wstring customName;

    for (int i = 2; i < argc; i++) {
        if ((wcscmp(argv[i], L"--name") == 0 || wcscmp(argv[i], L"-n") == 0) && i + 1 < argc) {
            customName = argv[++i];
        }
    }

    if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("\u6587\u4ef6\u4e0d\u5b58\u5728: %s\n", WtoU8(exePath.c_str()).c_str());
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }

    size_t len = exePath.size();
    if (len < 5 || _wcsicmp(exePath.c_str() + len - 4, L".exe") != 0) {
        printf("\u975e .exe \u6587\u4ef6: %s\n", WtoU8(exePath.c_str()).c_str());
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }

    WCHAR fullPath[MAX_PATH];
    if (GetFullPathNameW(exePath.c_str(), MAX_PATH, fullPath, NULL) > 0) {
        exePath = fullPath;
    }

    std::wstring name;
    if (!customName.empty()) {
        name = Sanitize(customName);
    } else {
        size_t slash = exePath.find_last_of(L"\\/");
        std::wstring filename = (slash != std::wstring::npos) ? exePath.substr(slash + 1) : exePath;
        size_t dot = filename.rfind(L'.');
        if (dot != std::wstring::npos) filename = filename.substr(0, dot);
        name = Sanitize(filename);

        std::wstring parentFolder;
        if (slash != std::wstring::npos) {
            size_t prevSlash = exePath.find_last_of(L"\\/", slash - 1);
            parentFolder = (prevSlash != std::wstring::npos) ?
                exePath.substr(prevSlash + 1, slash - prevSlash - 1) :
                exePath.substr(0, slash);
            parentFolder = Sanitize(parentFolder);
        }

        std::vector<ExeEntry> existing;
        if (ReadJson(g_jsonPath, existing)) {
            bool dup = false;
            for (const auto& e : existing) {
                if (_wcsicmp(e.name.c_str(), name.c_str()) == 0) { dup = true; break; }
            }
            if (dup && !parentFolder.empty()) {
                name = parentFolder + L"-" + name;
            }
        }
    }

    {
        std::vector<ExeEntry> existing;
        if (ReadJson(g_jsonPath, existing)) {
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

    std::vector<ExeEntry> entries;
    ReadJson(g_jsonPath, entries);

    for (const auto& e : entries) {
        if (_wcsicmp(e.path.c_str(), exePath.c_str()) == 0) {
            printf("\u8be5\u7a0b\u5e8f\u5df2\u6ce8\u518c\u4e3a: %s\n", WtoU8(e.name.c_str()).c_str());
            printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
            return 0;
        }
    }

    entries.push_back({name, exePath});

    printf("\u6dfb\u52a0: %s\n  -> %s\n\n", WtoU8(name.c_str()).c_str(), WtoU8(exePath.c_str()).c_str());

    ShowProgress(0, 3, "\u6b63\u5728\u5f00\u59cb...");
    if (!WriteJson(g_jsonPath, entries)) {
        printf("\n\u5199\u5165 JSON \u5931\u8d25\u3002\n");
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }
    ShowProgress(1, 3, "JSON \u5df2\u66f4\u65b0"); Sleep(100);

    if (!WriteCpp(g_cppPath, entries)) {
        printf("\n\u5199\u5165 CPP \u6570\u636e\u6587\u4ef6\u5931\u8d25\u3002\n");
        printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
        return 1;
    }
    ShowProgress(2, 3, "CPP \u5df2\u751f\u6210"); Sleep(100);
    ShowProgress(2, 3, "\u6b63\u5728\u7f16\u8bd1 drun.exe ...");

    if (RecompileDrun()) {
        ShowProgress(3, 3, "drun.exe \u5df2\u91cd\u7f16"); printf("\n");
    } else {
        printf("\n\u8b66\u544a: drun.exe \u7f16\u8bd1\u5931\u8d25\uff0c\u8bf7\u67e5\u770b\u65e5\u5fd7\u3002\n");
        printf("\u65e5\u5fd7\u6587\u4ef6: %s\n", WtoU8(g_logPath).c_str());
        printf("\u624b\u52a8\u547d\u4ee4: g++ -o drun.exe drun_main.cpp drun_data.cpp -static -municode -O2 -s\n");
    }

    printf("\n\u73b0\u5728\u53ef\u4ee5\u7528 drun %s \u542f\u52a8!\n", WtoU8(name.c_str()).c_str());
    printf("\n\u6309 Enter \u952e\u9000\u51fa..."); getchar();
    return 0;
}

