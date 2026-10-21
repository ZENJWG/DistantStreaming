# One-command Windows build (Visual Studio 2026)

Double-click **build.bat**, or open a terminal at the project root:

```bat
build.bat
```

No paths are required. `build.bat` delegates dependency management to
`tools\bootstrap-deps.ps1` (Windows PowerShell 5.1). On an internet-connected
Windows 10/11 machine it will:

1. Find and check Git (2.30+), CMake (4.2+), and **VS 2026 x86 C++ tools**.
2. Use official Microsoft `winget` packages to install missing Git/CMake/VS
   and attempt to upgrade detected installations. If you already have VS 2026
   but lack the C++ workload, add it through **Visual Studio Installer**
   rather than creating a duplicate Visual Studio installation.
3. Clone Plugin-SDK from `https://github.com/DK22Pac/plugin-sdk` into
   `.deps\plugin-sdk`, and vcpkg from
   `https://github.com/microsoft/vcpkg` into `.deps\vcpkg`.
4. Fast-forward and update recursive submodules of **managed** checkouts on
   subsequent runs (does not reset dirty checkouts or change branches).
5. Regenerate Plugin-SDK's `plugin.slnx` using its upstream Premake VS 2026
   action and incrementally build its **plugin_sa** Release target, which
   produces `.deps\plugin-sdk\output\lib\Plugin.lib`.
6. Bootstrap vcpkg and install/update its **MinHook x86-windows-static** port.
7. Configure and compile `DistantStreaming.asi` with CMake's
   `Visual Studio 18 2026` generator and `-A Win32`.
8. Run CTest and copy the ASI plus INI to `dist`.

Build output:

```
dist\
  DistantStreaming.asi
  DistantStreaming.ini
```

## Reuse your own dependencies

Arguments are optional and positional:

```bat
build.bat "C:\plugin-sdk" "" "C:\vcpkg"
build.bat "C:\plugin-sdk" "C:\plugin-sdk\output\lib\Plugin.lib" "C:\vcpkg"
```

You can also specify `PLUGIN_SDK_ROOT`, `PLUGIN_SDK_SA_LIB`, and
`VCPKG_ROOT` environment variables. An explicit existing library path is
**never overwritten**. SDK and vcpkg Git checkouts supplied by the user are
not updated automatically by default. The plugin_sa target is still compiled
if you specify only the SDK folder but not a library file. vcpkg will install
MinHook if missing, even in an external installation.

If external vcpkg contains old MinHook, upgrading it requires opting in.
Managed `.deps` checkouts are automatically updated unless disabled.

## Optional switches (environment variables)

```bat
set ASC_SKIP_UPGRADES=1
build.bat
```

Use locally installed versions without automatic version upgrades. Missing
tools still install if internet and winget are available.

```bat
set ASC_OFFLINE=1
build.bat
```

No downloading/updating. Requires all dependencies already installed and
MinHook cached or present. Offline build may still attempt vcpkg install
against local package cache.

```bat
set ASC_UPDATE_EXTERNAL=1
build.bat "C:\plugin-sdk" "" "C:\vcpkg"
```

Opt in to `git pull --ff-only` for explicit Git checkouts and port upgrades
in the explicit vcpkg tree. This can introduce breaking upstream SDK changes.

**Security / reliability:** Dependency downloads come from official Git
remotes and Windows `winget` package manifests; installers may request admin
permissions or a reboot. The script never runs a third-party install command
directly from an unverified pasted URL. `winget upgrade` returns nonzero for
"already latest" on some versions; in that situation the script keeps the
usable installed version and warns. It cannot guarantee that external
unmanaged installations are at the absolute latest version.

**First build prerequisites:** PowerShell 5.1 and Microsoft's Windows App
Installer (`winget`). VS 2026 may already be present. The first Plugin-SDK
clone can occupy substantial disk space. If VS Installer reports a pending
reboot, restart and rerun `build.bat`.

**Runtime caveat:** A successful compile and CTest run does not verify GTA SA
engine ABI or in-game stability. The plugin no longer requires a configured CRC32 value; automatic PE/address checks remain. Always
test against unmodified SA 1.0 US before installing the ASI permanently.

## Troubleshooting

- `winget not found`: install/update Windows App Installer from Microsoft
  Store, then retry. Alternatively provide installed dependencies via PATH
  and command arguments.
- `CMake requires >=4.2`: reopen terminal after winget installation; confirm
  `cmake --version`, ensure older versions are not shadowing PATH.
- `VS 2026 C++ tools not detected`: Visual Studio Installer ->
  **Modify** -> **Desktop development with C++** -> **MSVC x86/x64**
  and Windows SDK. Rerun after required restart.
- `SDK build fails`: examine upstream `plugin.slnx` build errors. The SDK
  is a moving dependency; compatibility with this plugin must be checked.
- `Cannot find Plugin.lib`: run the SDK's `plugin_sa` Release target
  (`Mixed Platforms`), not the zDebug target.
- `MinHook not found`: check `vcpkg list` for `minhook:x86-windows-static`.
- Network or permission errors: run from an appropriate terminal, and
  review firewall, proxy, Windows Installer dialogs and logs.
