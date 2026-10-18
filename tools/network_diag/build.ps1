param([string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $taskRepo 'build/network-diag' }
$taskBuild = [IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$env:PATH = 'D:\Qt\Qt\Tools\mingw1310_64\bin;D:\Qt\Qt\6.10.1\mingw_64\bin;' + $env:PATH
function Invoke-BoundedCMake([string[]]$Arguments, [string]$Phase, [int]$Limit) {
    $taskOut = Join-Path $taskBuild ($Phase + '.log')
    $taskErr = Join-Path $taskBuild ($Phase + '-errors.log')
    $taskRun = Start-Process -FilePath (Get-Command cmake).Source -ArgumentList $Arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $taskOut -RedirectStandardError $taskErr
    if (!$taskRun.WaitForExit($Limit * 1000)) {
        taskkill /PID $taskRun.Id /T /F | Out-Null
        throw "$Phase exceeded $Limit seconds; stopped its process tree."
    }
    $taskRun.Refresh()
    Get-Content -LiteralPath $taskOut -Tail 12
    Get-Content -LiteralPath $taskErr -Tail 24
    if ($taskRun.ExitCode -ne 0) { throw "$Phase failed ($($taskRun.ExitCode))." }
}
Invoke-BoundedCMake @('-S', ('"' + $taskRepo + '"'), '-B', ('"' + $taskBuild + '"'), '-G', '"MinGW Makefiles"', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_PREFIX_PATH=D:/Qt/Qt/6.10.1/mingw_64', '-DWTFML_BUILD_TESTS=ON', '-DWTFML_BUILD_NETWORK_DIAG=ON') 'configure' 180
Invoke-BoundedCMake @('--build', ('"' + $taskBuild + '"'), '--target', 'WTFMLNetworkDiag', '-j', '4') 'build' 600
