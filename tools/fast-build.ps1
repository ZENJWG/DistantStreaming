#requires -Version 5.1
<#
Fast incremental build after build.bat has configured VS/MSVC/Plugin-SDK once.
Exit 20 = first-time setup required (build.bat then runs the full bootstrap).
Exit 1  = actual compile/link/package error (do not hide it by bootstrapping).
#>
[CmdletBinding()]
param([switch]$ForceCache64)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build64 = Join-Path $project 'build-vs2026-x64'
$build32 = Join-Path $project 'build-vs2026-win32'
$helper64 = Join-Path $build64 'Release\DistantStreaming_AssetCache64.exe'
$asi = Join-Path $build32 'Release\DistantStreaming.asi'
$dist = Join-Path $project 'dist'

function Find-Cmake {
    $cmd = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($path in @(
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\CMake\bin\cmake.exe'))) {
        if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) { return $path }
    }
    # Desktop C++ includes CMake even when no standalone install is on PATH.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $bundled = & $vswhere -latest -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        foreach ($path in $bundled) {
            if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
        }
    }
    return $null
}

function Run([string]$exe, [string[]]$arguments) {
    Write-Host ('[fast-build] ' + ($arguments -join ' '))
    & $exe @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
}

function Copy-IfChanged([string]$source, [string]$destination) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing build output: $source"
    }
    $copy = -not (Test-Path -LiteralPath $destination -PathType Leaf)
    if (-not $copy) {
        $src = Get-Item -LiteralPath $source
        $dst = Get-Item -LiteralPath $destination
        $copy = ($src.Length -ne $dst.Length)
        if (-not $copy) {
            $copy = ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -cne
                     (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash)
        }
    }
    if ($copy) { Copy-Item -LiteralPath $source -Destination $destination -Force }
}

if (-not (Test-Path -LiteralPath (Join-Path $build64 'CMakeCache.txt') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $build32 'CMakeCache.txt') -PathType Leaf)) {
    Write-Host '[fast-build] First-time CMake setup needed; use build.bat (automatic) or build.bat full.'
    exit 20
}
$cmake = Find-Cmake
if (-not $cmake) {
    Write-Host '[fast-build] CMake not found; full bootstrap may locate/install it.'
    exit 20
}

try {
    . (Join-Path $PSScriptRoot 'cache64-build-state.ps1')
    $fingerprint = Get-Cache64InputFingerprint -Project $project -Build64 $build64
    if ($ForceCache64 -or -not (Test-Cache64BuildCurrent -Build64 $build64 `
                            -Fingerprint $fingerprint -Executable $helper64)) {
        Write-Host '[fast-build] Cache64 changed/missing: building only the x64 helper target.'
        Run $cmake @('--build', $build64, '--config', 'Release',
            '--target', 'DistantStreaming_AssetCache64', '--parallel')
        Save-Cache64BuildState -Project $project -Build64 $build64 -Executable $helper64
    } else {
        Write-Host '[fast-build] Cache64 unchanged: x64 compile/link SKIPPED.'
    }

    # Keep ASI and helper separate. Win32 builds do not cause x64 cache rebuilds.
    Run $cmake @('--build', $build32, '--config', 'Release',
        '--target', 'DistantStreaming', '--parallel')

    if (-not (Test-Path -LiteralPath $asi -PathType Leaf)) {
        throw "Missing ASI output: $asi"
    }
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    foreach ($legacy in @(
        'AdaptiveStreaming.asi', 'AdaptiveStreaming.ini',
        'AdaptiveStreaming.pdb', 'AdaptiveStreaming.map',
        'ASC_AssetCache64.exe', 'ASC_HDService64.exe')) {
        $legacyPath = Join-Path $dist $legacy
        if (Test-Path -LiteralPath $legacyPath) {
            Remove-Item -LiteralPath $legacyPath -Force
        }
    }
    Copy-IfChanged $helper64 (Join-Path $dist 'DistantStreaming_AssetCache64.exe')
    Copy-IfChanged $asi (Join-Path $dist 'DistantStreaming.asi')
    Copy-IfChanged (Join-Path $project 'config\DistantStreaming.ini') `
        (Join-Path $dist 'DistantStreaming.ini')
    foreach ($symbol in @('DistantStreaming.pdb', 'DistantStreaming.map')) {
        $src = Join-Path (Join-Path $build32 'Release') $symbol
        if (Test-Path -LiteralPath $src -PathType Leaf) {
            Copy-IfChanged $src (Join-Path $dist $symbol)
        }
    }
    Write-Host '[fast-build] OK: dist\DistantStreaming.asi + unchanged/rebuilt Cache64.exe'
    exit 0
} catch {
    Write-Host ('[fast-build] ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'For a clean rebuild / dependency update, use build.bat full.'
    exit 1
}
