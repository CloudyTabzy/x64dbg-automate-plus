#include "xauto_cmd.h"
#include <pluginsdk/bridgemain.h>
#include <pluginsdk/bridgegraph.h>
#include "pluginmain.h"
#include <TlHelp32.h>
#include <Shlwapi.h>
#include <winternl.h>
#include <algorithm>


void get_debugger_pid(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, GetCurrentProcessId());
}

void get_compat_v(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, std::string(XAUTO_COMPAT_VERSION));
}

void get_debugger_version(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, BridgeGetDbgVersion());
}

void dbg_eval(msgpack::object root, msgpack::sbuffer& response_buffer) {
    bool success; 
    std::string cmd;

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::STR) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_EVAL", "Invalid or missing eval string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(cmd);
    size_t res = DbgEval(cmd.c_str(), &success);
    std::tuple<size_t, bool> out_tup(res, success);
    msgpack::pack(response_buffer, out_tup);
}

void dbg_cmd_exec_direct(msgpack::object root, msgpack::sbuffer& response_buffer) {
    std::string cmd;

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::STR) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_EVAL", "Invalid or missing command string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(cmd);
    msgpack::pack(response_buffer, DbgCmdExecDirect(cmd.c_str()));
}

void dbg_cmd_exec_direct_ex(msgpack::object root, msgpack::sbuffer& response_buffer) {
    std::string cmd;

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::STR) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_EVAL", "Invalid or missing command string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(cmd);

    // Run the command, then atomically read back x64dbg's $result variable.
    // Many commands (Find, eval, gpa, mod.*, etc.) write their primary output
    // into $result; capturing it here in one round-trip avoids a race where a
    // separate eval call could observe a $result mutated by another request.
    bool success = DbgCmdExecDirect(cmd.c_str());

    bool result_valid = false;
    duint result_value = DbgEval("$result", &result_valid);

    CmdExecExTup out(success, (size_t)result_value, result_valid);
    msgpack::pack(response_buffer, out);
}

void dbg_is_running(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, DbgIsRunning());
}

void dbg_is_debugging(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, DbgIsDebugging());
}

void dbg_is_elevated(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, BridgeIsProcessElevated());
}

void dbg_get_bitness(msgpack::sbuffer& response_buffer) {
    msgpack::pack(response_buffer, sizeof(void*) == 8 ? 64 : 32);
}

void dbg_memmap(msgpack::sbuffer& response_buffer) {
    MEMMAP mm;
    DbgMemMap(&mm);
    std::vector<MemPageTup> memmap_entry_vec;
    for (size_t i = 0; i < mm.count; i++) {
        auto bi = mm.page[i].mbi;
        memmap_entry_vec.push_back(MemPageTup(
            (size_t)bi.BaseAddress,
            (size_t)bi.AllocationBase,
            bi.AllocationProtect,
            #if defined (_WIN64)
            bi.PartitionId,
            #else
            0,
            #endif
            bi.RegionSize,
            bi.State,
            bi.Protect,
            bi.Type,
            std::string(mm.page[i].info)
        ));
    }
    msgpack::pack(response_buffer, memmap_entry_vec);
    BridgeFree(mm.page);
}

void gui_refresh_views(msgpack::sbuffer& response_buffer) {
    GuiUpdateAllViews();
    msgpack::pack(response_buffer, true);
}

void dbg_read_memory(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    size_t size;

    if(root.via.array.size < 3 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER || root.via.array.ptr[2].type != msgpack::type::POSITIVE_INTEGER) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_READ", "Invalid or missing memory read parameters"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    root.via.array.ptr[2].convert(size);

    std::vector<uint8_t> membuf(size);
    if(!DbgMemRead(addr, membuf.data(), size)) {
        XAutoErrorResponse resp_obj = {"XERROR_READ_FAILED", "Memory read failed"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    msgpack::pack(response_buffer, membuf);
}

void dbg_write_memory(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    std::vector<uint8_t> data;

    if(root.via.array.size < 3 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER || root.via.array.ptr[2].type != msgpack::type::BIN) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_WRITE", "Invalid or missing memory write parameters"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    root.via.array.ptr[2].convert(data);

    if(!DbgMemWrite(addr, data.data(), data.size())) {
        XAutoErrorResponse resp_obj = {"XERROR_WRITE_FAILED", "Memory write failed"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    msgpack::pack(response_buffer, true);
}

// Carbon copy of the non-exposed methods in BridegeMain.cpp
#define MXCSRFLAG_IE 0x1
#define MXCSRFLAG_DE 0x2
#define MXCSRFLAG_ZE 0x4
#define MXCSRFLAG_OE 0x8
#define MXCSRFLAG_UE 0x10
#define MXCSRFLAG_PE 0x20
#define MXCSRFLAG_DAZ 0x40
#define MXCSRFLAG_IM 0x80
#define MXCSRFLAG_DM 0x100
#define MXCSRFLAG_ZM 0x200
#define MXCSRFLAG_OM 0x400
#define MXCSRFLAG_UM 0x800
#define MXCSRFLAG_PM 0x1000
#define MXCSRFLAG_FZ 0x8000

static void GetMxCsrFields(MXCSRFIELDS* MxCsrFields, DWORD MxCsr)
{
    MxCsrFields->IE = ((MxCsr & MXCSRFLAG_IE) != 0);
    MxCsrFields->DE = ((MxCsr & MXCSRFLAG_DE) != 0);
    MxCsrFields->ZE = ((MxCsr & MXCSRFLAG_ZE) != 0);
    MxCsrFields->OE = ((MxCsr & MXCSRFLAG_OE) != 0);
    MxCsrFields->UE = ((MxCsr & MXCSRFLAG_UE) != 0);
    MxCsrFields->PE = ((MxCsr & MXCSRFLAG_PE) != 0);
    MxCsrFields->DAZ = ((MxCsr & MXCSRFLAG_DAZ) != 0);
    MxCsrFields->IM = ((MxCsr & MXCSRFLAG_IM) != 0);
    MxCsrFields->DM = ((MxCsr & MXCSRFLAG_DM) != 0);
    MxCsrFields->ZM = ((MxCsr & MXCSRFLAG_ZM) != 0);
    MxCsrFields->OM = ((MxCsr & MXCSRFLAG_OM) != 0);
    MxCsrFields->UM = ((MxCsr & MXCSRFLAG_UM) != 0);
    MxCsrFields->PM = ((MxCsr & MXCSRFLAG_PM) != 0);
    MxCsrFields->FZ = ((MxCsr & MXCSRFLAG_FZ) != 0);

    MxCsrFields->RC = (MxCsr & 0x6000) >> 13;
}

#define x87CONTROLWORD_FLAG_IM 0x1
#define x87CONTROLWORD_FLAG_DM 0x2
#define x87CONTROLWORD_FLAG_ZM 0x4
#define x87CONTROLWORD_FLAG_OM 0x8
#define x87CONTROLWORD_FLAG_UM 0x10
#define x87CONTROLWORD_FLAG_PM 0x20
#define x87CONTROLWORD_FLAG_IEM 0x80
#define x87CONTROLWORD_FLAG_IC 0x1000

static void Getx87ControlWordFields(X87CONTROLWORDFIELDS* x87ControlWordFields, WORD ControlWord)
{
    x87ControlWordFields->IM = ((ControlWord & x87CONTROLWORD_FLAG_IM) != 0);
    x87ControlWordFields->DM = ((ControlWord & x87CONTROLWORD_FLAG_DM) != 0);
    x87ControlWordFields->ZM = ((ControlWord & x87CONTROLWORD_FLAG_ZM) != 0);
    x87ControlWordFields->OM = ((ControlWord & x87CONTROLWORD_FLAG_OM) != 0);
    x87ControlWordFields->UM = ((ControlWord & x87CONTROLWORD_FLAG_UM) != 0);
    x87ControlWordFields->PM = ((ControlWord & x87CONTROLWORD_FLAG_PM) != 0);
    x87ControlWordFields->IEM = ((ControlWord & x87CONTROLWORD_FLAG_IEM) != 0);
    x87ControlWordFields->IC = ((ControlWord & x87CONTROLWORD_FLAG_IC) != 0);

    x87ControlWordFields->RC = ((ControlWord & 0xC00) >> 10);
    x87ControlWordFields->PC = ((ControlWord & 0x300) >> 8);
}

#define x87STATUSWORD_FLAG_I 0x1
#define x87STATUSWORD_FLAG_D 0x2
#define x87STATUSWORD_FLAG_Z 0x4
#define x87STATUSWORD_FLAG_O 0x8
#define x87STATUSWORD_FLAG_U 0x10
#define x87STATUSWORD_FLAG_P 0x20
#define x87STATUSWORD_FLAG_SF 0x40
#define x87STATUSWORD_FLAG_ES 0x80
#define x87STATUSWORD_FLAG_C0 0x100
#define x87STATUSWORD_FLAG_C1 0x200
#define x87STATUSWORD_FLAG_C2 0x400
#define x87STATUSWORD_FLAG_C3 0x4000
#define x87STATUSWORD_FLAG_B 0x8000

static void Getx87StatusWordFields(X87STATUSWORDFIELDS* x87StatusWordFields, WORD StatusWord)
{
    x87StatusWordFields->I = ((StatusWord & x87STATUSWORD_FLAG_I) != 0);
    x87StatusWordFields->D = ((StatusWord & x87STATUSWORD_FLAG_D) != 0);
    x87StatusWordFields->Z = ((StatusWord & x87STATUSWORD_FLAG_Z) != 0);
    x87StatusWordFields->O = ((StatusWord & x87STATUSWORD_FLAG_O) != 0);
    x87StatusWordFields->U = ((StatusWord & x87STATUSWORD_FLAG_U) != 0);
    x87StatusWordFields->P = ((StatusWord & x87STATUSWORD_FLAG_P) != 0);
    x87StatusWordFields->SF = ((StatusWord & x87STATUSWORD_FLAG_SF) != 0);
    x87StatusWordFields->ES = ((StatusWord & x87STATUSWORD_FLAG_ES) != 0);
    x87StatusWordFields->C0 = ((StatusWord & x87STATUSWORD_FLAG_C0) != 0);
    x87StatusWordFields->C1 = ((StatusWord & x87STATUSWORD_FLAG_C1) != 0);
    x87StatusWordFields->C2 = ((StatusWord & x87STATUSWORD_FLAG_C2) != 0);
    x87StatusWordFields->C3 = ((StatusWord & x87STATUSWORD_FLAG_C3) != 0);
    x87StatusWordFields->B = ((StatusWord & x87STATUSWORD_FLAG_B) != 0);

    x87StatusWordFields->TOP = ((StatusWord & 0x3800) >> 11);
}

// Definitions From TitanEngine
#define Getx87r0PositionInRegisterArea(STInTopStack) ((8 - STInTopStack) % 8)
#define Calculatex87registerPositionInRegisterArea(x87r0_position, index) (((x87r0_position + index) % 8))
#define GetRegisterAreaOf87register(register_area, x87r0_position, index) (((char *) register_area) + 10 * Calculatex87registerPositionInRegisterArea(x87r0_position, index) )
#define GetSTValueFromIndex(x87r0_position, index) ((x87r0_position + index) % 8)


void dbg_read_regs(msgpack::sbuffer& response_buffer) {
    REGDUMP_AVX512 rd;

    // Calculated
    FLAGS flags;
    memset(&flags, 0, sizeof(flags));
    X87FPUREGISTER x87FPURegisters[8];
    memset(x87FPURegisters, 0, sizeof(x87FPURegisters));
    unsigned long long mmx[8];
    memset(mmx, 0, sizeof(mmx));
    MXCSRFIELDS MxCsrFields;
    memset(&MxCsrFields, 0, sizeof(MxCsrFields));
    X87STATUSWORDFIELDS x87StatusWordFields;
    memset(&x87StatusWordFields, 0, sizeof(x87StatusWordFields));
    X87CONTROLWORDFIELDS x87ControlWordFields;
    memset(&x87ControlWordFields, 0, sizeof(x87ControlWordFields));
    LASTERROR lastError;
    memset(&lastError, 0, sizeof(lastError));
    LASTSTATUS lastStatus;
    memset(&lastStatus, 0, sizeof(lastStatus));

    DbgGetRegDumpEx(&rd, sizeof(rd));
    GetMxCsrFields(&MxCsrFields, rd.regcontext.MxCsr);
    Getx87ControlWordFields(&x87ControlWordFields, rd.regcontext.x87fpu.ControlWord);
    Getx87StatusWordFields(&x87StatusWordFields, rd.regcontext.x87fpu.StatusWord);

    DWORD x87r0_position = Getx87r0PositionInRegisterArea(x87StatusWordFields.TOP);
    for(int i = 0; i < 8; i++)
    {
        memcpy(x87FPURegisters[i].data, GetRegisterAreaOf87register(rd.regcontext.RegisterArea, x87r0_position, i), 10);
        mmx[i] = *((uint64_t*)&x87FPURegisters[i].data);
        x87FPURegisters[i].st_value = GetSTValueFromIndex(x87r0_position, i);
        x87FPURegisters[i].tag = (int)((rd.regcontext.x87fpu.TagWord >> (i * 2)) & 0x3);
    }

    char fmtString[64] = "";
    auto pStringFormatInline = DbgFunctions()->StringFormatInline; // When called before dbgfunctionsinit() this can be NULL!
    lastError.code = rd.lastError;
    if(pStringFormatInline && sprintf_s(fmtString, _TRUNCATE, "{winerrorname@%X}", lastError.code) != -1)
    {
        pStringFormatInline(fmtString, sizeof(lastError.name), lastError.name);
    }
    else
    {
        memset(lastError.name, 0, sizeof(lastError.name));
    }

    lastStatus.code = rd.lastStatus;
    if(pStringFormatInline && sprintf_s(fmtString, _TRUNCATE, "{ntstatusname@%X}", lastStatus.code) != -1)
    {
        pStringFormatInline(fmtString, sizeof(lastStatus.name), lastStatus.name);
    }
    else
    {
        memset(lastStatus.name, 0, sizeof(lastStatus.name));
    }

    std::array<uint8_t, 80> reg_area;
    std::copy_n((uint8_t*)&rd.regcontext.RegisterArea[0], 80, reg_area.begin()); 

    #ifdef _WIN64
    std::array<uint8_t, 64 * 32> zmm_regs;
    std::copy_n((uint8_t*)&rd.regcontext.ZmmRegisters[0], 64 * 32, zmm_regs.begin()); 
    #else
    std::array<uint8_t, 64 * 8> zmm_regs;
    std::copy_n((uint8_t*)&rd.regcontext.ZmmRegisters[0], 64 * 8, zmm_regs.begin());
    #endif

    FpuRegsArr fpu_regs;
    for (size_t i = 0; i < 8; i++) {
        fpu_regs[i] = FpuRegsTup(
            std::array<uint8_t, 10>(),
            x87FPURegisters[i].st_value,
            x87FPURegisters[i].tag
        );
        std::copy_n((uint8_t*)&x87FPURegisters[i].data[0], 10, std::get<0>(fpu_regs[i]).begin()); 
    }

    std::array<uint8_t, 128> _lastError;
    std::copy_n((uint8_t*)&lastError.name[0], 128, _lastError.begin());
    std::array<uint8_t, 128> _lastStatus;
    std::copy_n((uint8_t*)&lastStatus.name[0], 128, _lastStatus.begin());

    #ifdef _WIN64
    std::tuple<
        size_t, 
        CtxTup64, 
        FlagsTup, 
        FpuRegsArr, 
        MmxArr,
        MxcsrFieldsTup,
        x87StatusWordFieldsTup,
        x87ControlWordFieldsTup,
        std::tuple<uint32_t, std::array<uint8_t, 128>>,
        std::tuple<uint32_t, std::array<uint8_t, 128>>
    > regdump(
        64,
        CtxTup64(
            rd.regcontext.cax, rd.regcontext.cbx, rd.regcontext.ccx, rd.regcontext.cdx, rd.regcontext.cbp, rd.regcontext.csp, rd.regcontext.csi, rd.regcontext.cdi,
            rd.regcontext.r8, rd.regcontext.r9, rd.regcontext.r10, rd.regcontext.r11, rd.regcontext.r12, rd.regcontext.r13, rd.regcontext.r14, rd.regcontext.r15,
            rd.regcontext.cip,
            rd.regcontext.eflags,
            rd.regcontext.cs, rd.regcontext.ds, rd.regcontext.es, rd.regcontext.fs, rd.regcontext.gs, rd.regcontext.ss,
            rd.regcontext.dr0, rd.regcontext.dr1, rd.regcontext.dr2, rd.regcontext.dr3, rd.regcontext.dr6, rd.regcontext.dr7,
            reg_area,
            x87fpuTup(
                rd.regcontext.x87fpu.ControlWord,
                rd.regcontext.x87fpu.StatusWord,
                rd.regcontext.x87fpu.TagWord,
                rd.regcontext.x87fpu.ErrorOffset,
                rd.regcontext.x87fpu.ErrorSelector,
                rd.regcontext.x87fpu.DataOffset,
                rd.regcontext.x87fpu.DataSelector,
                rd.regcontext.x87fpu.Cr0NpxState
            ),
            rd.regcontext.MxCsr,
            zmm_regs
        ),
        FlagsTup(flags.c, flags.p, flags.a, flags.z, flags.s, flags.t, flags.i, flags.d, flags.o),
        fpu_regs,
        MmxArr {mmx[0], mmx[1], mmx[2], mmx[3], mmx[4], mmx[5], mmx[6], mmx[7]},
        MxcsrFieldsTup(
            MxCsrFields.FZ, MxCsrFields.PM, MxCsrFields.UM, MxCsrFields.OM,
            MxCsrFields.ZM, MxCsrFields.IM, MxCsrFields.DM, MxCsrFields.DAZ,
            MxCsrFields.PE, MxCsrFields.UE, MxCsrFields.OE, MxCsrFields.ZE,
            MxCsrFields.DE, MxCsrFields.IE, MxCsrFields.RC
        ),
        x87StatusWordFieldsTup(
            x87StatusWordFields.B, x87StatusWordFields.C3, x87StatusWordFields.C2, x87StatusWordFields.C1, x87StatusWordFields.C0,
            x87StatusWordFields.ES, x87StatusWordFields.SF, x87StatusWordFields.P, x87StatusWordFields.U, x87StatusWordFields.O, 
            x87StatusWordFields.Z, x87StatusWordFields.D, x87StatusWordFields.I, x87StatusWordFields.TOP),
        x87ControlWordFieldsTup(
            x87ControlWordFields.IC, x87ControlWordFields.IEM, x87ControlWordFields.PM, x87ControlWordFields.UM, x87ControlWordFields.OM, 
            x87ControlWordFields.ZM, x87ControlWordFields.DM, x87ControlWordFields.IM, x87ControlWordFields.RC, x87ControlWordFields.PC),
        std::tuple<uint32_t, std::array<uint8_t, 128>>(lastError.code, _lastError),
        std::tuple<uint32_t, std::array<uint8_t, 128>>(lastStatus.code, _lastStatus)
    );
    #else
    std::tuple<
        size_t, 
        CtxTup32, 
        FlagsTup, 
        FpuRegsArr, 
        MmxArr,
        MxcsrFieldsTup,
        x87StatusWordFieldsTup,
        x87ControlWordFieldsTup,
        std::tuple<uint32_t, std::array<uint8_t, 128>>,
        std::tuple<uint32_t, std::array<uint8_t, 128>>
    > regdump(
        32,
        CtxTup32(
            rd.regcontext.cax, rd.regcontext.cbx, rd.regcontext.ccx, rd.regcontext.cdx, rd.regcontext.cbp, rd.regcontext.csp, rd.regcontext.csi, rd.regcontext.cdi,
            rd.regcontext.cip,
            rd.regcontext.eflags,
            rd.regcontext.cs, rd.regcontext.ds, rd.regcontext.es, rd.regcontext.fs, rd.regcontext.gs, rd.regcontext.ss,
            rd.regcontext.dr0, rd.regcontext.dr1, rd.regcontext.dr2, rd.regcontext.dr3, rd.regcontext.dr6, rd.regcontext.dr7,
            reg_area,
            x87fpuTup(
                rd.regcontext.x87fpu.ControlWord,
                rd.regcontext.x87fpu.StatusWord,
                rd.regcontext.x87fpu.TagWord,
                rd.regcontext.x87fpu.ErrorOffset,
                rd.regcontext.x87fpu.ErrorSelector,
                rd.regcontext.x87fpu.DataOffset,
                rd.regcontext.x87fpu.DataSelector,
                rd.regcontext.x87fpu.Cr0NpxState
            ),
            rd.regcontext.MxCsr,
            zmm_regs
        ),
        FlagsTup(flags.c, flags.p, flags.a, flags.z, flags.s, flags.t, flags.i, flags.d, flags.o),
        fpu_regs,
        MmxArr {mmx[0], mmx[1], mmx[2], mmx[3], mmx[4], mmx[5], mmx[6], mmx[7]},
        MxcsrFieldsTup(
            MxCsrFields.FZ, MxCsrFields.PM, MxCsrFields.UM, MxCsrFields.OM,
            MxCsrFields.ZM, MxCsrFields.IM, MxCsrFields.DM, MxCsrFields.DAZ,
            MxCsrFields.PE, MxCsrFields.UE, MxCsrFields.OE, MxCsrFields.ZE,
            MxCsrFields.DE, MxCsrFields.IE, MxCsrFields.RC
        ),
        x87StatusWordFieldsTup(
            x87StatusWordFields.B, x87StatusWordFields.C3, x87StatusWordFields.C2, x87StatusWordFields.C1, x87StatusWordFields.C0,
            x87StatusWordFields.ES, x87StatusWordFields.SF, x87StatusWordFields.P, x87StatusWordFields.U, x87StatusWordFields.O, 
            x87StatusWordFields.Z, x87StatusWordFields.D, x87StatusWordFields.I, x87StatusWordFields.TOP),
        x87ControlWordFieldsTup(
            x87ControlWordFields.IC, x87ControlWordFields.IEM, x87ControlWordFields.PM, x87ControlWordFields.UM, x87ControlWordFields.OM, 
            x87ControlWordFields.ZM, x87ControlWordFields.DM, x87ControlWordFields.IM, x87ControlWordFields.RC, x87ControlWordFields.PC),
        std::tuple<uint32_t, std::array<uint8_t, 128>>(lastError.code, _lastError),
        std::tuple<uint32_t, std::array<uint8_t, 128>>(lastStatus.code, _lastStatus)
    );
    #endif

    msgpack::pack(response_buffer, regdump);
}

void dbg_read_setting_sz(msgpack::object root, msgpack::sbuffer& response_buffer) {
    std::string section;
    std::string setting_name;

    if(root.via.array.size < 3 || root.via.array.ptr[1].type != msgpack::type::STR || root.via.array.ptr[2].type != msgpack::type::STR) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_SETTING", "Invalid or missing setting string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(section);
    root.via.array.ptr[2].convert(setting_name);

    char* setting_val = (char*)BridgeAlloc(MAX_SETTING_SIZE);
    bool res = BridgeSettingGet(section.c_str(), setting_name.c_str(), setting_val);
    msgpack::pack(response_buffer, std::tuple<bool, std::string>(res, std::string(setting_val)));
    BridgeFree(setting_val);
}

void dbg_write_setting_sz(msgpack::object root, msgpack::sbuffer& response_buffer) {
    std::string section;
    std::string setting_name;
    std::string setting_val;

    if( root.via.array.size < 3 || 
        root.via.array.ptr[1].type != msgpack::type::STR || 
        root.via.array.ptr[2].type != msgpack::type::STR || 
        root.via.array.ptr[3].type != msgpack::type::STR
    ) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_SETTING", "Invalid or missing setting string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(section);
    root.via.array.ptr[2].convert(setting_name);
    root.via.array.ptr[3].convert(setting_val);

    bool res = BridgeSettingSet(section.c_str(), setting_name.c_str(), setting_val.c_str());
    msgpack::pack(response_buffer, res);
}

void dbg_read_setting_uint(msgpack::object root, msgpack::sbuffer& response_buffer) {
    std::string section;
    std::string setting_name;

    if(root.via.array.size < 3 || root.via.array.ptr[1].type != msgpack::type::STR || root.via.array.ptr[2].type != msgpack::type::STR) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_SETTING", "Invalid or missing setting string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(section);
    root.via.array.ptr[2].convert(setting_name);

    duint setting_val;
    bool res = BridgeSettingGetUint(section.c_str(), setting_name.c_str(), &setting_val);
    msgpack::pack(response_buffer, std::tuple<bool, size_t>(res, setting_val));
}

void dbg_write_setting_uint(msgpack::object root, msgpack::sbuffer& response_buffer) {
    std::string section;
    std::string setting_name;
    size_t setting_val;

    if( root.via.array.size < 3 || 
        root.via.array.ptr[1].type != msgpack::type::STR || 
        root.via.array.ptr[2].type != msgpack::type::STR || 
        (root.via.array.ptr[3].type != msgpack::type::POSITIVE_INTEGER)
    ) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_SETTING", "Invalid or missing setting string"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(section);
    root.via.array.ptr[2].convert(setting_name);
    root.via.array.ptr[3].convert(setting_val);

    bool res = BridgeSettingSetUint(section.c_str(), setting_name.c_str(), setting_val);
    msgpack::pack(response_buffer, res);
}

void dbg_is_valid_read_ptr(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    msgpack::pack(response_buffer, DbgMemIsValidReadPtr(addr));
}

void disassemble_at(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    DISASM_INSTR instr;

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    DbgDisasmAt(addr, &instr);
    msgpack::pack(response_buffer, DisasmTup(
        std::string(instr.instruction),
        instr.argcount,
        instr.instr_size,
        instr.type,
        {
            DisasmArgTup(
                std::string(instr.arg[0].mnemonic),
                instr.arg[0].type,
                instr.arg[0].segment,
                instr.arg[0].constant,
                instr.arg[0].value,
                instr.arg[0].memvalue
            ),
            DisasmArgTup(
                std::string(instr.arg[1].mnemonic),
                instr.arg[1].type,
                instr.arg[1].segment,
                instr.arg[1].constant,
                instr.arg[1].value,
                instr.arg[1].memvalue
            ),
            DisasmArgTup(
                std::string(instr.arg[2].mnemonic),
                instr.arg[2].type,
                instr.arg[2].segment,
                instr.arg[2].constant,
                instr.arg[2].value,
                instr.arg[2].memvalue
            )
        }
    ));
}

void assemble_at(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    std::string instr;

    if(root.via.array.size < 3 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER || root.via.array.ptr[2].type != msgpack::type::STR) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    root.via.array.ptr[2].convert(instr);
    msgpack::pack(response_buffer, DbgAssembleAt(addr, instr.c_str()));
}

void get_breakpoints(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t bp_type;
    BPMAP bp_list;

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(bp_type);

    int n_bps = DbgGetBpList((BPXTYPE)bp_type, &bp_list);
    std::vector<BpxTup> bp_vec;
    for (int i = 0; i < n_bps; i++) {
        bp_vec.push_back(BpxTup(
            bp_list.bp[i].type,
            bp_list.bp[i].addr,
            bp_list.bp[i].enabled,
            bp_list.bp[i].singleshoot,
            bp_list.bp[i].active,
            std::string(bp_list.bp[i].name),
            std::string(bp_list.bp[i].mod),
            bp_list.bp[i].slot,
            bp_list.bp[i].typeEx,
            bp_list.bp[i].hwSize,
            bp_list.bp[i].hitCount,
            bp_list.bp[i].fastResume,
            bp_list.bp[i].silent,
            std::string(bp_list.bp[i].breakCondition),
            std::string(bp_list.bp[i].logText),
            std::string(bp_list.bp[i].logCondition),
            std::string(bp_list.bp[i].commandText),
            std::string(bp_list.bp[i].commandCondition)
        ));
    }
    
    BridgeFree(bp_list.bp);
    msgpack::pack(response_buffer, bp_vec);
}

void get_label_at(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    size_t sreg;
    char text[MAX_LABEL_SIZE];
    memset(text, 0, MAX_LABEL_SIZE);

    if(root.via.array.size < 3 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER || root.via.array.ptr[2].type != msgpack::type::POSITIVE_INTEGER) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    root.via.array.ptr[2].convert(sreg);
    bool res = DbgGetLabelAt(addr, (SEGMENTREG)sreg, text);
    msgpack::pack(response_buffer, std::tuple<bool, std::string>(res, std::string(text)));
}

void get_comment_at(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    char text[MAX_COMMENT_SIZE];
    memset(text, 0, MAX_COMMENT_SIZE);

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    bool res = DbgGetCommentAt(addr, text);
    msgpack::pack(response_buffer, std::tuple<bool, std::string>(res, std::string(text)));
}

void get_symbol_at(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    SYMBOLINFO* info = (SYMBOLINFO*)BridgeAlloc(sizeof(SYMBOLINFO));
    memset(info, 0, sizeof(SYMBOLINFO));

    if(root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        msgpack::pack(response_buffer, false);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    bool res = DbgGetSymbolInfoAt(addr, info);
    msgpack::pack(response_buffer, std::tuple<bool, size_t, std::string, std::string, size_t, size_t>(
        res,
        info->addr,
        info->decoratedSymbol ? std::string(info->decoratedSymbol) : std::string(),
        info->undecoratedSymbol ? std::string(info->undecoratedSymbol) : std::string(),
        info->type,
        info->ordinal
    ));

    if (res) {
        if (info->freeDecorated) {
            BridgeFree(info->decoratedSymbol);
        }
        if (info->freeUndecorated) {
            BridgeFree(info->undecoratedSymbol);
        }
    }
    BridgeFree(info);
}

std::wstring get_session_filename(size_t session_pid) {
    wchar_t temp_path[MAX_PATH * 4];
    if (GetTempPathW(MAX_PATH * 2, temp_path) == 0) {
        dprintf("Failed to get temp path\n");
        wcscpy(temp_path, L"c:\\windows\\temp\\");
    }

    return std::wstring(temp_path) + L"xauto_session." + std::to_wstring(session_pid) + L".lock";
}


void dbg_get_tls_callbacks(msgpack::sbuffer& response_buffer) {
    DWORD pid = DbgGetProcessId();
    if (pid == 0) {
        XAutoErrorResponse resp_obj = {"XERROR_NO_TARGET", "No debuggee attached"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        XAutoErrorResponse resp_obj = {"XERROR_OPEN_PROCESS", "Failed to open debugee process"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    wchar_t exe_path[MAX_PATH];
    DWORD path_len = MAX_PATH;
    std::vector<uint32_t> callback_rvas;

    if (QueryFullProcessImageNameW(hProcess, 0, exe_path, &path_len)) {
        HANDLE hFile = CreateFileW(exe_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            IMAGE_DOS_HEADER dos_header;
            DWORD bytes_read;
            if (ReadFile(hFile, &dos_header, sizeof(dos_header), &bytes_read, NULL) && dos_header.e_magic == IMAGE_DOS_SIGNATURE) {
                SetFilePointer(hFile, dos_header.e_lfanew, NULL, FILE_BEGIN);
                IMAGE_NT_HEADERS32 nt_headers;
                if (ReadFile(hFile, &nt_headers, sizeof(nt_headers), &bytes_read, NULL) && nt_headers.Signature == IMAGE_NT_SIGNATURE) {
                    IMAGE_DATA_DIRECTORY tls_dir;
                    #ifdef _WIN64
                    if (nt_headers.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                        tls_dir = ((IMAGE_NT_HEADERS64*)&nt_headers)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
                    } else
                    #endif
                    {
                        tls_dir = nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
                    }

                    if (tls_dir.VirtualAddress != 0 && tls_dir.Size >= sizeof(IMAGE_TLS_DIRECTORY32)) {
                        DWORD tls_raw = tls_dir.VirtualAddress;
                        IMAGE_TLS_DIRECTORY32 tls;
                        SetFilePointer(hFile, tls_raw, NULL, FILE_BEGIN);
                        if (ReadFile(hFile, &tls, sizeof(tls), &bytes_read, NULL)) {
                            DWORD callback_array_rva = (DWORD)(tls.AddressOfCallBacks - nt_headers.OptionalHeader.ImageBase);
                            if (callback_array_rva != 0) {
                                SetFilePointer(hFile, callback_array_rva, NULL, FILE_BEGIN);
                                DWORD callback_rva;
                                while (ReadFile(hFile, &callback_rva, sizeof(callback_rva), &bytes_read, NULL) && bytes_read == sizeof(callback_rva)) {
                                    if (callback_rva == 0) break;
                                    callback_rvas.push_back(callback_rva);
                                }
                            }
                        }
                    }
                }
            }
            CloseHandle(hFile);
        }
    }

    CloseHandle(hProcess);
    msgpack::pack(response_buffer, callback_rvas);
}


void dbg_virtual_protect_ex(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr;
    size_t size;
    uint32_t new_prot;

    if (root.via.array.size < 4 ||
        root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
        root.via.array.ptr[2].type != msgpack::type::POSITIVE_INTEGER ||
        root.via.array.ptr[3].type != msgpack::type::POSITIVE_INTEGER) {
        XAutoErrorResponse resp_obj = {"XERROR_BAD_VPROTECT", "Need addr, size, new_prot"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    root.via.array.ptr[1].convert(addr);
    root.via.array.ptr[2].convert(size);
    root.via.array.ptr[3].convert(new_prot);

    DWORD pid = DbgGetProcessId();
    if (pid == 0) {
        msgpack::pack(response_buffer, false);
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION, FALSE, pid);
    if (!hProcess) {
        msgpack::pack(response_buffer, false);
        return;
    }

    DWORD old_prot;
    BOOL result = VirtualProtectEx(hProcess, (LPVOID)addr, size, new_prot, &old_prot);
    CloseHandle(hProcess);
    msgpack::pack(response_buffer, (bool)result);
}


void dbg_suspend_all_threads(msgpack::sbuffer& response_buffer) {
    DWORD pid = DbgGetProcessId();
    if (pid == 0) {
        XAutoErrorResponse resp_obj = {"XERROR_NO_TARGET", "No debuggee attached"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        msgpack::pack(response_buffer, false);
        return;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);

    int suspended = 0;
    int failed = 0;

    if (Thread32First(hSnapshot, &te32)) {
        do {
            if (te32.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
                if (hThread) {
                    if (SuspendThread(hThread) != (DWORD)-1) {
                        suspended++;
                    } else {
                        failed++;
                    }
                    CloseHandle(hThread);
                } else {
                    failed++;
                }
            }
        } while (Thread32Next(hSnapshot, &te32));
    }

    CloseHandle(hSnapshot);
    msgpack::pack(response_buffer, std::tuple<int, int>(suspended, failed));
}


void dbg_get_peb(msgpack::sbuffer& response_buffer) {
    DWORD pid = DbgGetProcessId();
    if (pid == 0) {
        XAutoErrorResponse resp_obj = {"XERROR_NO_TARGET", "No debuggee attached"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        XAutoErrorResponse resp_obj = {"XERROR_OPEN_PROCESS", "Failed to open debugee process"};
        msgpack::pack(response_buffer, resp_obj);
        return;
    }

    typedef LONG (WINAPI *PNtQueryInformationProcess)(
        HANDLE, ULONG, PVOID, ULONG, PULONG);
    static PNtQueryInformationProcess pNtQIP = 
        (PNtQueryInformationProcess)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");

    uint8_t being_debugged = 0;
    uint32_t nt_global_flag = 0;
    uint32_t heap_flags = 0;
    uint32_t heap_force_flags = 0;

    if (pNtQIP) {
        PROCESS_BASIC_INFORMATION pbi;
        memset(&pbi, 0, sizeof(pbi));

        LONG status = pNtQIP(hProcess, 0, &pbi, sizeof(pbi), NULL);
        if (status >= 0 && pbi.PebBaseAddress) {
            SIZE_T bytes_read;
            uint8_t peb_byte;

            if (ReadProcessMemory(hProcess, (BYTE*)pbi.PebBaseAddress + 2, &peb_byte, 1, &bytes_read) && bytes_read == 1) {
                being_debugged = peb_byte;
            }

            #ifdef _WIN64
            ReadProcessMemory(hProcess, (BYTE*)pbi.PebBaseAddress + 0xBC, &nt_global_flag, 4, &bytes_read);
            #else
            ReadProcessMemory(hProcess, (BYTE*)pbi.PebBaseAddress + 0x68, &nt_global_flag, 4, &bytes_read);
            #endif

            ULONG_PTR process_heap_ptr = 0;
            #ifdef _WIN64
            if (ReadProcessMemory(hProcess, (BYTE*)pbi.PebBaseAddress + 0x30, &process_heap_ptr, sizeof(process_heap_ptr), &bytes_read) && process_heap_ptr != 0) {
                ReadProcessMemory(hProcess, (BYTE*)process_heap_ptr + 0x70, &heap_flags, 4, &bytes_read);
                ReadProcessMemory(hProcess, (BYTE*)process_heap_ptr + 0x74, &heap_force_flags, 4, &bytes_read);
            }
            #else
            if (ReadProcessMemory(hProcess, (BYTE*)pbi.PebBaseAddress + 0x18, &process_heap_ptr, sizeof(process_heap_ptr), &bytes_read) && process_heap_ptr != 0) {
                ReadProcessMemory(hProcess, (BYTE*)process_heap_ptr + 0x0C, &heap_flags, 4, &bytes_read);
                ReadProcessMemory(hProcess, (BYTE*)process_heap_ptr + 0x10, &heap_force_flags, 4, &bytes_read);
            }
            #endif
        }
    }

    CloseHandle(hProcess);

    std::tuple<uint8_t, uint32_t, uint32_t, uint32_t> result(being_debugged, nt_global_flag, heap_flags, heap_force_flags);
    msgpack::pack(response_buffer, result);
}


void dbg_get_process_info(msgpack::sbuffer& response_buffer) {
    DWORD pid = DbgGetProcessId();
    DWORD tid = DbgGetThreadId();
    
    size_t entry_point = 0;
    size_t image_base = 0;
    size_t image_size = 0;
    std::string exe_path_str;
    bool is_64bit = false;

    if (pid != 0) {
        is_64bit = (sizeof(void*) == 8);

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess) {
            wchar_t path_buf[MAX_PATH];
            DWORD path_len = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, path_buf, &path_len)) {
                int len = WideCharToMultiByte(CP_UTF8, 0, path_buf, -1, NULL, 0, NULL, NULL);
                if (len > 0) {
                    std::vector<char> buf(len);
                    WideCharToMultiByte(CP_UTF8, 0, path_buf, -1, buf.data(), len, NULL, NULL);
                    exe_path_str.assign(buf.data());

                    HANDLE hFile = CreateFileW(path_buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        IMAGE_DOS_HEADER dos_header;
                        DWORD bytes_read;
                        if (ReadFile(hFile, &dos_header, sizeof(dos_header), &bytes_read, NULL) && dos_header.e_magic == IMAGE_DOS_SIGNATURE) {
                            SetFilePointer(hFile, dos_header.e_lfanew, NULL, FILE_BEGIN);
                            IMAGE_NT_HEADERS32 nt_headers;
                            if (ReadFile(hFile, &nt_headers, sizeof(nt_headers), &bytes_read, NULL) && nt_headers.Signature == IMAGE_NT_SIGNATURE) {
                                #ifdef _WIN64
                                if (nt_headers.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                                    auto* hdr64 = (IMAGE_NT_HEADERS64*)&nt_headers;
                                    entry_point = hdr64->OptionalHeader.AddressOfEntryPoint;
                                    image_base = hdr64->OptionalHeader.ImageBase;
                                    image_size = hdr64->OptionalHeader.SizeOfImage;
                                    is_64bit = true;
                                } else
                                #endif
                                {
                                    entry_point = nt_headers.OptionalHeader.AddressOfEntryPoint;
                                    image_base = nt_headers.OptionalHeader.ImageBase;
                                    image_size = nt_headers.OptionalHeader.SizeOfImage;
                                    is_64bit = false;
                                }
                            }
                        }
                        CloseHandle(hFile);
                    }
                }
            }
            CloseHandle(hProcess);
        }
    }

    std::tuple<uint32_t, uint32_t, size_t, size_t, size_t, std::string, bool> result(
        (uint32_t)pid, (uint32_t)tid, entry_point, image_base, image_size, exe_path_str, is_64bit
    );
    msgpack::pack(response_buffer, result);
}

void dbg_get_callstack(msgpack::sbuffer& response_buffer) {
    DBGCALLSTACK callstack;
    DbgFunctions()->GetCallStackEx(&callstack, false);
    std::vector<CallStackEntryTup> entries;
    for (int i = 0; i < callstack.total; i++) {
        entries.push_back(CallStackEntryTup(
            callstack.entries[i].addr,
            callstack.entries[i].from,
            callstack.entries[i].to,
            std::string(callstack.entries[i].comment)
        ));
    }
    msgpack::pack(response_buffer, entries);
    BridgeFree(callstack.entries);
}

// ============================================================================
// Phase 7+ — AI-native runtime analysis extensions
// ============================================================================

void dbg_get_threads(msgpack::sbuffer& response_buffer) {
    THREADLIST threadList;
    DbgGetThreadList(&threadList);
    std::vector<ThreadInfoTup> entries;
    for (int i = 0; i < threadList.count; i++) {
        auto& t = threadList.list[i];
        entries.push_back(ThreadInfoTup(
            t.BasicInfo.ThreadId,
            t.BasicInfo.ThreadStartAddress,
            t.BasicInfo.ThreadLocalBase,
            t.ThreadCip,
            t.SuspendCount,
            (uint32_t)t.Priority,
            (uint32_t)t.WaitReason,
            t.LastError,
            std::string(t.BasicInfo.threadName)
        ));
    }
    msgpack::pack(response_buffer, entries);
    BridgeFree(threadList.list);
}

void dbg_get_xrefs(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr = 0;
    if (root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        XAutoErrorResponse resp = {"XERROR_BAD_ARG", "Invalid or missing address for xrefs"};
        msgpack::pack(response_buffer, resp);
        return;
    }
    root.via.array.ptr[1].convert(addr);

    XREF_INFO info;
    if (!DbgXrefGet(addr, &info)) {
        XAutoErrorResponse resp = {"XERROR_XREF_FAILED", "Failed to get xrefs"};
        msgpack::pack(response_buffer, resp);
        return;
    }
    std::vector<XrefRecordTup> records;
    for (size_t i = 0; i < info.refcount; i++) {
        records.push_back(XrefRecordTup(info.references[i].addr, (uint32_t)info.references[i].type));
    }
    msgpack::pack(response_buffer, records);
    BridgeFree(info.references);
}

void dbg_get_function(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr = 0;
    if (root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        XAutoErrorResponse resp = {"XERROR_BAD_ARG", "Invalid or missing address for function info"};
        msgpack::pack(response_buffer, resp);
        return;
    }
    root.via.array.ptr[1].convert(addr);

    Script::Function::FunctionInfo info;
    if (!Script::Function::GetInfo(addr, &info)) {
        XAutoErrorResponse resp = {"XERROR_FUNC_NOT_FOUND", "No function found at address"};
        msgpack::pack(response_buffer, resp);
        return;
    }
    msgpack::pack(response_buffer, FunctionInfoTup(
        info.rvaStart,
        info.rvaEnd,
        info.instructioncount,
        info.manual
    ));
}

void dbg_analyze_function(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t entry = 0;
    if (root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        XAutoErrorResponse resp = {"XERROR_BAD_ARG", "Invalid or missing entry point for CFG analysis"};
        msgpack::pack(response_buffer, resp);
        return;
    }
    root.via.array.ptr[1].convert(entry);

    BridgeCFGraphList graph;
    if (!DbgAnalyzeFunction(entry, &graph)) {
        XAutoErrorResponse resp = {"XERROR_ANALYZE_FAILED", "Failed to analyze function"};
        msgpack::pack(response_buffer, resp);
        return;
    }

    // Serialize as tuple: (entry_point, [node_tuples...])
    // Each node tuple: (start, end, brtrue, brfalse, icount, terminal, split, indirectcall, [exits...], [(addr, [bytes...]), ...])
    std::vector<std::tuple<
        size_t, size_t, size_t, size_t, size_t, bool, bool, bool,
        std::vector<size_t>,
        std::vector<std::tuple<size_t, std::vector<uint8_t>>>
    >> nodeTuples;

    auto nodes = (BridgeCFNodeList*)graph.nodes.data;
    int nodeCount = graph.nodes.count;
    for (int i = 0; i < nodeCount; i++) {
        auto& n = nodes[i];

        std::vector<size_t> exits;
        auto exitData = (duint*)n.exits.data;
        for (int j = 0; j < n.exits.count; j++) {
            exits.push_back(exitData[j]);
        }

        std::vector<std::tuple<size_t, std::vector<uint8_t>>> instrs;
        auto instrData = (BridgeCFInstruction*)n.instrs.data;
        for (int j = 0; j < n.instrs.count; j++) {
            std::vector<uint8_t> bytes;
            for (int k = 0; k < 15; k++) bytes.push_back(instrData[j].data[k]);
            instrs.push_back(std::make_tuple((size_t)instrData[j].addr, bytes));
        }

        nodeTuples.push_back(std::make_tuple(
            (size_t)n.start, (size_t)n.end, (size_t)n.brtrue, (size_t)n.brfalse,
            (size_t)n.icount, n.terminal, n.split, n.indirectcall,
            exits, instrs
        ));
    }

    msgpack::pack(response_buffer, std::make_tuple((size_t)graph.entryPoint, nodeTuples));
    BridgeCFGraph::Free(&graph);
}

void dbg_get_string(msgpack::object root, msgpack::sbuffer& response_buffer) {
    size_t addr = 0;
    if (root.via.array.size < 2 || root.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        XAutoErrorResponse resp = {"XERROR_BAD_ARG", "Invalid or missing address for string"};
        msgpack::pack(response_buffer, resp);
        return;
    }
    root.via.array.ptr[1].convert(addr);

    char text[2048];
    if (DbgGetStringAt(addr, text)) {
        msgpack::pack(response_buffer, std::string(text));
    } else {
        msgpack::pack(response_buffer, "");
    }
}

void dbg_get_patches(msgpack::sbuffer& response_buffer) {
    size_t count = 0;
    // First call: get count (returns false but sets count)
    DbgFunctions()->PatchEnum(nullptr, &count);
    if (count == 0) {
        msgpack::pack(response_buffer, std::vector<PatchInfoTup>());
        return;
    }
    DBGPATCHINFO* patches = (DBGPATCHINFO*)BridgeAlloc(count * sizeof(DBGPATCHINFO));
    if (!DbgFunctions()->PatchEnum(patches, &count)) {
        BridgeFree(patches);
        msgpack::pack(response_buffer, std::vector<PatchInfoTup>());
        return;
    }
    std::vector<PatchInfoTup> results;
    for (size_t i = 0; i < count; i++) {
        results.push_back(PatchInfoTup(patches[i].addr, patches[i].oldbyte, patches[i].newbyte));
    }
    msgpack::pack(response_buffer, results);
    BridgeFree(patches);
}

void dbg_get_modules(msgpack::sbuffer& response_buffer) {
    BridgeList<Script::Module::ModuleInfo> list;
    if (!Script::Module::GetList(&list)) {
        msgpack::pack(response_buffer, std::vector<ModuleInfoTup>());
        return;
    }
    std::vector<ModuleInfoTup> results;
    for (int i = 0; i < list.Count(); i++) {
        auto& m = list[i];
        results.push_back(ModuleInfoTup(
            (size_t)m.base, (size_t)m.size, (size_t)m.entry,
            m.sectionCount, std::string(m.name), std::string(m.path)
        ));
    }
    msgpack::pack(response_buffer, results);
}

void dbg_get_seh_chain(msgpack::sbuffer& response_buffer) {
    DBGSEHCHAIN chain;
    DbgFunctions()->GetSEHChain(&chain);
    std::vector<SehRecordTup> records;
    for (duint i = 0; i < chain.total; i++) {
        records.push_back(SehRecordTup(chain.records[i].addr, chain.records[i].handler));
    }
    msgpack::pack(response_buffer, records);
    BridgeFree(chain.records);
}

void dbg_get_handles(msgpack::sbuffer& response_buffer) {
    BridgeList<HANDLEINFO> list;
    if (!DbgFunctions()->EnumHandles(&list)) {
        msgpack::pack(response_buffer, std::vector<HandleInfoTup>());
        return;
    }
    std::vector<HandleInfoTup> results;
    auto getHandleName = DbgFunctions()->GetHandleName;
    for (int i = 0; i < list.Count(); i++) {
        auto& h = list[i];
        // Resolve the human-readable type ("File"/"Mutant"/"Event"/"Key"/
        // "DebugObject"/...) and object name (file path, mutex name, ...).
        // DebugObject handles in particular are a classic anti-debug signal.
        char name[512] = {0};
        char typeName[256] = {0};
        if (getHandleName != nullptr) {
            getHandleName(h.Handle, name, sizeof(name), typeName, sizeof(typeName));
        }
        results.push_back(HandleInfoTup(
            (size_t)h.Handle, h.TypeNumber, h.GrantedAccess,
            std::string(typeName), std::string(name)));
    }
    msgpack::pack(response_buffer, results);
}

// ── Coverage tracking ────────────────────────────────────────────────────────

std::unordered_set<size_t> g_coverage_set;
std::mutex g_coverage_mutex;
std::atomic<bool> g_coverage_active{false};

// Returns (active, current_count)
void coverage_start(msgpack::sbuffer& response_buffer) {
    g_coverage_active.store(true);
    std::lock_guard<std::mutex> lock(g_coverage_mutex);
    msgpack::pack(response_buffer, std::tuple<bool, size_t>(true, g_coverage_set.size()));
}

void coverage_stop(msgpack::sbuffer& response_buffer) {
    g_coverage_active.store(false);
    std::lock_guard<std::mutex> lock(g_coverage_mutex);
    msgpack::pack(response_buffer, std::tuple<bool, size_t>(false, g_coverage_set.size()));
}

// arg[1]=start_addr, arg[2]=end_addr (both 0 = return all)
void coverage_get(msgpack::object root, msgpack::sbuffer& response_buffer) {
    duint start_filter = 0, end_filter = 0;
    if (root.type == msgpack::type::ARRAY && root.via.array.size >= 3) {
        root.via.array.ptr[1].convert(start_filter);
        root.via.array.ptr[2].convert(end_filter);
    }
    std::vector<size_t> addrs;
    {
        std::lock_guard<std::mutex> lock(g_coverage_mutex);
        addrs.reserve(g_coverage_set.size());
        for (auto addr : g_coverage_set) {
            if (start_filter == 0 || (addr >= start_filter && (end_filter == 0 || addr < end_filter)))
                addrs.push_back((size_t)addr);
        }
    }
    std::sort(addrs.begin(), addrs.end());
    msgpack::pack(response_buffer, std::tuple<std::vector<size_t>, size_t>(addrs, addrs.size()));
}

void coverage_clear(msgpack::sbuffer& response_buffer) {
    std::lock_guard<std::mutex> lock(g_coverage_mutex);
    g_coverage_set.clear();
    msgpack::pack(response_buffer, true);
}
