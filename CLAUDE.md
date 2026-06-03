# x64dbg-automate

x64dbg plugin that exposes a ZMQ-based automation server for remote control of the debugger.

## Build

**Always use `build.ps1`.** It is the canonical, proven build path — it locates
`vcvarsall.bat`, imports the full MSVC environment, points CMake at the project's
vcpkg toolchain, builds, and (with `-Deploy`) copies the plugin into the x64dbg
plugins folders. Do NOT invoke `cmake --build` by hand: a raw build relies on a
pre-existing cache and skips the vcpkg/MSVC environment setup, and it will not
deploy or build the 32-bit plugin.

```powershell
# 64-bit Release build + deploy to the x64dbg plugins folder
.\build.ps1 -Arch x64 -Deploy

# 32-bit Release build + deploy
.\build.ps1 -Arch x86 -Deploy

# Faster rebuild when the build dir is already configured (skip cmake configure)
.\build.ps1 -Arch x64 -Deploy -NoConfigure
```

Notes for running it from a tool:
- Run it with the **PowerShell** tool, not Bash (the `.\build.ps1` path mangles
  through a bash→powershell hop). Do **not** append `2>&1` — the script sets
  `$ErrorActionPreference='Stop'`, and redirecting cmake/cmkr's stderr banner
  turns it into a thrown error even on a successful build.
- A change to any `src/*.cpp` is shared by both arches — build **both** x64 and
  x86 so the `.dp64` and `.dp32` stay in sync.

Outputs: `build64\Release\x64dbg-automate.dp64`, `build32\Release\x64dbg-automate.dp32`.

`-Deploy` copies the plugin **and** `libzmq-mt-4_3_5.dll` to
`C:\Dev\RE_Tools\snapshot_2025-08-19_19-40\release\{x64,x32}\plugins`
(override by setting `X64DBG_PATH` to a release dir containing `x64\` and `x32\`).
After deploying, restart x64dbg/x32dbg (or use its plugin-reload) to load the new build.

## Project structure

- `src/plugin.cpp` — Plugin callbacks, menu UI, event publishing
- `src/xauto_server.cpp` / `.h` — ZMQ server, session management, command handling
- `src/pluginmain.cpp` / `.h` — Plugin entry points and SDK boilerplate
- x64dbg plugin SDK is fetched via CMake into `build64/_deps/x64dbg-src/pluginsdk/`

## Settings

Stored in x64dbg.ini under `[XAutomate]`. Key settings:
- `Mode` — `"local"` (default) or `"remote"`
- `BindAddress` — Bind address for remote mode (e.g. `0.0.0.0`)
- `ReqRepPort` / `PubSubPort` — Fixed ports for remote mode (0 = random, local only)
