# Pure planning helper: only accept compiler outputs belonging to the launcher.
function Get-CiLauncherObjects([string[]]$TargetLines) {
    $objects = @($TargetLines | ForEach-Object {
        if ($_ -match '^(CMakeFiles/WTFMinecraftLauncher\.dir/[^:]+\.obj): CXX_COMPILER__WTFMinecraftLauncher_\S+$') {
            $Matches[1]
        }
    } | Sort-Object -Unique)
    if ($objects.Count -eq 0) { throw 'Ninja listed no launcher compiler outputs; refusing an empty build plan.' }
    return $objects
}
