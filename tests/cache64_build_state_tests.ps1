param([Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\cache64-build-state.ps1')
$workRoot = (Resolve-Path -LiteralPath $WorkDirectory).Path
$fixture = [IO.Path]::GetFullPath((Join-Path $workRoot ('fingerprint-' + [guid]::NewGuid())))
if (-not $fixture.StartsWith($workRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Fixture must stay inside the specified build directory'
}
try {
    foreach ($relative in @('src\service64', 'src\core', 'src\game', 'build')) {
        New-Item -ItemType Directory -Path (Join-Path $fixture $relative) -Force | Out-Null
    }
    Set-Content -LiteralPath (Join-Path $fixture 'CMakeLists.txt') -Value '# fixture'
    Set-Content -LiteralPath (Join-Path $fixture 'build\CMakeCache.txt') -Value '# fixture'
    Set-Content -LiteralPath (Join-Path $fixture 'src\service64\main.cpp') -Value '// helper'
    $coreFile = Join-Path $fixture 'src\core\WorldCatalogCache.cpp'
    Set-Content -LiteralPath $coreFile -Value '// core v1'
    $before = Get-Cache64InputFingerprint -Project $fixture -Build64 (Join-Path $fixture 'build')
    Set-Content -LiteralPath $coreFile -Value '// core v2'
    $after = Get-Cache64InputFingerprint -Project $fixture -Build64 (Join-Path $fixture 'build')
    if ($before -ceq $after) { throw 'Core implementation change must rebuild the helper' }
    Set-Content -LiteralPath (Join-Path $fixture 'src\game\CameraReadinessWin.cpp') -Value '// ASI only'
    $asiOnly = Get-Cache64InputFingerprint -Project $fixture -Build64 (Join-Path $fixture 'build')
    if ($after -cne $asiOnly) { throw 'ASI-only change must not rebuild the helper' }
    Write-Output 'PASS: shared core invalidates helper; ASI-only changes preserve its build cache'
} finally {
    # $fixture was resolved and verified beneath the explicitly supplied build
    # directory above, before creating or recursively removing any files.
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
