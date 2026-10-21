#requires -Version 5.1
<#
Tracks only the files used by the independent x64 cache helper.
Changes to camera scan, renderer, ASI, telemetry, etc. MUST NOT invalidate it.
This cache is a build optimization, not a runtime asset cache.
#>
Set-StrictMode -Version Latest

function Get-Cache64InputFingerprint {
    param(
        [Parameter(Mandatory=$true)][string]$Project,
        [Parameter(Mandatory=$true)][string]$Build64
    )

    $projectFull = [IO.Path]::GetFullPath($Project).TrimEnd('\', '/')
    $cacheFile = Join-Path $Build64 'CMakeCache.txt'
    $cmakeFile = Join-Path $projectFull 'CMakeLists.txt'
    $sourceDir = Join-Path $projectFull 'src\service64'
    if (-not (Test-Path -LiteralPath $cacheFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $cmakeFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
        throw 'Cache64 build configuration/source tree is missing'
    }

    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $visited = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in (Get-ChildItem -LiteralPath $sourceDir -File |
            Where-Object { $_.Extension -in @('.cpp','.c','.h','.hpp') })) {
        $pending.Push($file.FullName)
    }
    # The executable also links asc_core. Quoted-include traversal cannot
    # discover implementation files behind a static library's public headers.
    # Hash its sources too, so a catalog/cache fix cannot leave a stale helper.
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $projectFull 'src\core') -File |
            Where-Object { $_.Extension -in @('.cpp','.c','.h','.hpp') })) {
        $pending.Push($file.FullName)
    }

    # Follow local quoted includes recursively, rather than hashing every
    # include/asc header (most of which belong exclusively to the Win32 ASI).
    while ($pending.Count -gt 0) {
        $path = [IO.Path]::GetFullPath($pending.Pop())
        if (-not $visited.Add($path)) { continue }
        foreach ($hit in @(Select-String -LiteralPath $path -Pattern '^\s*#\s*include\s*"([^"]+)"')) {
            $name = $hit.Matches[0].Groups[1].Value
            $candidates = @(
                (Join-Path (Split-Path $path -Parent) $name),
                (Join-Path (Join-Path $projectFull 'include') $name),
                (Join-Path $sourceDir $name)
            )
            foreach ($candidate in $candidates) {
                if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
                $full = [IO.Path]::GetFullPath($candidate)
                if ($full.StartsWith($projectFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    $pending.Push($full)
                    break
                }
            }
        }
    }

    # CMake project and x64 configuration changes can affect compiler/link flags.
    [void]$visited.Add($cmakeFile)
    [void]$visited.Add([IO.Path]::GetFullPath($cacheFile))
    $entries = New-Object 'System.Collections.Generic.List[string]'
    foreach ($file in @($visited | Sort-Object)) {
        $relative = if ($file.StartsWith($projectFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
            $file.Substring($projectFull.Length + 1).Replace('\','/')
        } else {
            'build64/CMakeCache.txt'
        }
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        $entries.Add("$relative=$hash")
    }
    $text = $entries -join "`n"
    $bytes = [Text.Encoding]::UTF8.GetBytes($text)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace('-','')
    } finally {
        $sha256.Dispose()
    }
}

function Get-Cache64StampPath {
    param([Parameter(Mandatory=$true)][string]$Build64)
    return (Join-Path $Build64 'cache64-inputs.sha256')
}

function Test-Cache64BuildCurrent {
    param(
        [Parameter(Mandatory=$true)][string]$Build64,
        [Parameter(Mandatory=$true)][string]$Fingerprint,
        [Parameter(Mandatory=$true)][string]$Executable
    )
    $stamp = Get-Cache64StampPath -Build64 $Build64
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf) -or
        -not (Test-Path -LiteralPath $stamp -PathType Leaf)) { return $false }
    return (([IO.File]::ReadAllText($stamp)).Trim() -ceq $Fingerprint)
}

function Save-Cache64BuildState {
    param(
        [Parameter(Mandatory=$true)][string]$Project,
        [Parameter(Mandatory=$true)][string]$Build64,
        [Parameter(Mandatory=$true)][string]$Executable
    )
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Cache64 output missing; refusing to mark up-to-date: $Executable"
    }
    $fingerprint = Get-Cache64InputFingerprint -Project $Project -Build64 $Build64
    $stamp = Get-Cache64StampPath -Build64 $Build64
    [IO.File]::WriteAllText($stamp, $fingerprint + "`n", [Text.Encoding]::ASCII)
}
