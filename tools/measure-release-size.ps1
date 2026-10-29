#Requires -Version 7.0
[CmdletBinding()]
param([string]$BuildDir = 'build/release-mingw13')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDir, $repo)
$exe = Join-Path $build 'WTFMinecraftLauncher.exe'
$compilerLine = Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_CXX_COMPILER:[^=]+=(.+)$' | Select-Object -First 1
if (-not $compilerLine -or -not (Test-Path -LiteralPath $exe)) { throw 'Build the release executable first.' }
$bin = Split-Path $compilerLine.Matches[0].Groups[1].Value -Parent
foreach ($tool in @('strip.exe', 'objdump.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $bin $tool))) { throw "This measurement requires GNU $tool; source binary unchanged." }
}
$output = Join-Path $repo ('build/size-review-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$null = New-Item -ItemType Directory -Path $output
function Invoke-Bounded([string]$Tool, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $bin $Tool))
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) { $process.Kill($true); throw "$Tool exceeded 30 seconds." }
    if ($process.ExitCode -ne 0) { throw $stderr.GetAwaiter().GetResult() }
    return $stdout.GetAwaiter().GetResult()
}
function Get-Imports([string]$Path) {
    $dump = Invoke-Bounded 'objdump.exe' @('-p', $Path)
    return @([regex]::Matches($dump, 'DLL Name:\s*(\S+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object)
}
$sourceHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$imports = Get-Imports $exe
$variants = @()
foreach ($option in @('--strip-debug', '--strip-unneeded')) {
    $copy = Join-Path $output ($option.TrimStart('-') + '.exe')
    Copy-Item -LiteralPath $exe -Destination $copy
    $null = Invoke-Bounded 'strip.exe' @($option, $copy)
    $variants += @{ option=$option; bytes=(Get-Item -LiteralPath $copy).Length;
        importsUnchanged=(@(Compare-Object $imports (Get-Imports $copy)).Count -eq 0); file=$copy }
}
$unchanged = $sourceHash -eq (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
@{ source=$exe; sourceBytes=(Get-Item -LiteralPath $exe).Length; sourceUnchanged=$unchanged;
   imports=$imports; variants=$variants; note='Imports are checked; runtime compatibility still requires launch testing.' } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
if (-not $unchanged) { throw 'Source binary changed during measurement; do not use these results.' }
Write-Output (Join-Path $output 'report.json')
