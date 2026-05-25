#pragma once
#include <msgpack.hpp>
#include <cstdint>


constexpr const char* XAUTO_COMPAT_VERSION = "Axon_MCP"; // TODO: externalize


class XAutoErrorResponse {
public:
    std::string success;
    std::string error_string;
    MSGPACK_DEFINE(success, error_string);
};

void get_debugger_pid(msgpack::sbuffer& response_buffer);
void get_compat_v(msgpack::sbuffer& response_buffer);
void get_debugger_version(msgpack::sbuffer& response_buffer);
void dbg_eval(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_cmd_exec_direct(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_is_running(msgpack::sbuffer& response_buffer);
void dbg_is_debugging(msgpack::sbuffer& response_buffer);
void dbg_is_elevated(msgpack::sbuffer& response_buffer);
void dbg_memmap(msgpack::sbuffer& response_buffer);
void dbg_get_bitness(msgpack::sbuffer& response_buffer);
void gui_refresh_views(msgpack::sbuffer& response_buffer);

typedef std::tuple<size_t, size_t, uint32_t, uint16_t, size_t, uint32_t, uint32_t, uint32_t, std::string> MemPageTup;
void dbg_memmap(msgpack::sbuffer& response_buffer);
void dbg_read_memory(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_write_memory(msgpack::object root, msgpack::sbuffer& response_buffer);


typedef std::tuple<uint16_t, uint16_t, uint16_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t> x87fpuTup;
typedef std::tuple<
    size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, // gp regs
    size_t, size_t, // ip, flags
    uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, // segs
    size_t, size_t, size_t, size_t, size_t, size_t, // dregs
    std::array<uint8_t, 80>,
    x87fpuTup,
    uint32_t,
    std::array<uint8_t, 64 * 32>
> CtxTup64;
typedef std::tuple<
    size_t, size_t, size_t, size_t, size_t, size_t, size_t, size_t, // gp regs
    size_t, size_t, // ip, flags
    uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, // segs
    size_t, size_t, size_t, size_t, size_t, size_t, // dregs
    std::array<uint8_t, 80>,
    x87fpuTup,
    uint32_t,
    std::array<uint8_t, 64 * 8>
> CtxTup32;
typedef std::tuple<bool, bool, bool, bool, bool, bool, bool, bool, bool> FlagsTup;
typedef std::tuple<std::array<uint8_t, 10>, size_t, size_t> FpuRegsTup;
typedef std::array<FpuRegsTup, 8> FpuRegsArr;
typedef std::array<uint64_t, 8> MmxArr;
typedef std::tuple<bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, uint16_t> MxcsrFieldsTup;
typedef std::tuple<bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, bool, uint16_t> x87StatusWordFieldsTup;
typedef std::tuple<bool, bool, bool, bool, bool, bool, bool, bool, uint16_t, uint16_t> x87ControlWordFieldsTup;
void dbg_read_regs(msgpack::sbuffer& response_buffer);
void dbg_read_setting_sz(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_write_setting_sz(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_read_setting_uint(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_write_setting_uint(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_is_valid_read_ptr(msgpack::object root, msgpack::sbuffer& response_buffer);

typedef std::tuple<std::string, size_t, size_t, size_t, size_t, size_t> DisasmArgTup;
typedef std::tuple<std::string, size_t, size_t, size_t, std::array<DisasmArgTup, 3>> DisasmTup;
void disassemble_at(msgpack::object root, msgpack::sbuffer& response_buffer);
void assemble_at(msgpack::object root, msgpack::sbuffer& response_buffer);

typedef std::tuple<
    size_t, size_t, bool, bool, bool, std::string, std::string, 
    uint16_t, uint8_t, uint8_t, size_t, bool, bool, std::string, 
    std::string, std::string, std::string, std::string> BpxTup;
void get_breakpoints(msgpack::object root, msgpack::sbuffer& response_buffer);
void get_label_at(msgpack::object root, msgpack::sbuffer& response_buffer);
void get_comment_at(msgpack::object root, msgpack::sbuffer& response_buffer);
void get_symbol_at(msgpack::object root, msgpack::sbuffer& response_buffer);
std::wstring get_session_filename(size_t session_pid);

void dbg_get_tls_callbacks(msgpack::sbuffer& response_buffer);
void dbg_virtual_protect_ex(msgpack::object root, msgpack::sbuffer& response_buffer);
void dbg_suspend_all_threads(msgpack::sbuffer& response_buffer);
void dbg_get_peb(msgpack::sbuffer& response_buffer);
void dbg_get_process_info(msgpack::sbuffer& response_buffer);

typedef std::tuple<size_t, size_t, size_t, std::string> CallStackEntryTup;
void dbg_get_callstack(msgpack::sbuffer& response_buffer);

// Thread info: threadId, startAddress, localBase, cip, suspendCount, priority, waitReason, lastError, name
typedef std::tuple<uint32_t, size_t, size_t, size_t, uint32_t, uint32_t, uint32_t, uint32_t, std::string> ThreadInfoTup;
void dbg_get_threads(msgpack::sbuffer& response_buffer);

// Xref record: addr, type
typedef std::tuple<size_t, uint32_t> XrefRecordTup;
void dbg_get_xrefs(msgpack::object root, msgpack::sbuffer& response_buffer);

// Function info: start, end, instructionCount, manual
typedef std::tuple<size_t, size_t, size_t, bool> FunctionInfoTup;
void dbg_get_function(msgpack::object root, msgpack::sbuffer& response_buffer);

// CFG node: start, end, brtrue, brfalse, icount, terminal, split, indirectcall, exits[], instrs[]
typedef std::tuple<size_t, size_t, size_t, size_t, size_t, bool, bool, bool, std::vector<size_t>, std::vector<std::tuple<size_t, std::string>>> CfgNodeTup;
// instr tuple: addr, hex_bytes
typedef std::tuple<size_t, std::vector<uint8_t>> CfgInstrTup;
void dbg_analyze_function(msgpack::object root, msgpack::sbuffer& response_buffer);

void dbg_get_string(msgpack::object root, msgpack::sbuffer& response_buffer);

// Patch info: addr, oldbyte, newbyte
typedef std::tuple<size_t, uint8_t, uint8_t> PatchInfoTup;
void dbg_get_patches(msgpack::sbuffer& response_buffer);

// Module info: base, size, entry, sectionCount, name, path
typedef std::tuple<size_t, size_t, size_t, int, std::string, std::string> ModuleInfoTup;
void dbg_get_modules(msgpack::sbuffer& response_buffer);

// SEH record: addr, handler
typedef std::tuple<size_t, size_t> SehRecordTup;
void dbg_get_seh_chain(msgpack::sbuffer& response_buffer);

// Handle info: handle, typeNumber, grantedAccess
typedef std::tuple<size_t, uint8_t, uint32_t> HandleInfoTup;
void dbg_get_handles(msgpack::sbuffer& response_buffer);
