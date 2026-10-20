#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$work = Join-Path $repo ('build/test-runner-selfchecks/' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $work 'fixture space 测试'
$reports = Join-Path $work 'reports'
$null = New-Item -ItemType Directory -Path $fixture -Force
Copy-Item -LiteralPath (Join-Path $repo 'tests/test_runner/RunnerFixture.cmake') -Destination (Join-Path $fixture 'CTestTestfile.cmake')
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldPartitionCase = $env:WTFML_PARTITION_FIXTURE_CASE
$failed = 0

function Check([bool]$Ok, [string]$Text) {
    if ($Ok) { Write-Host "PASS $Text" }
    else { Write-Host "FAIL $Text"; $script:failed++ }
}

try {
    . (Join-Path $PSScriptRoot 'ci-object-batches.ps1')
    $objectFixture = @(
        'CMakeFiles/WTFMinecraftLauncher.dir/src/one.cpp.obj: CXX_COMPILER__WTFMinecraftLauncher_unscanned_Release',
        'CMakeFiles/WTFMinecraftLauncher.dir/src/one.cpp.obj: CXX_COMPILER__WTFMinecraftLauncher_unscanned_Release',
        'CMakeFiles/WTFMinecraftLauncher.dir/src/space name.cpp.obj: CXX_COMPILER__WTFMinecraftLauncher_unscanned_Release',
        'tests/CMakeFiles/verify.dir/one.cpp.obj: CXX_COMPILER__verify_unscanned_Release',
        'WTFMinecraftLauncher.exe: CXX_EXECUTABLE_LINKER__WTFMinecraftLauncher_Release'
    )
    $planned = @(Get-CiLauncherObjects $objectFixture)
    Check ($planned.Count -eq 2 -and $planned[1] -like '*space name.cpp.obj') 'launcher object plan excludes other targets and deduplicates outputs'
    $emptyRejected = $false
    try { $null = Get-CiLauncherObjects @('unrelated: phony') } catch { $emptyRejected = $true }
    Check $emptyRejected 'empty launcher object plan fails closed'
    foreach ($suite in @('core', 'mods', 'java', 'window')) {
        # These failures are intentional fixture outcomes, isolated from normal CI reports.
        $timeout = if ($suite -eq 'java') { 2 } else { 15 }
        $output = Join-Path $work "$suite.output.log"
        & (Join-Path $PSScriptRoot 'test.ps1') -Suite $suite -BuildDir $fixture -NoBuild -ReportRoot $reports -TestTimeoutSeconds $timeout *> $output
        $code = $LASTEXITCODE
        $expected = if ($suite -eq 'core') { 0 } else { 1 }
        Check ($code -eq $expected) "runner exit status for $suite"
        if ($code -ne $expected) { Get-Content -LiteralPath $output -Tail 25 | Write-Host }
        Check ($env:TEMP -eq $oldTemp -and $env:TMP -eq $oldTmp) "temporary environment restored after $suite"
    }
    $files = @(Get-ChildItem -LiteralPath $reports -Filter summary.json -Recurse)
    Check ($files.Count -eq 4) 'each invocation preserves a separate report'
    foreach ($file in $files) {
        $record = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        Check ($record.stages.Count -eq 1) "one test stage recorded for $($record.suite)"
        $stage = $record.stages[0]
        switch ($record.suite) {
            'core' {
                Check ($record.exitCode -eq 0 -and $stage.exitCode -eq 0) 'passing tests succeed with a spaced/Unicode build path'
                $junit = Join-Path $file.DirectoryName 'results.xml'
                Check (Test-Path -LiteralPath $junit) 'successful test writes JUnit report'
            }
            'mods' { Check ($record.exitCode -ne 0 -and $stage.exitCode -ne 0 -and -not $stage.timedOut) 'real assertion failure is not swallowed' }
            'java' { Check ($stage.timedOut -and $stage.exitCode -eq 124 -and $record.exitCode -ne 0) 'timeout kills the process tree and records 124' }
            'window' { Check ($record.exitCode -ne 0 -and $stage.exitCode -ne 0 -and -not $stage.timedOut) 'empty test selection is not a false green' }
            default { Check $false 'unexpected fixture suite' }
        }
    }
    $partitionFixture = Join-Path $work 'partitions space 测试'
    $null = New-Item -ItemType Directory -Path $partitionFixture -Force
    Copy-Item -LiteralPath (Join-Path $repo 'tests/test_runner/PartitionFixture.cmake') `
        -Destination (Join-Path $partitionFixture 'CTestTestfile.cmake')
    foreach ($case in @('valid', 'missing', 'duplicate', 'empty')) {
        $env:WTFML_PARTITION_FIXTURE_CASE = $case
        & (Join-Path $PSScriptRoot 'check-ci-partitions.ps1') -BuildDir $partitionFixture `
            *> (Join-Path $work "partition-$case.output.log")
        Check ($LASTEXITCODE -eq $(if ($case -eq 'valid') { 0 } else { 1 })) `
            "CI partition check handles $case (visual tests excluded)"
    }
} catch {
    Write-Host "FAIL runner self-check: $_"
    $failed++
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:WTFML_PARTITION_FIXTURE_CASE = $oldPartitionCase
    Write-Host "Self-check evidence: $work"
}
exit $(if ($failed) { 1 } else { 0 })
