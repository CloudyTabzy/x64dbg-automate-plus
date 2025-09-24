#include "plugin.h"
#include "pluginmain.h"


CBTYPE last_event;
void* last_cbinfo;


void cb_sys_breakpoint(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_SYSTEMBREAKPOINT* bp = (PLUG_CB_SYSTEMBREAKPOINT*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t>(std::string("EVENT_SYSTEMBREAKPOINT"), (size_t)bp->reserved));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_breakpoint(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_BREAKPOINT* bp = (PLUG_CB_BREAKPOINT*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<
        std::string, 
        size_t, 
        size_t, 
        bool, 
        bool, 
        bool,
        std::string,
        std::string,
        size_t,
        uint8_t,
        uint8_t,
        size_t,
        bool, 
        bool,
        std::string,
        std::string,
        std::string,
        std::string,
        std::string
    >(
        std::string("EVENT_BREAKPOINT"),
        bp->breakpoint->type,
        bp->breakpoint->addr,
        bp->breakpoint->enabled,
        bp->breakpoint->singleshoot,
        bp->breakpoint->active,
        std::string(bp->breakpoint->name),
        std::string(bp->breakpoint->mod),
        bp->breakpoint->slot,
        bp->breakpoint->typeEx,
        bp->breakpoint->hwSize,
        bp->breakpoint->hitCount,
        bp->breakpoint->fastResume,
        bp->breakpoint->silent,
        std::string(bp->breakpoint->breakCondition),
        std::string(bp->breakpoint->logText),
        std::string(bp->breakpoint->logCondition),
        std::string(bp->breakpoint->commandText),
        std::string(bp->breakpoint->commandCondition)
    ));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_create_thread(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_CREATETHREAD* ct = (PLUG_CB_CREATETHREAD*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t, size_t, size_t>(
        std::string("EVENT_CREATE_THREAD"), (size_t)ct->dwThreadId, (size_t)ct->CreateThread->lpThreadLocalBase, (size_t)ct->CreateThread->lpStartAddress));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_exit_thread(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_EXITTHREAD* et = (PLUG_CB_EXITTHREAD*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t, size_t>(
        std::string("EVENT_EXIT_THREAD"), (size_t)et->dwThreadId, (size_t)et->ExitThread->dwExitCode));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_load_dll(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_LOADDLL* ld = (PLUG_CB_LOADDLL*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, std::string, size_t>(
        std::string("EVENT_LOAD_DLL"), std::string(ld->modname), (size_t)ld->LoadDll->lpBaseOfDll));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_unload_dll(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_UNLOADDLL* udl = (PLUG_CB_UNLOADDLL*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t>(
        std::string("EVENT_UNLOAD_DLL"), (size_t)udl->UnloadDll->lpBaseOfDll));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_debugstr(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_OUTPUTDEBUGSTRING* ods = (PLUG_CB_OUTPUTDEBUGSTRING*)callbackInfo;
    msgpack::sbuffer outbuf;

    std::vector<uint8_t> membuf(ods->DebugString->nDebugStringLength);
    if(!DbgMemRead((size_t)ods->DebugString->lpDebugStringData, membuf.data(), ods->DebugString->nDebugStringLength)) {
        dprintf("Failed to read debug string memory\n");
        return;
    }

    msgpack::pack(outbuf, std::tuple<std::string, std::vector<uint8_t>>(
        std::string("EVENT_OUTPUT_DEBUG_STRING"), membuf));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_exception(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_EXCEPTION* exc = (PLUG_CB_EXCEPTION*)callbackInfo;
    msgpack::sbuffer outbuf;
    std::vector<size_t> params;

    for (size_t i = 0; i < exc->Exception->ExceptionRecord.NumberParameters; i++) {
        params.push_back(exc->Exception->ExceptionRecord.ExceptionInformation[i]);
    }

    msgpack::pack(outbuf, std::tuple<std::string, size_t, size_t, size_t, size_t, size_t, std::vector<size_t>, size_t>(
        std::string("EVENT_EXCEPTION"), 
        (size_t)exc->Exception->ExceptionRecord.ExceptionCode,
        (size_t)exc->Exception->ExceptionRecord.ExceptionFlags,
        (size_t)exc->Exception->ExceptionRecord.ExceptionRecord,
        (size_t)exc->Exception->ExceptionRecord.ExceptionAddress,
        (size_t)exc->Exception->ExceptionRecord.NumberParameters,
        params,
        exc->Exception->dwFirstChance));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_stepped(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_STEPPED* _st = (PLUG_CB_STEPPED*)callbackInfo;
    msgpack::sbuffer outbuf;
    std::vector<size_t> params;

    msgpack::pack(outbuf, std::tuple<std::string>(
        std::string("EVENT_STEPPED")));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_resume_debug(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_RESUMEDEBUG* _rd = (PLUG_CB_RESUMEDEBUG*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string>(
        std::string("EVENT_RESUME_DEBUG")));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_pause_debug(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_PAUSEDEBUG* _pd = (PLUG_CB_PAUSEDEBUG*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string>(
        std::string("EVENT_PAUSE_DEBUG")));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_attach(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_ATTACH* at = (PLUG_CB_ATTACH*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t>(
        std::string("EVENT_ATTACH"), (size_t)at->dwProcessId));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_detach(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_DETACH* dt = (PLUG_CB_DETACH*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t>(
        std::string("EVENT_DETACH"), (size_t)dt->fdProcessInfo->dwProcessId));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_init_debug(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_INITDEBUG* id = (PLUG_CB_INITDEBUG*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, std::string>(
        std::string("EVENT_INIT_DEBUG"), std::string(id->szFileName)));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_stop_debug(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_STOPDEBUG* sd = (PLUG_CB_STOPDEBUG*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string>(
        std::string("EVENT_STOP_DEBUG")));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_create_process(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_CREATEPROCESS* cp = (PLUG_CB_CREATEPROCESS*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t, size_t, size_t, std::string>(
        std::string("EVENT_CREATE_PROCESS"),
        (size_t)cp->fdProcessInfo->dwProcessId, 
        (size_t)cp->fdProcessInfo->dwThreadId, 
        (size_t)cp->CreateProcessInfo->lpStartAddress,
        std::string(cp->DebugFileName)));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

void cb_exit_process(CBTYPE cbType, void* callbackInfo)
{
    PLUG_CB_EXITPROCESS* ep = (PLUG_CB_EXITPROCESS*)callbackInfo;
    msgpack::sbuffer outbuf;
    msgpack::pack(outbuf, std::tuple<std::string, size_t>(
        std::string("EVENT_EXIT_PROCESS"), (size_t)ep->ExitProcess->dwExitCode));
    srv->pub_socket.send(zmq::buffer(outbuf.data(), outbuf.size()), zmq::send_flags::none);
}

bool pluginInit(PLUG_INITSTRUCT* initStruct)
{
    dprintf("pluginInit(pluginHandle: %d)\n", pluginHandle);
    last_cbinfo = nullptr;
    _plugin_registercallback(pluginHandle, CB_BREAKPOINT, cb_breakpoint);
    _plugin_registercallback(pluginHandle, CB_SYSTEMBREAKPOINT, cb_sys_breakpoint);
    _plugin_registercallback(pluginHandle, CB_CREATETHREAD, cb_create_thread);
    _plugin_registercallback(pluginHandle, CB_EXITTHREAD, cb_exit_thread);
    _plugin_registercallback(pluginHandle, CB_LOADDLL, cb_load_dll);
    _plugin_registercallback(pluginHandle, CB_UNLOADDLL, cb_unload_dll);
    _plugin_registercallback(pluginHandle, CB_OUTPUTDEBUGSTRING, cb_debugstr);
    _plugin_registercallback(pluginHandle, CB_EXCEPTION, cb_exception);
    _plugin_registercallback(pluginHandle, CB_STEPPED, cb_stepped);
    _plugin_registercallback(pluginHandle, CB_RESUMEDEBUG, cb_resume_debug);
    _plugin_registercallback(pluginHandle, CB_PAUSEDEBUG, cb_pause_debug);
    _plugin_registercallback(pluginHandle, CB_INITDEBUG, cb_init_debug);
    _plugin_registercallback(pluginHandle, CB_STOPDEBUG, cb_stop_debug);
    _plugin_registercallback(pluginHandle, CB_ATTACH, cb_attach);
    _plugin_registercallback(pluginHandle, CB_DETACH, cb_detach);
    _plugin_registercallback(pluginHandle, CB_CREATEPROCESS, cb_create_process);
    _plugin_registercallback(pluginHandle, CB_EXITPROCESS, cb_exit_process);
    return true;
}

void pluginStop()
{
    dprintf("pluginStop(pluginHandle: %d)\n", pluginHandle);
    srv->release_session();
}

void pluginSetup()
{
    dprintf("pluginSetup(pluginHandle: %d)\n", pluginHandle);
}
