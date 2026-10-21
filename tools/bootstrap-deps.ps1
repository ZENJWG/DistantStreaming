#requires -Version 5.1
<#
  Auto-dependency bootstrap for Distant Streaming (Windows / VS 2026 / Win32).
  Called by ../build.bat. All downloads are from winget or official git remotes.
  - Installs/updates Git and CMake using winget.
  - Checks VS 2026 x86 C++ tools; installs missing VS Build Tools (not a second IDE).
  - Clones/updates OWN managed Plugin-SDK and vcpkg checkouts under .deps.
  - Generates and builds Plugin.lib (plugin_sa target) with Premake/MSBuild, if required.
  - Installs/updates MinHook:x86-windows-static using vcpkg.
  - Builds the ASI and x64 helper and packages dist/; tests are opt-in.

  EXTERNAL paths (passed as args/environment) are NEVER git-pulled or vcpkg-upgraded
  unless ASC_UPDATE_EXTERNAL=1. MinHook can be installed into an external vcpkg
  because it is a required dependency.
#>
[CmdletBinding()]
param(
    [Parameter(Position=0)][string]$PluginSdkRoot = '',
    [Parameter(Position=1)][string]$PluginSdkSaLib = '',
    [Parameter(Position=2)][string]$VcpkgRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$managed = Join-Path $project '.deps'
$upgrade = $env:ASC_SKIP_UPGRADES -ne '1'
$updateExternal = $env:ASC_UPDATE_EXTERNAL -eq '1'
$offline = $env:ASC_OFFLINE -eq '1'
$wingetReady = $false
New-Item -ItemType Directory -Force -Path $managed | Out-Null

function Say([string]$msg) { Write-Host "[Distant Streaming] $msg" }
function Fail([string]$msg) { throw $msg }
function Exec([string]$command, [string[]]$arguments) {
    Say ("RUN " + (Split-Path $command -Leaf) + ' ' + ($arguments -join ' '))
    & $command @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$command exited with $LASTEXITCODE"
    }
}
function Exec-AllowFailure([string]$command, [string[]]$arguments) {
    & $command @arguments | Out-Host
    return ($LASTEXITCODE -eq 0)
}
function Resolve-Executable([string]$name, [string[]]$alternatives) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($file in $alternatives) {
        if ($file -and (Test-Path -LiteralPath $file -PathType Leaf)) {
            $env:PATH = (Split-Path $file -Parent) + ';' + $env:PATH
            return $file
        }
    }
    return $null
}
function Winget-Package([string]$id, [bool]$present) {
    if (-not $wingetReady) {
        Fail "winget (Windows App Installer) is required for automatic install/upgrade of $id. Install App Installer from Microsoft first."
    }
    if ($offline) { Fail "Cannot install missing dependency $id in ASC_OFFLINE=1 mode" }
    $wingetArgs = @(
        $(if ($present) {'upgrade'} else {'install'}),
        '--id', $id, '--exact', '--source', 'winget',
        '--silent', '--accept-package-agreements', '--accept-source-agreements'
    )
    # For upgrades winget returns nonzero when already current or unmanaged.
    # A failed install is always fatal; a failed upgrade leaves a visible warning.
    if (-not (Exec-AllowFailure 'winget.exe' $wingetArgs)) {
        if (-not $present) { Fail "winget could not install $id. Check network, permissions, and installer output." }
        Say "WARNING: winget did not upgrade $id (already current or unsupported)."
    }
}
function Test-VersionAtLeast([string]$found, [version]$minimum) {
    if ($found -match '(\d+\.\d+(?:\.\d+)?)') {
        return ([version]$Matches[1] -ge $minimum)
    }
    return $false
}
function Ensure-Binary([string]$name, [string]$package, [string[]]$locations, [version]$minimum) {
    $binary = Resolve-Executable $name $locations
    if (-not $binary) { Winget-Package $package $false }
    elseif ($upgrade -and -not $offline) { Winget-Package $package $true }

    # An installer can leave an OLD binary ahead of the new one on PATH.
    # Probe known install locations as well and select the newest candidate.
    $candidates = @($binary) + $locations
    $best = $null
    $bestVersion = [version]'0.0'
    $bestDescription = ''
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $versionText = (& $candidate --version 2>&1 | Select-Object -First 1 | Out-String).Trim()
        if ($versionText -match '(\d+\.\d+(?:\.\d+)?)') {
            $detectedVersion = [version]$Matches[1]
            if ($detectedVersion -gt $bestVersion) {
                $best = $candidate
                $bestVersion = $detectedVersion
                $bestDescription = $versionText
            }
        }
    }
    if (-not $best) {
        Fail "$name was installed but is not discoverable. Reopen your terminal (or restart Windows) and rerun build.bat."
    }
    if ($bestVersion -lt $minimum) {
        Fail "$name reports '$bestDescription' but requires >= $minimum. Update it or remove outdated PATH entries."
    }
    $env:PATH = (Split-Path $best -Parent) + ';' + $env:PATH
    Say "${name}: $bestDescription"
    return $best
}
function Is-ManagedPath([string]$path, [string]$name) {
    return ([IO.Path]::GetFullPath($path).TrimEnd('\') -ieq
        [IO.Path]::GetFullPath((Join-Path $managed $name)).TrimEnd('\'))
}
function Ensure-Repository([string]$path, [string]$url, [bool]$canUpdate) {
    if (-not (Test-Path -LiteralPath $path)) {
        if ($offline) { Fail "$path is missing and ASC_OFFLINE=1" }
        New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
        Exec $script:git @('clone', '--recurse-submodules', $url, $path)
    } elseif (-not (Test-Path -LiteralPath (Join-Path $path '.git'))) {
        if ($canUpdate) { Fail "$path exists but is not a Git checkout. Refusing to overwrite it." }
        Say "Using external, non-Git dependency path without updating: $path"
        return
    }
    if ($upgrade -and $canUpdate -and -not $offline) {
        # --ff-only protects local commits/branches from implicit rebasing or resetting.
        Exec $script:git @('-C', $path, 'pull', '--ff-only')
        Exec $script:git @('-C', $path, 'submodule', 'update', '--init', '--recursive')
    } elseif ((Test-Path -LiteralPath (Join-Path $path '.git')) -and -not $offline -and $canUpdate) {
        Exec $script:git @('-C', $path, 'submodule', 'update', '--init', '--recursive')
    }
}
function Find-SdkLib([string]$sdk) {
    $dir = Join-Path $sdk 'output\lib'
    if (-not (Test-Path -LiteralPath $dir)) { return $null }
    # Upstream Plugin-SDK's plugin_sa project produces Plugin.lib in Release.
    # Never accidentally link Plugin_d.lib from zDebug.
    $exact = Join-Path $dir 'Plugin.lib'
    if (Test-Path -LiteralPath $exact -PathType Leaf) { return $exact }
    return $null
}
function Find-VsWhere {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )
    return Resolve-Executable 'vswhere.exe' $candidates
}
function Find-Vs([string]$vswhere) {
    if (-not $vswhere) { return $null }
    $paths = @(& $vswhere '-latest' '-products' '*' '-version' '[18.0,19.0)' '-requires' 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' '-property' 'installationPath' 2>$null)
    foreach ($candidate in $paths) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'MSBuild\Current\Bin\MSBuild.exe'))) {
            return $candidate
        }
    }
    return $null
}
function Resolve-VS {
    $vswhere = Find-VsWhere
    $vs = Find-Vs $vswhere
    if (-not $vs) {
        Say "Visual Studio 2026 x86/x64 C++ build tools not detected."
        if ($offline) { Fail "Install VS 2026 Desktop development with C++ / x86 tools before offline build." }
        $otherVS = $null
        if ($vswhere) {
            $otherVS = (& $vswhere '-latest' '-products' '*' '-version' '[18.0,19.0)' '-property' 'installationPath' 2>$null | Select-Object -First 1)
        }
        if ($otherVS) {
            # Do not install a duplicate VS instance when the user's existing
            # VS 2026 simply lacks the C++ workload.
            $product = (& $vswhere '-latest' '-products' '*' '-version' '[18.0,19.0)' '-property' 'productId' 2>$null | Select-Object -First 1)
            $workload = if ($product -eq 'Microsoft.VisualStudio.Product.BuildTools') {
                'Microsoft.VisualStudio.Workload.VCTools'
            } else {
                'Microsoft.VisualStudio.Workload.NativeDesktop'
            }
            $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
            if (-not (Test-Path -LiteralPath $installer)) {
                Fail "The existing VS 2026 is missing C++ tools. Open Visual Studio Installer and add Desktop development with C++."
            }
            Say "Adding missing $workload to existing VS 2026..."
            Exec $installer @('modify', '--installPath', $otherVS, '--add', $workload,
                '--includeRecommended', '--quiet', '--norestart')
        } else {
            if (-not $wingetReady) {
                Fail "Install VS 2026 with Desktop development with C++ via Visual Studio Installer."
            }
            $ok = Exec-AllowFailure 'winget.exe' @(
                'install', '--id', 'Microsoft.VisualStudio.BuildTools', '-e',
                '--source', 'winget', '--accept-package-agreements',
                '--accept-source-agreements',
                '--override', '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
            )
            if (-not $ok) { Fail "VS Build Tools installation failed. Use Visual Studio Installer to add 'Desktop development with C++'." }
        }
        $vswhere = Find-VsWhere
        $vs = Find-Vs $vswhere
        if (-not $vs) {
            Fail "VS 2026 C++ tools were installed but are not visible. Restart Windows if requested, then rerun."
        }
    }
    Say "VS 2026 toolchain: $vs"
    if ($upgrade -and -not $offline -and $wingetReady -and $vswhere) {
        $product = (& $vswhere '-latest' '-products' '*' '-version' '[18.0,19.0)' '-requires' 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' '-property' 'productId' 2>$null | Select-Object -First 1)
        $package = switch ($product) {
            'Microsoft.VisualStudio.Product.BuildTools' {'Microsoft.VisualStudio.BuildTools'}
            'Microsoft.VisualStudio.Product.Community' {'Microsoft.VisualStudio.Community'}
            'Microsoft.VisualStudio.Product.Professional' {'Microsoft.VisualStudio.Professional'}
            'Microsoft.VisualStudio.Product.Enterprise' {'Microsoft.VisualStudio.Enterprise'}
            default {$null}
        }
        if ($package) {
            Say "Checking $package for updates via winget..."
            Winget-Package $package $true
        }
    }
    # Visual Studio installers can report success but require restart.
    $vswhere = Find-VsWhere
    $vs = Find-Vs $vswhere
    if (-not $vs) { Fail 'VS 2026 build tools unavailable after update. Restart Windows and rerun.' }
    return (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe')
}
function Build-Sdk([string]$sdk, [string]$msbuild) {
    $premake = Join-Path $sdk 'tools\premake\premake5.exe'
    $premakeLua = Join-Path $sdk 'tools\premake\premake5.lua'
    if (-not (Test-Path -LiteralPath $premake)) {
        Fail "Plugin-SDK Premake not found: $premake"
    }
    if (-not (Test-Path -LiteralPath $premakeLua)) {
        Fail "Plugin-SDK premake5.lua not found: $premakeLua"
    }
    $env:PLUGIN_SDK_DIR = $sdk
    Exec $premake @('vs2026', "--file=$premakeLua")
    $solution = Join-Path $sdk 'plugin.slnx'
    if (-not (Test-Path -LiteralPath $solution)) {
        $solution = Join-Path $sdk 'plugin.sln'
    }
    if (-not (Test-Path -LiteralPath $solution)) {
        Fail "Plugin-SDK solution was not generated."
    }
    Exec $msbuild @($solution, '/target:plugin_sa', '/property:Configuration=Release',
        '/property:Platform=Mixed Platforms', '/maxcpucount', '/verbosity:minimal')
    $library = Find-SdkLib $sdk
    if (-not $library) {
        Fail "Plugin-SDK built, but output\lib\Plugin.lib was not found. Inspect its release output."
    }
    return $library
}
function Ensure-Vcpkg([string]$root, [bool]$canUpdate) {
    $bootstrap = Join-Path $root 'bootstrap-vcpkg.bat'
    if (-not (Test-Path -LiteralPath $bootstrap)) { Fail "Not a vcpkg checkout: $root" }
    $exe = Join-Path $root 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $exe) -or ($upgrade -and $canUpdate)) {
        if ($offline -and -not (Test-Path -LiteralPath $exe)) { Fail "vcpkg.exe missing in offline mode" }
        Exec 'cmd.exe' @('/d','/c', ('""' + $bootstrap + '" -disableMetrics"'))
    }
    if (-not (Test-Path -LiteralPath $exe)) { Fail "vcpkg bootstrap did not create vcpkg.exe" }
    $env:VCPKG_ROOT = $root
    if ($upgrade -and $canUpdate -and -not $offline) {
        # Classic mode: upgrade installed ports to versions in the current registry.
        # 'upgrade' may indicate there is nothing to do; install below is authoritative.
        $ok = Exec-AllowFailure $exe @('upgrade','--no-dry-run')
        if (-not $ok) { Say "WARNING: vcpkg upgrade was incomplete; retrying MinHook install." }
    }
    Exec $exe @('install', 'minhook:x86-windows-static')
    $toolchain = Join-Path $root 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path -LiteralPath $toolchain)) { Fail "vcpkg toolchain not found" }
    return $toolchain
}

try {
    Say 'Automatic VS 2026 dependency bootstrap'
    if ($offline) { Say 'Offline mode: no download/update attempts' }
    if (-not $upgrade) { Say 'Version upgrades disabled (ASC_SKIP_UPGRADES=1)' }

    $wingetReady = [bool](Get-Command 'winget.exe' -ErrorAction SilentlyContinue)
    if (-not $wingetReady) { Say "winget unavailable: existing dependencies can be reused, but missing tools cannot be installed." }

    $gitPaths = @(
        (Join-Path $env:ProgramFiles 'Git\cmd\git.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Git\cmd\git.exe')
    )
    $cmakePaths = @(
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\CMake\bin\cmake.exe')
    )
    $git = Ensure-Binary 'git.exe' 'Git.Git' $gitPaths ([version]'2.30')
    $cmake = Ensure-Binary 'cmake.exe' 'Kitware.CMake' $cmakePaths ([version]'4.2.0')
    $msbuild = Resolve-VS

    if (-not $PluginSdkRoot) { $PluginSdkRoot = $env:PLUGIN_SDK_ROOT }
    if (-not $PluginSdkSaLib) { $PluginSdkSaLib = $env:PLUGIN_SDK_SA_LIB }
    if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $PluginSdkRoot) { $PluginSdkRoot = Join-Path $managed 'plugin-sdk' }
    if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $managed 'vcpkg' }

    $sdkManaged = Is-ManagedPath $PluginSdkRoot 'plugin-sdk'
    $vcpkgManaged = Is-ManagedPath $VcpkgRoot 'vcpkg'
    Ensure-Repository $PluginSdkRoot 'https://github.com/DK22Pac/plugin-sdk.git' ($sdkManaged -or $updateExternal)
    if (-not (Test-Path -LiteralPath (Join-Path $PluginSdkRoot 'plugin_sa\game_sa\CStreaming.h'))) {
        Fail "Invalid Plugin-SDK root: $PluginSdkRoot"
    }
    Ensure-Repository $VcpkgRoot 'https://github.com/microsoft/vcpkg.git' ($vcpkgManaged -or $updateExternal)

    if ($PluginSdkSaLib) {
        if (-not (Test-Path -LiteralPath $PluginSdkSaLib -PathType Leaf)) {
            Fail "Explicit Plugin-SDK library path does not exist: $PluginSdkSaLib"
        }
        $sdkLib = [IO.Path]::GetFullPath($PluginSdkSaLib)
        Say "Using explicitly provided SDK library: $sdkLib (not automatically replaced)."
    } else {
        # Always perform an incremental Release SDK build; this covers both
        # first-time installation and updates of the managed checkout.
        $sdkLib = Build-Sdk $PluginSdkRoot $msbuild
    }

    $toolchain = Ensure-Vcpkg $VcpkgRoot ($vcpkgManaged -or $updateExternal)

    # Independent 64-bit CPU asset cache. Never links or injects into GTA.
    $build64 = Join-Path $project 'build-vs2026-x64'
    Exec $cmake @('-S', $project, '-B', $build64, '-G', 'Visual Studio 18 2026',
        '-A', 'x64', '-DASC_BUILD_ASI=OFF', '-DASC_BUILD_TESTS=OFF',
        '-DCMAKE_CXX_STANDARD=23')
    # Build only the independent helper target; do not compile unrelated x64 core.
    Exec $cmake @('--build', $build64, '--config', 'Release',
        '--target', 'DistantStreaming_AssetCache64', '--parallel')
    $helper64 = Join-Path $build64 'Release\DistantStreaming_AssetCache64.exe'
    if (-not (Test-Path -LiteralPath $helper64)) {
        Fail "Missing required x64 asset cache executable: $helper64"
    }
    # Make the very next build.bat invocation reuse this exact x64 binary.
    . (Join-Path $PSScriptRoot 'cache64-build-state.ps1')
    Save-Cache64BuildState -Project $project -Build64 $build64 -Executable $helper64
    # GTA ASI stays Win32. Keep original Plugin-SDK and vcpkg flow.
    $build = Join-Path $project 'build-vs2026-win32'
    $dist = Join-Path $project 'dist'
    Exec $cmake @('-S', $project, '-B', $build, '-G', 'Visual Studio 18 2026',
        '-A', 'Win32', "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DPLUGIN_SDK_ROOT=$PluginSdkRoot", "-DPLUGIN_SDK_SA_LIB=$sdkLib",
        "-DVCPKG_TARGET_TRIPLET=x86-windows-static",
        '-DASC_BUILD_ASI=ON', '-DASC_BUILD_TESTS=OFF', '-DCMAKE_CXX_STANDARD=23')
    Exec $cmake @('--build', $build, '--config', 'Release', '--parallel')
    # Tests are opt-in; build.bat only builds the Win32 game plugin.
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    $asi = Join-Path $build 'Release\DistantStreaming.asi'
    $ini = Join-Path $project 'config\DistantStreaming.ini'
    if (-not (Test-Path -LiteralPath $asi)) { Fail "ASI output not found at $asi" }
    # Retire only obsolete artifacts in this project's generated dist/
    # directory. Never delete any files in a user's GTA installation.
    # This prevents accidentally copying two ASIs which hook the same engine.
    foreach ($legacyName in @(
        'AdaptiveStreaming.asi', 'AdaptiveStreaming.ini',
        'AdaptiveStreaming.pdb', 'AdaptiveStreaming.map',
        'ASC_AssetCache64.exe', 'ASC_HDService64.exe')) {
        $legacyPath = Join-Path $dist $legacyName
        if (Test-Path -LiteralPath $legacyPath) {
            Remove-Item -LiteralPath $legacyPath -Force
        }
    }
    Copy-Item -LiteralPath $helper64 -Destination (Join-Path $dist 'DistantStreaming_AssetCache64.exe') -Force
    Copy-Item -LiteralPath $asi -Destination (Join-Path $dist 'DistantStreaming.asi') -Force
    # Keep matching symbols private/local; the .map file resolves crash RVAs
    # without requiring a live debugger, while the .pdb supports WinDbg.
    foreach ($symbols in @('DistantStreaming.pdb', 'DistantStreaming.map')) {
        $source = Join-Path (Join-Path $build 'Release') $symbols
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $dist $symbols) -Force
        }
    }
    Copy-Item -LiteralPath $ini -Destination (Join-Path $dist 'DistantStreaming.ini') -Force
    Say "SUCCESS: $dist\DistantStreaming.asi + $dist\DistantStreaming_AssetCache64.exe (x64 CPU asset cache)"
    Say 'In-game validation is still required; no executable CRC32 is needed.'
    exit 0
} catch {
    Write-Host ''
    Write-Host ('[ERROR] ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'See docs\BUILD.md for troubleshooting.'
    exit 1
}
