#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

try {
    foreach ($entry in @('tests/main.cpp', 'tests/ui_check/main.cpp', 'tests/mw_check/main.cpp')) {
        $lines = [IO.File]::ReadAllLines((Join-Path $repo $entry))
        if ($lines.Count -gt 4000) { throw "$entry exceeds 4000 lines; move another responsibility into a module." }
        if ($lines -match '^\s*#include\s+".*\.cpp"') { throw "$entry includes implementation files instead of linking modules." }
    }
    $owners = Get-Content -LiteralPath (Join-Path $repo 'tests/core_modules/owners.json') -Raw | ConvertFrom-Json -AsHashtable
    foreach ($folder in @('tests/core_modules', 'tests/ui_check/modules', 'tests/mw_check/modules')) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo $folder) -File) {
            if ($file.Extension -notin @('.cpp', '.hpp')) { continue }
            if ([IO.File]::ReadAllLines($file.FullName).Count -gt 5000) {
                throw "$($file.Name) exceeds 5000 lines; split its sub-responsibilities."
            }
            if ($folder -eq 'tests/core_modules' -and $file.Extension -eq '.cpp' -and
                $file.Name -ne 'Support.cpp' -and $file.Name -notlike '*_main.cpp') {
                if (-not $owners.ContainsKey($file.Name)) { throw "Core assertion file has no domain owner: $($file.Name)" }
            }
        }
    }
    foreach ($name in $owners.Keys) {
        if (-not (Test-Path -LiteralPath (Join-Path $repo "tests/core_modules/$name"))) { throw "Stale core test owner: $name" }
        $domain = $owners[$name]
        if (-not (Test-Path -LiteralPath (Join-Path $repo "tests/core_modules/${domain}_main.cpp"))) { throw "Unknown core domain: $domain" }
    }

    # ---- 设置沙盒：tests/ 下每个带 main() 的进程都必须把 QSettings 搬进自己的临时目录 ----
    # 只写 `setPath()` 不够：Windows 上默认格式是 NativeFormat，`setPath` 管不到它 ——
    # `QSettings()` 照样打到 HKCU\Software\WTFML\WTFML，读出来是这台机器用户在启动器里挑的
    # 主题（断言跟着人走），写下去就是改用户的真设置。所以这四条一起要：默认格式、UserScope
    # 路径、运行时守卫（tests/support/SettingsIsolation.hpp）和它的 include。
    $isolationRules = @(
        @{Pattern = 'QSettings::setDefaultFormat\(\s*QSettings::IniFormat\s*\)'; What = 'setDefaultFormat(IniFormat)'},
        @{Pattern = 'QSettings::setPath\(\s*QSettings::IniFormat\s*,\s*QSettings::UserScope'; What = 'setPath(IniFormat, UserScope)'},
        @{Pattern = 'requireIsolatedSettings\('; What = 'requireIsolatedSettings()（运行时守卫）'},
        @{Pattern = 'support/SettingsIsolation\.hpp'; What = '#include "support/SettingsIsolation.hpp"'}
    )
    $entries = @(Get-ChildItem -LiteralPath (Join-Path $repo 'tests') -Recurse -File -Filter '*.cpp' |
        Where-Object { [IO.File]::ReadAllText($_.FullName) -match '(?m)^\s*int\s+main\s*\(' })
    if ($entries.Count -eq 0) { throw 'tests/ 下没扫到任何 main() 入口，规则失配' }
    foreach ($entry in $entries) {
        $source = [IO.File]::ReadAllText($entry.FullName)
        $name = [IO.Path]::GetRelativePath($repo, $entry.FullName) -replace '\\', '/'
        foreach ($rule in $isolationRules) {
            if ($source -notmatch $rule.Pattern) { throw "$name 没有把设置搬进沙盒：缺 $($rule.What)" }
        }
    }
    # 系统设置那一路 + 默认数据根：入口和 core 域入口多要两样（模块进程另有自己的沙盒）。
    foreach ($entry in @('tests/main.cpp', 'tests/ui_check/main.cpp', 'tests/mw_check/main.cpp')) {
        $source = [IO.File]::ReadAllText((Join-Path $repo $entry))
        if (-not $source.Contains('QSettings::SystemScope') -or -not $source.Contains('wtfml.testDefaultMinecraftDir')) {
            throw "$entry lacks system/default-root isolation"
        }
    }
    # A private user INI alone still falls back to machine settings; default ledgers
    # also need their own game root before production objects are constructed.
    foreach ($entry in Get-ChildItem -LiteralPath (Join-Path $repo 'tests/core_modules') -Filter '*_main.cpp') {
        $source = [IO.File]::ReadAllText($entry.FullName)
        foreach ($required in @('QSettings::SystemScope', 'wtfml.testDefaultMinecraftDir', 'QTemporaryDir fixture')) {
            if (-not $source.Contains($required)) { throw "$($entry.Name) lacks process isolation: $required" }
        }
    }

    # ---- 界面语言：测试一律钉 zh_CN ----
    # CI 的 Windows 是英文区域；产品默认跟随系统，不钉的话英文区域那一趟会装英文翻译，
    # 几千条中文断言一起变红。钉的位置只有一处（守卫里），所以每个入口用守卫就自动钉住。
    $isolationHeader = [IO.File]::ReadAllText((Join-Path $repo 'tests/support/SettingsIsolation.hpp'))
    if (-not $isolationHeader.Contains('General/Language')) {
        throw 'tests/support/SettingsIsolation.hpp 没有把界面语言钉成 zh_CN（见 docs/dev/I18N.md）'
    }

    # ---- tools/：起真 exe 的脚本要么带 --acceptance-profile，要么写明为什么必须动真设置 ----
    # 2026-11-13 那次真 HKCU 被写进 BackdropTint=0 / BackdropBlur=0，就是自动化在**真**设置上
    # 点到了滑块。新加一个起 exe 的脚本忘了隔离，这里直接红，不靠谁记得。
    $startPattern = 'Start-Process[^\r\n]*\$[Ee]xe|&\s*\$[Ee]xe\b|Start-Process[^\r\n]*WTFMinecraftLauncher'
    $optOut = '# settings-isolation: real-settings'
    foreach ($script in Get-ChildItem -LiteralPath (Join-Path $repo 'tools') -File -Filter '*.ps1') {
        if ($script.Name -eq 'check-test-modules.ps1') { continue }  # 本脚本只是把规则写在自己身上
        $text = [IO.File]::ReadAllText($script.FullName)
        # 只认**产品那个 exe**：perf-download / player_stress 起的是自己的探针，探针自己有沙盒。
        if (-not $text.Contains('WTFMinecraftLauncher')) { continue }
        if ($text -notmatch $startPattern) { continue }
        if ($text.Contains('--acceptance-profile')) { continue }
        $reason = @($text -split "`r?`n" | Where-Object { $_.Contains($optOut) })
        if ($reason.Count -eq 0 -or $reason[0].Trim().Length -le $optOut.Length + 12) {
            throw "tools/$($script.Name) 起真 exe 却没隔离：加 --acceptance-profile，或补一行「$optOut（为什么必须动真设置）」"
        }
    }
    Write-Host 'PASS test entry/module size limits, core build ownership and settings isolation'
} catch {
    Write-Host "FAIL $_"
    exit 1
}
