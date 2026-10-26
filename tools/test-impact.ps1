#Requires -Version 7.0
[CmdletBinding()]
param([Alias('ChangedPath')][string[]]$ImpactChangedPath,
    [Alias('BaseRef')][string]$ImpactBaseRef = 'origin/main',
    [Alias('Full')][switch]$ImpactFull, [Alias('GitHubOutput')][switch]$ImpactGitHubOutput)

function Get-TestImpact([string[]]$Paths, [bool]$ForceFull = $false) {
    $catalog = [ordered]@{
        core = @('core', 'core-bootstrap', 'core-downloads', 'core-install', 'core-instances', 'core-java', 'core-accounts',
                 'core-resources', 'core-launch', 'core-servers', 'core-activity', 'core-presets', 'core-toolbox')
        'core-downloads' = @('core-downloads')
        'core-bootstrap' = @('core-bootstrap')
        'core-install' = @('core-install')
        'core-instances' = @('core-instances')
        'core-java' = @('core-java')
        'core-accounts' = @('core-accounts')
        'core-resources' = @('core-resources')
        'core-launch' = @('core-launch')
        'core-servers' = @('core-servers')
        'core-activity' = @('core-activity')
        'core-presets' = @('core-presets')
        'core-toolbox' = @('core-toolbox')
        runner = @('test-runner', 'test-modules', 'ci-partitions')
        ui = @('ui-refresh', 'install-layout', 'lifecycle'); theme = @('theme')
        'ui-history' = @('ui-bootstrap')
        window = @('main-window'); workspace = @('workspace', 'activity-selection')
        java = @('java-manage'); mods = @('mod-action-panel'); upgrade = @('instance-upgrade')
        network = @('download-retry', 'high-speed-download', 'segmented-download', 'segment-install')
        presets = @('preset-install'); toolbox = @('tool-features', 'toolbox-polish', 'armor-trim')
        resources = @('resource-packs'); app = @('network-diag')
    }
    $testSources = @{
        'tests/ui_check/modules/UiInstallLayoutCheck.cpp' = 'ui'
        'tests/ui_check/modules/UiToolboxPolishCheck.cpp' = 'toolbox'
        'tests/ui_check/modules/UiResourcePacksCheck.cpp' = 'resources'
        'tests/ui_check/modules/UiLifecycleCheck.cpp' = 'ui'
        'tests/ui_check/modules/UiUiRefreshCheck.cpp' = 'ui'
        'tests/ui_check/modules/UiThemeCheck.cpp' = 'theme'
        'tests/mw_check/modules/MwHighSpeedDownloadCheck.cpp' = 'network'
        'tests/mw_check/modules/MwDownloadRetryCheck.cpp' = 'network'
        'tests/mw_check/modules/MwActivitySelectionCheck.cpp' = 'workspace'
        'tests/mw_check/modules/MwWorkspaceCheck.cpp' = 'workspace'
        'tests/mw_check/JavaManageChecks.cpp' = 'java'
        'tests/mw_check/ModActionPanelChecks.cpp' = 'mods'
        'tests/mw_check/RangeChecks.cpp' = 'network'
        'tests/mw_check/SegmentInstallChecks.cpp' = 'network'
        'tests/mw_check/ToolFeatureChecks.cpp' = 'toolbox'
        'tests/mw_check/PresetInstallChecks.cpp' = 'presets'
        'tests/mw_check/PresetManifestChecks.cpp' = 'presets'
        'tests/mw_check/PresetPreviewChecks.cpp' = 'presets'
        'tests/mw_check/InstanceUpgradeChecks.cpp' = 'upgrade'
        'tests/ui_check/ArmorTrimChecks.cpp' = 'toolbox'
    }
    $modules = [Collections.Generic.HashSet[string]]::new()
    $reasons = [Collections.Generic.List[string]]::new()
    $all = $ForceFull
    foreach ($raw in $Paths) {
        $path = $raw.Replace('\', '/') -replace '^\./', ''
        if ($path -match '^(docs/|build/|.*\.md$|LICENSE$|\.gitignore$)') { continue }
        if ($path -match '^(CMakeLists\.txt|CMakePresets\.json|\.github/|tests/.*/CMakeLists\.txt|tests/CMakeLists\.txt|src/MainWindow\.(cpp|hpp)|src/main\.cpp|ui/MainWindowUI\.(cpp|hpp))$' -or
            $path -match '^(\.github/|src/(auth|settings|net|launcher|tasks|util)/|tests/main\.cpp$|tests/mw_check/main\.cpp$|tests/ui_check/main\.cpp$)') {
            $all = $true; $reasons.Add("shared: $path"); continue
        }
        if ($path -match '^tests/core_modules/([^/]+)$') {
            $owners = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../tests/core_modules/owners.json') -Raw | ConvertFrom-Json -AsHashtable
            if ($owners.ContainsKey($Matches[1])) { $null = $modules.Add('core-' + $owners[$Matches[1]]); continue }
        }
        if ($path -match '^tests/ui_check/modules/Ui[^/]*Checks\.cpp$') {
            $null = $modules.Add('ui-history'); continue
        }
        if ($testSources.ContainsKey($path)) { $null = $modules.Add($testSources[$path]); continue }
        if ($path -match '^tests/mw_check/modules/[^/]*(Checks|Tests)\.cpp$') {
            $null = $modules.Add('window'); continue
        }
        $selected = switch -Regex ($path) {
            '(?i)VersionDeclaration' { @('core','upgrade'); break }
            '(?i)(InstanceUpdate|InstanceUpgrade|UpgradeAssessment|InstanceTransfer)' { @('upgrade','ui'); break }
            '(?i)(Preset|Modpack)' { @('core','presets','network','mods','ui'); break }
            '(?i)(Theme|ToggleSwitch|MemorySlider|JavaSettings|JavaManage|GlobalJava|EffectiveJava)' { @('core','java','theme','ui','window'); break }
            '(?i)(ArmorTrim|Toolbox|ToolFeature|Banner|Beacon|Enchantment)' { @('core','toolbox','ui'); break }
            '(?i)(TaskCenter|TaskDetail|Activity|Workspace|HomeQuick)' { @('workspace','ui','window'); break }
            '(?i)(ModAction|ModDepend|ResourceStore|ResourceCatalog|InstanceFile|ResourcePack)' { @('core','mods','resources','upgrade','presets','ui'); break }
            '^(src/download/|tools/network_diag/)|(?i:Download|RangeChecks|SegmentInstall)' { @('core','network','presets'); break }
            '^(tools/.*\.ps1|tests/test_runner/)' { @('runner'); break }
            '^ui/' { @('ui','theme','window'); break }
            default { @() }
        }
        if ($selected.Count -eq 0) { $all = $true; $reasons.Add("unclassified: $path") }
        foreach ($module in $selected) { $null = $modules.Add($module) }
        if ($path -match '^ui/.*\.hpp$') { $null = $modules.Add('ui-history') }
        if ($path -match '^(src/|ui/|assets/|tools/network_diag/)') { $null = $modules.Add('app') }
    }
    if ($all) { foreach ($key in $catalog.Keys) { $null = $modules.Add($key) } }
    $tests = @($modules | ForEach-Object { $catalog[$_] } | Sort-Object -Unique)
    $suites = [Collections.Generic.List[string]]::new()
    if ($modules.Contains('app')) { $suites.Add('ci-app') }
    if (@($modules | Where-Object { $_ -eq 'core' -or $_ -like 'core-*' -or $_ -eq 'runner' }).Count) { $suites.Add('ci-core') }
    if (@($modules | Where-Object { $_ -in @('ui','ui-history','theme','resources','toolbox') }).Count) { $suites.Add('ci-ui') }
    if (@($modules | Where-Object { $_ -in @('window','workspace','java','mods','upgrade','network','presets','toolbox') }).Count) { $suites.Add('ci-integration') }
    [pscustomobject]@{ full = $all; modules = @($modules | Sort-Object); tests = $tests; suites = @($suites.ToArray());
        reasons = @($reasons.ToArray()); paths = @($Paths); testMap = $catalog }
}

function Get-ChangedTestPaths([string]$Base) {
    $repo = Split-Path $PSScriptRoot -Parent
    $paths = @(& git -C $repo diff --no-renames --name-only "$Base...HEAD")
    if ($LASTEXITCODE -ne 0) { throw "Cannot determine changes against $Base; use -Full or a valid -BaseRef." }
    $paths += @(& git -C $repo diff --no-renames --name-only HEAD)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate working changes.' }
    $paths += @(& git -C $repo ls-files --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate untracked source files.' }
    @($paths | Sort-Object -Unique)
}

if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    if (-not $PSBoundParameters.ContainsKey('ImpactChangedPath')) {
        $ImpactChangedPath = if ($ImpactFull) { @() } else { @(Get-ChangedTestPaths $ImpactBaseRef) }
    }
    $impact = Get-TestImpact $ImpactChangedPath ([bool]$ImpactFull)
    $impact | ConvertTo-Json -Depth 5
    if ($ImpactGitHubOutput) {
        if (-not $env:GITHUB_OUTPUT) { throw 'GITHUB_OUTPUT is not available.' }
        # An empty matrix is invalid even when the job is skipped. Keep one placeholder.
        $matrix = if ($impact.suites.Count) { @($impact.suites) } else { @('ci-core') }
        "suites=$($matrix | ConvertTo-Json -Compress -AsArray)" >> $env:GITHUB_OUTPUT
        $filter = if ($impact.full) { '' } else { $impact.modules -join ',' }
        "modules=$filter" >> $env:GITHUB_OUTPUT
        "run_tests=$([bool]$impact.suites.Count)".ToLowerInvariant() >> $env:GITHUB_OUTPUT
    }
}
