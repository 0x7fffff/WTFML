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

    # ---- 分区标签覆盖：所有带 ci-* 分区标签的非 visual 测试都必须归入某个模块 ----
    . (Join-Path $PSScriptRoot 'test-impact.ps1')
    $catalog = (Get-TestImpact @() $true).testMap
    $mappedTests = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($key in $catalog.Keys) {
        foreach ($name in $catalog[$key]) { $null = $mappedTests.Add("wtfml.$name") }
    }

    function Strip-CMakeComments([string]$Text) {
        $sb = [Text.StringBuilder]::new($Text.Length)
        $inQuote = $false
        $inComment = $false
        for ($i = 0; $i -lt $Text.Length; $i++) {
            $ch = $Text[$i]
            if ($inComment) {
                if ($ch -eq "`n") { $inComment = $false; [void]$sb.Append($ch) }
                continue
            }
            if ($inQuote) {
                [void]$sb.Append($ch)
                if ($ch -eq '\' -and $i + 1 -lt $Text.Length) { $i++; [void]$sb.Append($Text[$i]) }
                elseif ($ch -eq '"') { $inQuote = $false }
                continue
            }
            if ($ch -eq '#') { $inComment = $true; continue }
            if ($ch -eq '"') { $inQuote = $true }
            [void]$sb.Append($ch)
        }
        [regex]::Replace($sb.ToString(), '(?is)\bfunction\s*\(.*?\bendfunction\s*\([^)]*\)', '')
    }

    function Get-CMakeCallArgs([string]$Text, [string]$CommandRegex) {
        $calls = [Collections.Generic.List[string]]::new()
        foreach ($m in [regex]::Matches($Text, $CommandRegex)) {
            $depth = 1
            $inQuote = $false
            $start = $m.Index + $m.Length
            for ($i = $start; $i -lt $Text.Length; $i++) {
                $ch = $Text[$i]
                if ($inQuote) {
                    if ($ch -eq '\' -and $i + 1 -lt $Text.Length) { $i++ }
                    elseif ($ch -eq '"') { $inQuote = $false }
                    continue
                }
                if ($ch -eq '"') { $inQuote = $true; continue }
                if ($ch -eq '(') { $depth++ }
                elseif ($ch -eq ')') {
                    $depth--
                    if ($depth -eq 0) {
                        $calls.Add($Text.Substring($start, $i - $start))
                        break
                    }
                }
            }
        }
        return @($calls)
    }

    function Split-CMakeTokens([string]$ArgsText) {
        $tokens = [Collections.Generic.List[string]]::new()
        foreach ($m in [regex]::Matches($ArgsText, '"((?:\\.|[^"\\])*)"|(\S+)')) {
            $val = if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }
            if ($val) { $tokens.Add($val) }
        }
        return @($tokens)
    }

    $cmakeGroups = @(
        @{ Files = @('tests/CMakeLists.txt', 'tests/core_modules.cmake'); DefaultPartition = 'ci-core' },
        @{ Files = @('tests/ui_check/CMakeLists.txt'); DefaultPartition = 'ci-ui' },
        @{ Files = @('tests/mw_check/CMakeLists.txt'); DefaultPartition = 'ci-integration' },
        @{ Files = @('tools/network_diag/CMakeLists.txt'); DefaultPartition = $null }
    )
    $knownCMake = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($g in $cmakeGroups) { foreach ($f in $g.Files) { $null = $knownCMake.Add($f) } }
    $extraFiles = @(foreach ($dirName in @('tests', 'tools')) {
        $dir = Join-Path $repo $dirName
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        Get-ChildItem -LiteralPath $dir -Recurse -File -Include 'CMakeLists.txt', '*.cmake' | ForEach-Object {
            $rel = ([IO.Path]::GetRelativePath($repo, $_.FullName)) -replace '\\', '/'
            if ($rel -notmatch '^tests/test_runner/' -and -not $knownCMake.Contains($rel)) { $rel }
        }
    })
    if ($extraFiles.Count -gt 0) {
        $cmakeGroups += @{ Files = $extraFiles; DefaultPartition = $null }
    }

    $totalPartitionedTests = 0
    foreach ($group in $cmakeGroups) {
        $groupTests = [ordered]@{}
        foreach ($rel in $group.Files) {
            $full = Join-Path $repo $rel
            if (-not (Test-Path -LiteralPath $full)) { continue }
            $clean = Strip-CMakeComments ([IO.File]::ReadAllText($full))
            foreach ($argsText in Get-CMakeCallArgs $clean '(?i)\b(?:wtfml_)?add_test\s*\(') {
                $tokens = Split-CMakeTokens $argsText
                if ($tokens.Count -eq 0) { continue }
                $testName = if ($tokens[0] -eq 'NAME' -and $tokens.Count -ge 2) { $tokens[1] } else { $tokens[0] }
                if ($testName -match '^\$') { continue }
                if (-not $groupTests.Contains($testName)) {
                    $groupTests[$testName] = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
                }
                if ($group.DefaultPartition) {
                    $null = $groupTests[$testName].Add($group.DefaultPartition)
                }
            }
            foreach ($argsText in Get-CMakeCallArgs $clean '(?i)\bset_tests_properties\s*\(') {
                $tokens = Split-CMakeTokens $argsText
                $propIdx = [Array]::IndexOf($tokens, 'PROPERTIES')
                if ($propIdx -lt 1) { continue }
                $targets = @($tokens[0..($propIdx - 1)])
                $labels = @()
                for ($j = $propIdx + 1; $j + 1 -lt $tokens.Count; $j += 2) {
                    if ($tokens[$j] -eq 'LABELS') {
                        $labels += @($tokens[$j + 1] -split '[;\s]+' | Where-Object { $_ })
                    }
                }
                foreach ($t in $targets) {
                    if ($t -match '^\$' -or -not $groupTests.Contains($t)) { continue }
                    foreach ($lbl in $labels) { $null = $groupTests[$t].Add($lbl) }
                }
            }
        }
        foreach ($testName in $groupTests.Keys) {
            $labels = $groupTests[$testName]
            $ciLabels = @($labels | Where-Object { $_ -like 'ci-*' })
            if ($ciLabels.Count -eq 0 -or $labels.Contains('visual')) { continue }
            $totalPartitionedTests++
            if (-not $mappedTests.Contains($testName)) {
                throw "Test '$testName' has CI partition label(s) ($($ciLabels -join ', ')) but is not mapped to any module in tools/test-impact.ps1."
            }
        }
    }
    if ($totalPartitionedTests -eq 0) { throw 'No CI-partitioned tests discovered; check CMake test parsing.' }

    Write-Host "PASS test entry/module size limits, core build ownership, settings isolation and CI module coverage ($totalPartitionedTests tests)"
} catch {
    Write-Host "FAIL $_"
    exit 1
}
