#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ExeEntry {
    const wchar_t* name;
    const wchar_t* path;
};

extern const ExeEntry g_exeMap[];
extern const int g_exeCount;

const wchar_t* FindExe(const wchar_t* name);
std::vector<const ExeEntry*> FuzzyFind(const wchar_t* partial);
