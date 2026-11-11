param([string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $taskRepo 'build/network-diag' }
$taskBuild = [IO.Path]::GetFullPath($BuildDirectory)
$taskExe = Join-Path $taskBuild 'tools/network_diag/WTFMLNetworkDiag.exe'
if (!(Test-Path -LiteralPath $taskExe)) { throw 'Build WTFMLNetworkDiag first.' }
$taskDestination = Join-Path $taskRepo ('build/WTFMLNetworkDiag-portable-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskDestination | Out-Null
Copy-Item -LiteralPath $taskExe -Destination $taskDestination
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs/dev/NETWORK_DIAGNOSTICS.md') -Destination (Join-Path $taskDestination '使用说明.md')
$env:PATH = 'D:\Qt\Qt\Tools\mingw1310_64\bin;D:\Qt\Qt\6.10.1\mingw_64\bin;' + $env:PATH
$taskDeploy = Start-Process -FilePath 'D:\Qt\Qt\6.10.1\mingw_64\bin\windeployqt.exe' -ArgumentList @('--release', '--no-translations', '--no-opengl-sw', '--no-system-d3d-compiler', '--dir', ('"' + $taskDestination + '"'), ('"' + (Join-Path $taskDestination 'WTFMLNetworkDiag.exe') + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskBuild 'deploy.log') -RedirectStandardError (Join-Path $taskBuild 'deploy-errors.log')
if (!$taskDeploy.WaitForExit(120000)) { taskkill /PID $taskDeploy.Id /T /F | Out-Null; throw 'Deployment exceeded 120 seconds.' }
$taskDeploy.Refresh()
Get-Content -LiteralPath (Join-Path $taskBuild 'deploy-errors.log') -Tail 10
if ($taskDeploy.ExitCode -ne 0) { throw 'Qt deployment failed.' }
Compress-Archive -LiteralPath $taskDestination -DestinationPath ($taskDestination + '.zip') -CompressionLevel Optimal
Write-Output $taskDestination
Write-Output ($taskDestination + '.zip')
