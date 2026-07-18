#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

std::string g_langCode = "en";
std::wstring g_langIniPath;
std::wstring g_installPath;
wchar_t g_selfPath[MAX_PATH];

std::string WtoU8(const wchar_t* w) {
    if (!w||!*w) return "";
    int l=WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL);
    if(l<=1)return "";
    std::string r(l-1,'\0');
    WideCharToMultiByte(CP_UTF8,0,w,-1,&r[0],l,NULL,NULL);
    return r;
}

std::string T(const char* key) {
    WCHAR buf[4096];
    std::wstring sec(g_langCode.begin(), g_langCode.end());
    GetPrivateProfileStringW(sec.c_str(),
        std::wstring(key, key+strlen(key)).c_str(),
        L"", buf, 4096, g_langIniPath.c_str());
    if (buf[0]) return WtoU8(buf);
    GetPrivateProfileStringW(L"en",
        std::wstring(key, key+strlen(key)).c_str(),
        L"", buf, 4096, g_langIniPath.c_str());
    return buf[0] ? WtoU8(buf) : key;
}

std::wstring FindInstallDir() {
    WCHAR localAppData[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
        std::wstring p = std::wstring(localAppData) + L"\\drun-launcher";
        if (GetFileAttributesW((p + L"\\config.ini").c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;
    }
    const wchar_t* knownDirs[] = {
    };
    // Check exe's own directory
    WCHAR selfPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, selfPath, MAX_PATH)) {
        WCHAR* slash = wcsrchr(selfPath, L'\\');
        if (slash) *slash = 0;
        std::wstring p = std::wstring(selfPath) + L"\\config.ini";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            return selfPath;
    }
    WCHAR pathBuf[32767];
    DWORD len = GetEnvironmentVariableW(L"Path", pathBuf, 32767);
    if (len > 0) {
        std::wstring ps(pathBuf, len);
        size_t pos = 0;
        while (pos < ps.size()) {
            size_t semi = ps.find(L';', pos);
            if (semi == std::wstring::npos) semi = ps.size();
            std::wstring dir = ps.substr(pos, semi - pos);
            if (GetFileAttributesW((dir + L"\\config.ini").c_str()) != INVALID_FILE_ATTRIBUTES)
                return dir;
            pos = semi + 1;
        }
    }
    return L"";
}

bool RemoveFromPath(const wchar_t* dir) {
    WCHAR b[32767]; DWORD l=GetEnvironmentVariableW(L"Path",b,32767);
    if(l==0) return false;
    std::wstring ps(b,l), ds(dir);
    if(!ds.empty() && ds.back()!=L'\\') ds+=L'\\';
    size_t pos = ps.find(ds);
    if(pos == std::wstring::npos) return false;
    size_t end = ps.find(L';', pos);
    if(end == std::wstring::npos) end = ps.size();
    if(pos > 0 && ps[pos-1]==L';') pos--;
    else if(end < ps.size() && ps[end]==L';') end++;
    ps.erase(pos, end - pos);
    HKEY hk;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Environment",0,KEY_SET_VALUE,&hk)!=ERROR_SUCCESS) return false;
    RegSetValueExW(hk,L"Path",0,REG_EXPAND_SZ,(BYTE*)ps.c_str(),(DWORD)((ps.size()+1)*sizeof(WCHAR)));
    RegCloseKey(hk);
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"Environment",SMTO_ABORTIFHUNG,5000,NULL);
    return true;
}

bool CleanPSProfile(const wchar_t* dir) {
    WCHAR docs[MAX_PATH];
    if(SHGetFolderPathW(NULL,CSIDL_PERSONAL,NULL,0,docs)!=S_OK) return false;
    std::wstring p=std::wstring(docs)+L"\\WindowsPowerShell\\profile.ps1";
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE) return false;
    DWORD s=GetFileSize(h,NULL);
    if(s==0||s>65536){CloseHandle(h);return false;}
    std::vector<char> buf(s+1); DWORD r;
    ReadFile(h,&buf[0],s,&r,NULL); buf[r]=0; CloseHandle(h);
    std::string content(buf.data());
    std::string target1 = "function drun { & \"" + WtoU8(dir) + "\\drun.exe\" @args }";
    std::string target2 = "function drun-plus { & \"" + WtoU8(dir) + "\\drun-plus.exe\" @args }";
    bool modified = false;
    size_t pos;
    while((pos=content.find(target1))!=std::string::npos){content.erase(pos,target1.size()+2);modified=true;}
    while((pos=content.find(target2))!=std::string::npos){content.erase(pos,target2.size()+2);modified=true;}
    if(!modified) return false;
    h=CreateFileW(p.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE) return false;
    DWORD w; WriteFile(h,content.c_str(),(DWORD)content.size(),&w,NULL); CloseHandle(h);
    return true;
}

void DeleteAllExceptSelf(const wchar_t* dir) {
    std::wstring search = std::wstring(dir) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".")==0 || wcscmp(fd.cFileName, L"..")==0) continue;
        std::wstring fp = std::wstring(dir) + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteAllExceptSelf(fp.c_str());
            RemoveDirectoryW(fp.c_str());
        } else {
            // Skip the uninstaller itself
            if (_wcsicmp(fp.c_str(), g_selfPath) == 0) continue;
            SetFileAttributesW(fp.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(fp.c_str());
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

bool DeleteSelf() {
    // Schedule self-deletion on next reboot
    return MoveFileExW(g_selfPath, NULL, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
}

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Drun Uninstaller");
    
    GetModuleFileNameW(NULL, g_selfPath, MAX_PATH);
    
    printf("\n  Drun Uninstaller\n  ================\n\n");
    
    g_installPath = FindInstallDir();
    if (g_installPath.empty()) {
        printf("  Drun not found on this system.\n");
        printf("\n  Press Enter to exit..."); getchar();
        return 1;
    }
    
    // Load lang.ini: D:\ first, then install dir
    g_langIniPath = L"D:\\lang.ini";
    if (GetFileAttributesW(g_langIniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        g_langIniPath = g_installPath + L"\\lang.ini";
    
    // Load language from config
    if (GetFileAttributesW((g_installPath + L"\\config.ini").c_str()) != INVALID_FILE_ATTRIBUTES) {
        WCHAR buf[32];
        GetPrivateProfileStringW(L"install", L"lang", L"en", buf, 32, (g_installPath + L"\\config.ini").c_str());
        g_langCode = WtoU8(buf);
    }
    
    printf("  %s\n\n", T("uninstall_warning").c_str());
    printf("  %s: %s\n", T("install_path_label").c_str(), WtoU8(g_installPath.c_str()).c_str());
    printf("\n  %s (y/N): ", T("uninstall_confirm").c_str());
    
    char in[16]; fgets(in, 16, stdin);
    if (in[0] != 'y' && in[0] != 'Y') {
        printf("  %s\n", T("uninstall_cancelled").c_str());
        printf("\n  %s", T("press_enter").c_str()); getchar();
        return 0;
    }
    
    printf("\n  %s...\n", T("uninstalling").c_str());
    
    // Delete all files in install dir except uninstaller
    DeleteAllExceptSelf(g_installPath.c_str());
    
    // Remove from PATH
    RemoveFromPath(g_installPath.c_str());
    
    // Clean PowerShell profile
    CleanPSProfile(g_installPath.c_str());
    
    // Try to remove the install dir
    RemoveDirectoryW(g_installPath.c_str());
    
    // Remove D:\lang.ini
    DeleteFileW(L"D:\\lang.ini");
    
    printf("  %s\n", T("uninstall_done").c_str());
    printf("\n  %s", T("press_enter").c_str()); getchar();
    
    // Schedule self-deletion
    DeleteSelf();
    
    return 0;
}
