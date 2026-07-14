param(
    [string]$Name
)

$mapPath = "D:\desktop\系统工具\npm-launcher\exe-map.json"
$map = Get-Content $mapPath -Encoding UTF8 | ConvertFrom-Json

if (-not $Name) {
    Write-Host "`n可用命令 (共 $($map.PSObject.Properties.Count) 个):`n" -ForegroundColor Cyan
    $map.PSObject.Properties | Sort-Object Name | ForEach-Object {
        Write-Host "  $($_.Name)" -ForegroundColor Yellow
        Write-Host "    -> $($_.Value)" -ForegroundColor DarkGray
    }
    return
}

$path = $map.$Name
if (-not $path) {
    # Fuzzy search
    $matches = $map.PSObject.Properties | Where-Object { $_.Name -like "*$Name*" }
    if ($matches.Count -gt 0) {
        Write-Host "`n未找到 '$Name'，你是否要找:" -ForegroundColor Yellow
        $matches | ForEach-Object { Write-Host "  $($_.Name)" -ForegroundColor Cyan }
    } else {
        Write-Host "未找到: $Name" -ForegroundColor Red
    }
    return
}

Write-Host "正在启动: $Name" -ForegroundColor Green
Start-Process -FilePath $path
