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
$failed = 0

function Check([bool]$Ok, [string]$Text) {
    if ($Ok) { Write-Host "PASS $Text" }
    else { Write-Host "FAIL $Text"; $script:failed++ }
}

try {
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
} catch {
    Write-Host "FAIL runner self-check: $_"
    $failed++
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    Write-Host "Self-check evidence: $work"
}
exit $(if ($failed) { 1 } else { 0 })
