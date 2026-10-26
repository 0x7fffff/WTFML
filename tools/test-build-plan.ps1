#Requires -Version 7.0

function Get-CompactTestRunnerMode([string]$Mode, [string]$Suite, [bool]$HasModules) {
    if ($Mode -eq 'Compact') { return 'ON' }
    if ($Mode -eq 'Standalone') { return 'OFF' }
    # Partial selections keep independent compilation. The shared runner needs
    # all specialist case objects, so enabling it for just a few changed modules
    # would increase cold CI work even though it saves links on a warm build.
    if (-not $HasModules -and $Suite -in @('full', 'ci-ui', 'ci-integration', 'visual', 'affected')) { return 'ON' }
    return 'OFF'
}

function Get-TestBuildTargets([object[]]$Tests, [object[]]$Owners) {
    $targets = [Collections.Generic.HashSet[string]]::new()
    foreach ($test in $Tests) {
        $entry = @($Owners | Where-Object { $_.test -eq $test.name })
        if ($entry.Count -ne 1) {
            throw "Missing/duplicate build owner for $($test.name). Update the CMake test target registry."
        }
        if ($entry[0].target -ne '-') { $null = $targets.Add($entry[0].target) }
    }
    @($targets | Sort-Object)
}

function Get-TestRuntimeTargets([string[]]$Targets) {
    # Build the shared layers separately so a cold integration build does not
    # spend its entire timeout compiling all three production layers at once.
    if (@($Targets | Where-Object { $_ -like 'mw_check*' }).Count) {
        'wtfml_core_runtime'; 'wtfml_ui_runtime'; 'wtfml_mw_runtime'
    } elseif (@($Targets | Where-Object { $_ -like 'ui_check*' }).Count) {
        'wtfml_core_runtime'; 'wtfml_ui_runtime'
    } elseif (@($Targets | Where-Object { $_ -like 'verify*' }).Count) {
        'wtfml_core_runtime'
    }
}
