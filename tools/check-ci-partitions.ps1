#Requires -Version 7.0
[CmdletBinding()]
param([string]$BuildDir = 'build/verification-mingw13')

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
$report = Join-Path $repo ('build/ci-partition-checks/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $report -Force

function Read-Tests([string]$Group, [string[]]$Filter) {
    $arguments = @('--test-dir', $build, '--show-only=json-v1', '-LE', 'visual') + $Filter
    $quoted = @($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    $output = Join-Path $report "$Group.json"
    $process = Start-Process ctest -ArgumentList $quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $output -RedirectStandardError (Join-Path $report "$Group.stderr.log")
    try {
        if (-not $process.WaitForExit(15000)) {
            $process.Kill($true)
            throw "CTest enumeration timed out: $Group"
        }
        if ($process.ExitCode -ne 0) { throw "CTest enumeration failed: $Group" }
        $catalog = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        return @($catalog.tests | ForEach-Object { $_.name })
    } finally {
        if (-not $process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
}

try {
    $full = @(Read-Tests 'full' @())
    if ($full.Count -eq 0) { throw 'The non-visual test set is empty' }
    $owners = @{}
    foreach ($group in @('ci-app', 'ci-core', 'ci-ui', 'ci-integration')) {
        $names = @(Read-Tests $group @('-L', "^$group$"))
        if ($names.Count -eq 0) { throw "Empty CI partition: $group" }
        foreach ($name in $names) {
            if ($owners.ContainsKey($name)) { throw "Duplicate CI assignment: $name" }
            $owners[$name] = $group
        }
        Write-Host "PASS $group contains $($names.Count) tests"
    }
    foreach ($name in $full) {
        if (-not $owners.ContainsKey($name)) { throw "Test missing from CI: $name" }
    }
    if ($owners.Count -ne $full.Count) { throw 'CI partitions differ from the full non-visual test set' }
    Write-Host "PASS all $($full.Count) non-visual tests belong to exactly one CI partition"
} catch {
    Write-Host "FAIL $_"
    exit 1
}
exit 0
