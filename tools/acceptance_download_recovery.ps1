#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Root,
    [string]$BuildDir = 'build/verification-mingw13',
    [ValidateRange(1, 900)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathFullyQualified($Root)) { throw 'Use a new absolute acceptance directory.' }
$rootPath = [IO.Path]::GetFullPath($Root)
if (Test-Path -LiteralPath $rootPath) { throw "Refusing existing directory: $rootPath" }
$executables = @('mw_check_high_speed_download', 'mw_check_preset_install')
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
foreach ($name in $executables) {
    if (-not (Test-Path -LiteralPath "$build/tests/mw_check/$name.exe")) { throw "Build $name first." }
}
$null = New-Item -ItemType Directory -Path "$rootPath/temp"
@{ format='wtfml-download-recovery-acceptance/1'; root=$rootPath; createdAt=(Get-Date).ToUniversalTime().ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath "$rootPath/owned.json" -Encoding utf8
$originalTemp = $env:TEMP; $originalTmp = $env:TMP; $originalPlatform = $env:QT_QPA_PLATFORM
$results = @()
try {
    $env:TEMP = "$rootPath/temp"; $env:TMP = $env:TEMP; $env:QT_QPA_PLATFORM = 'offscreen'
    foreach ($name in $executables) {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath "$build/tests/mw_check/$name.exe" -WorkingDirectory $rootPath -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput "$rootPath/$name.stdout.log" -RedirectStandardError "$rootPath/$name.stderr.log"
        $timeout = -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($timeout) { $process.Kill($true); $null = $process.WaitForExit(10000) }
        $results += @{ program=$name; exitCode=$process.ExitCode; timedOut=$timeout; seconds=$watch.Elapsed.TotalSeconds }
        Write-Host "$name : exit $($process.ExitCode), timeout $timeout; evidence: $rootPath"
    }
} finally {
    $env:TEMP=$originalTemp; $env:TMP=$originalTmp; $env:QT_QPA_PLATFORM=$originalPlatform
    @{ root=$rootPath; tests=$results; ok=($results.Count -eq 2 -and @($results | Where-Object { $_.timedOut -or $_.exitCode -ne 0 }).Count -eq 0) } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$rootPath/report.json" -Encoding utf8
}
if (@($results | Where-Object { $_.timedOut -or $_.exitCode -ne 0 }).Count) { exit 1 }
