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
    # A private user INI alone still falls back to machine settings; default ledgers
    # also need their own game root before production objects are constructed.
    foreach ($entry in Get-ChildItem -LiteralPath (Join-Path $repo 'tests/core_modules') -Filter '*_main.cpp') {
        $source = [IO.File]::ReadAllText($entry.FullName)
        foreach ($required in @('QSettings::SystemScope', 'QSettings::UserScope', 'wtfml.testDefaultMinecraftDir', 'QTemporaryDir fixture')) {
            if (-not $source.Contains($required)) { throw "$($entry.Name) lacks process isolation: $required" }
        }
    }
    foreach ($entry in @('tests/main.cpp','tests/ui_check/main.cpp','tests/mw_check/main.cpp')) {
        $source = [IO.File]::ReadAllText((Join-Path $repo $entry))
        if (-not $source.Contains('QSettings::SystemScope') -or -not $source.Contains('wtfml.testDefaultMinecraftDir')) {
            throw "$entry lacks system/default-root isolation"
        }
    }
    Write-Host 'PASS test entry/module size limits and core build ownership'
} catch {
    Write-Host "FAIL $_"
    exit 1
}
