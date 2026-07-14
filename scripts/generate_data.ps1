# generate_data.ps1 - Generate drun_data.cpp from exe-map.json
param(
    [string]$JsonPath = "..\exe-map.json",
    [string]$OutputPath = "..\drun_data.cpp"
)

$json = Get-Content $JsonPath -Raw -Encoding UTF8 | ConvertFrom-Json

$cpp = '#include "drun_data.h"' + "`r`n"
$cpp += '#include <cwchar>' + "`r`n"
$cpp += '#include <algorithm>' + "`r`n`r`n"
$cpp += 'const ExeEntry g_exeMap[] = {' + "`r`n"

foreach ($name in $json.PSObject.Properties.Name) {
    $path = $json.$name
    $n = $name -replace '\\', '\\' -replace '"', '\"'
    $p = $path -replace '\\', '\\' -replace '"', '\"'
    $cpp += "    { L`"$n`", L`"$p`" },`r`n"
}

$cpp += @"
};
const int g_exeCount = sizeof(g_exeMap) / sizeof(g_exeMap[0]);
const wchar_t* FindExe(const wchar_t* name) {
    for (int i = 0; i < g_exeCount; i++) {
        if (_wcsicmp(g_exeMap[i].name, name) == 0) return g_exeMap[i].path;
    }
    return nullptr;
}
std::vector<const ExeEntry*> FuzzyFind(const wchar_t* partial) {
    std::vector<const ExeEntry*> results;
    std::wstring plower(partial);
    std::transform(plower.begin(), plower.end(), plower.begin(), ::towlower);
    for (int i = 0; i < g_exeCount; i++) {
        std::wstring name(g_exeMap[i].name);
        std::wstring nlower = name;
        std::transform(nlower.begin(), nlower.end(), nlower.begin(), ::towlower);
        if (nlower.find(plower) != std::wstring::npos) results.push_back(&g_exeMap[i]);
    }
    return results;
}
"@

$cpp | Out-File $OutputPath -Encoding UTF8
Write-Host "Generated $OutputPath with $($json.PSObject.Properties.Count) entries"
