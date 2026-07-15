#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <io.h>
#include <fcntl.h>
#include <cstdlib>
#include "lang_codes.h"

std::string g_langCode = "en";
wchar_t g_iniPath[MAX_PATH];

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
    std::wstring section(g_langCode.begin(), g_langCode.end());
    GetPrivateProfileStringW(section.c_str(), 
        std::wstring(key, key+strlen(key)).c_str(),
        L"", buf, 4096, g_iniPath);
    if (buf[0]) return WtoU8(buf);
    // Fallback EN
    GetPrivateProfileStringW(L"en",
        std::wstring(key, key+strlen(key)).c_str(),
        L"", buf, 4096, g_iniPath);
    return buf[0] ? WtoU8(buf) : key;
}

bool AddToPath(const wchar_t* dir) {
    WCHAR b[32767]; DWORD l=GetEnvironmentVariableW(L"Path",b,32767);
    if(l==0&&GetLastError()==ERROR_ENVVAR_NOT_FOUND){b[0]=L'\0';l=0;}
    std::wstring ps(b,l), ds(dir);
    if(ps.find(ds)!=std::wstring::npos) return false;
    if(!ps.empty()&&ps.back()!=L';')ps+=L';';ps+=ds;
    HKEY hk;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Environment",0,KEY_SET_VALUE,&hk)!=ERROR_SUCCESS) return false;
    RegSetValueExW(hk,L"Path",0,REG_EXPAND_SZ,(BYTE*)ps.c_str(),(DWORD)((ps.size()+1)*sizeof(WCHAR)));
    RegCloseKey(hk);
    SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"Environment",SMTO_ABORTIFHUNG,5000,NULL);
    return true;
}

bool CreatePSProfile(const wchar_t* dir) {
    WCHAR docs[MAX_PATH];
    if(SHGetFolderPathW(NULL,CSIDL_PERSONAL,NULL,0,docs)!=S_OK) return false;
    std::wstring p=std::wstring(docs)+L"\\WindowsPowerShell"; CreateDirectoryW(p.c_str(),NULL); p+=L"\\profile.ps1";
    std::string ex;
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h!=INVALID_HANDLE_VALUE){DWORD s=GetFileSize(h,NULL);if(s>0&&s<65536){std::vector<char> b(s+1);DWORD r;ReadFile(h,&b[0],s,&r,NULL);b[r]=0;ex=b.data();if(!ex.empty()&&ex.back()!='\n')ex+="\r\n";}CloseHandle(h);}
    if(ex.find("function drun")!=std::string::npos) return false;
    ex+="function drun { & \""+WtoU8(dir)+"\\drun.exe\" @args }\r\n";
    ex+="function drun-plus { & \""+WtoU8(dir)+"\\drun-plus.exe\" @args }\r\n";
    h=CreateFileW(p.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE) return false;
    DWORD w;WriteFile(h,ex.c_str(),(DWORD)ex.size(),&w,NULL);CloseHandle(h);
    return true;
}

bool CopyBundled(const wchar_t* name, const wchar_t* destDir) {
    WCHAR ed[MAX_PATH];GetModuleFileNameW(NULL,ed,MAX_PATH);
    WCHAR* ls=wcsrchr(ed,L'\\');if(ls)*ls=L'\0';
    return CopyFileW((std::wstring(ed)+L"\\"+name).c_str(),(std::wstring(destDir)+L"\\"+name).c_str(),FALSE);
}

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Drun Launcher Setup");
    
    // Find lang.ini
    WCHAR ed[MAX_PATH];GetModuleFileNameW(NULL,ed,MAX_PATH);
    WCHAR* ls=wcsrchr(ed,L'\\');if(ls)*ls=L'\0';
    swprintf_s(g_iniPath, L"%s\\lang.ini", ed);
    
    // Load language names from INI
    struct LI { std::string code; std::string name; };
    std::vector<LI> langList;
    for(int i=0;i<LANG_COUNT;i++){
        WCHAR buf[256];
        std::wstring sec(g_langCodes[i]);
        GetPrivateProfileStringW(sec.c_str(),L"name",L"",buf,256,g_iniPath);
        if(buf[0]) langList.push_back({WtoU8(g_langCodes[i]), WtoU8(buf)});
    }
    
    // Language selection
    printf("\n  Drun Launcher Setup v2.0\n  ===========================\n\n");
    printf("  Select Language / 选择语言:\n\n");
    for(size_t i=0;i<langList.size();i++){
        printf("  [%3d] %s\n", (int)i+1, langList[i].name.c_str());
    }
    printf("\n  > ");
    
    char in[16]; fgets(in,16,stdin); int c=atoi(in);
    if(c>0&&c<=(int)langList.size()) g_langCode=langList[c-1].code;
    printf("\n");
    
    // Welcome
    printf("%s\n\n",T("welcome").c_str());
    printf("%s\n%s\n\n",T("description").c_str(),T("usage_hint").c_str());
    
    // Install dir
    WCHAR defDir[MAX_PATH];
    if(SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,0,defDir)!=S_OK) wcscpy_s(defDir,L"C:\\drun");
    std::wstring instPath=std::wstring(defDir)+L"\\drun-launcher";
    printf("%s [%s]: ",T("install_dir").c_str(),WtoU8(instPath.c_str()).c_str());
    WCHAR ib[MAX_PATH]={0};fgetws(ib,MAX_PATH,stdin);
    for(int i=0;ib[i];i++)if(ib[i]==L'\n'||ib[i]==L'\r'){ib[i]=L'\0';break;}
    if(ib[0])instPath=ib;
    for(size_t i=3;i<instPath.size();i++)if(instPath[i]==L'\\')CreateDirectoryW(instPath.substr(0,i).c_str(),NULL);
    CreateDirectoryW(instPath.c_str(),NULL);
    
    // Install files
    printf("\n%s\n\n",T("installing").c_str());
    const wchar_t* files[]={L"drun.exe",L"drun-plus.exe",L"drun-path.exe"};
    bool allOk=true;
    for(int i=0;i<3;i++){printf("  [%d/3] %s %s",i+1,T("step_copy").c_str(),WtoU8(files[i]).c_str());
        if(CopyBundled(files[i],instPath.c_str()))printf(" - %s\n",T("ok").c_str());
        else{printf(" - %s\n",T("failed").c_str());allOk=false;}}
    std::string ctr=T("contribute"); if(!ctr.empty()&&ctr!="contribute") printf("  %s\n\n",ctr.c_str());
    if(!allOk){printf("\n%s\n%s\n",T("error_missing").c_str(),T("file_not_found").c_str());printf("\n%s",T("press_enter").c_str());getchar();return 1;}
    
    HANDLE hj=CreateFileW((instPath+L"\\exe-map.json").c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hj!=INVALID_HANDLE_VALUE){const char* ej="{\n}";DWORD w;WriteFile(hj,ej,3,&w,NULL);CloseHandle(hj);}
    
    printf("\n%s ",T("add_path").c_str());
    printf("%s\n",AddToPath(instPath.c_str())?T("ok").c_str():T("already_path").c_str());
    printf("%s ",T("setup_ps").c_str());
    printf("%s\n",CreatePSProfile(instPath.c_str())?T("ok").c_str():T("ps_skipped").c_str());
    
    printf("\n  %s\n\n",T("complete_title").c_str());
    printf("  %s\n\n",T("complete_try").c_str());
    printf("    drun                    %s\n",T("list_all").c_str());
    printf("    drun-plus <exe> --name  %s\n",T("add_program").c_str());
    printf("\n  %s %s\n",T("install_path_label").c_str(),WtoU8(instPath.c_str()).c_str());
    printf("\n  %s",T("press_enter").c_str());getchar();
    return 0;
}
