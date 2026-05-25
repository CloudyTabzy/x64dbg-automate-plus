# x64dbg Automate Plus — ZMQ RPC Server

> The C++ x64dbg plugin that powers the [x64dbg Automate Plus](https://github.com/CloudyTabzy/x64dbg-automate-pyclient-plus) AI-native runtime analysis platform.

This plugin exposes x64dbg's full debugging, analysis, and introspection capabilities over a **ZeroMQ RPC server** with **MessagePack** serialization. It is the backbone that lets LLM agents and automation scripts control the debugger with structured commands instead of raw GUI interactions.

---

## What's New (Plus Edition)

The Plus edition significantly expands the original RPC surface from ~30 to **40+ commands**, adding deep runtime introspection capabilities required for automated reverse engineering:

| New Command | SDK Source | What It Returns |
|-------------|-----------|-----------------|
| `XAUTO_REQ_DBG_GET_CALLSTACK` | `GetCallStackEx` | Call stack entries with address, from, to, comment |
| `XAUTO_REQ_DBG_GET_THREADS` | `DbgGetThreadList` | Thread inventory: CIP, start address, priority, wait reason |
| `XAUTO_REQ_DBG_GET_XREFS` | `DbgXrefGet` | Typed cross-references (CALL / JMP / DATA) |
| `XAUTO_REQ_DBG_GET_FUNCTION` | `Script::Function::GetInfo` | Function start, end, instruction count |
| `XAUTO_REQ_DBG_ANALYZE_FUNCTION` | `DbgAnalyzeFunction` | **Full CFG** — basic blocks, branch targets, instruction bytes |
| `XAUTO_REQ_DBG_GET_STRING` | `DbgGetStringAt` | Auto-detected ASCII/Unicode strings |
| `XAUTO_REQ_DBG_GET_PATCHES` | `PatchEnum` | All debugger memory modifications |
| `XAUTO_REQ_DBG_GET_MODULES` | `Script::Module::GetList` | Loaded modules with base, size, entry, path |
| `XAUTO_REQ_DBG_GET_SEH_CHAIN` | `DbgGetSEHChain` | Structured exception handler chain |
| `XAUTO_REQ_DBG_GET_HANDLES` | `DbgGetHandles` | Open handles with type number and granted access |

These commands map 1:1 to the Python client's `api_analysis` and `api_memory` MCP tools, providing structured JSON responses instead of raw text output.

---

## Architecture

```
┌──────────────────┐
│  x64dbg GUI      │
│  (user can still │
│   interact)      │
└────────┬─────────┘
         │
┌────────▼─────────┐     ZMQ (req/rep + pub/sub)    ┌─────────────────┐
│  x64dbg-automate │◄───────────────────────────────►│  Python Client  │
│  plugin (this)   │        MessagePack payloads      │  / LLM Agent    │
└──────────────────┘                                  └─────────────────┘
```

- **Req/Rep socket** — synchronous RPC commands
- **Pub/Sub socket** — debugger events (breakpoints, module loads, exceptions)

---

## Building

Requires **Visual Studio 2022** with C++ CMake support. The x64dbg plugin SDK is fetched automatically via CMake's `FetchContent`.

### 64-bit plugin (primary)

From a VS2022 Developer Command Prompt:

```powershell
cmake -B build64 -G "Visual Studio 17 2022" -A x64
cmake --build build64 --config Release
```

Output: `build64/Release/x64dbg-automate.dp64`

### 32-bit plugin

```powershell
cmake -B build32 -G "Visual Studio 17 2022" -A Win32
cmake --build build32 --config Release
```

Output: `build32/Release/x64dbg-automate.dp32`

### Quick rebuild (after source changes)

```powershell
cmake --build build64 --config Release
```

---

## Installation

1. Build the plugin (see above)
2. Copy `build64/Release/x64dbg-automate.dp64` to your x64dbg `plugins` folder
3. Restart x64dbg — the plugin auto-starts the ZMQ server on localhost

The Python client will automatically discover and connect to the running server.

---

## RPC Protocol

Requests are MessagePack-encoded tuples: `[command_id, arg1, arg2, ...]`

Responses are MessagePack-encoded tuples: `[status, result_data]` where `status` is `"ok"` or `"err"`.

### Adding New Commands

1. **Declare the constant** in `src/xauto_server.h`:
   ```cpp
   constexpr const char* XAUTO_REQ_MY_COMMAND = "XAUTO_REQ_MY_COMMAND";
   ```

2. **Implement the handler** in `src/xauto_cmd.cpp`:
   ```cpp
   std::tuple<bool, std::string> dbg_my_command(size_t arg1) {
       // Use x64dbg SDK APIs
       return { true, "result" };
   }
   ```

3. **Register the dispatch** in `src/xauto_server.cpp` (`_dispatch_cmd`):
   ```cpp
   if (cmd == XAUTO_REQ_MY_COMMAND) {
       auto [ok, res] = dbg_my_command(std::get<size_t>(args[0]));
       return { ok, res };
   }
   ```

4. **Add the Python wrapper** in `x64dbg_automate/commands_xauto.py`

See existing commands like `XAUTO_REQ_DBG_EVAL` for a complete example.

---

## Project Structure

| File | Purpose |
|------|---------|
| `src/plugin.cpp` / `.h` | Plugin callbacks, menu UI, event publishing |
| `src/xauto_server.cpp` / `.h` | ZMQ server, session management, command dispatch |
| `src/xauto_cmd.cpp` / `.h` | **RPC command implementations** (this is where new commands live) |
| `src/pluginmain.cpp` / `.h` | Plugin entry points and SDK boilerplate |
| `cmake/` | CMake modules and x64dbg SDK fetch logic |
| `build64/_deps/x64dbg-src/pluginsdk/` | Auto-fetched x64dbg SDK headers |

---

## Settings

Stored in `x64dbg.ini` under the `[XAutomate]` section:

| Key | Default | Description |
|-----|---------|-------------|
| `Mode` | `local` | `local` (localhost only) or `remote` (bind to `BindAddress`) |
| `BindAddress` | `127.0.0.1` | Address to bind in remote mode |
| `ReqRepPort` | `0` | Fixed req/rep port (0 = random, local only) |
| `PubSubPort` | `0` | Fixed pub/sub port (0 = random, local only) |

---

## Compatibility

| Component | Requirement |
|-----------|-------------|
| Build Tool | Visual Studio 2022 (v17) |
| CMake | 3.20+ |
| x64dbg | Latest release (Oct 2024+) |
| Compat Version | `"Axon_MCP"` |
| Protocol | ZeroMQ + MessagePack |

The plugin advertises `"Axon_MCP"` as its compatibility version. The Python client verifies this on connection to ensure feature parity.

---

## Contributing

This repository is the C++ server component. For MCP tool issues, Python client bugs, or workflow requests, see the [pyclient-plus repo](https://github.com/CloudyTabzy/x64dbg-automate-pyclient-plus).

Upstream: [dariushoule/x64dbg-automate](https://github.com/dariushoule/x64dbg-automate)
