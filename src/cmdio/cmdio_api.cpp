
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include "cmdio.h"
#include "dlfcn.h"
#include "util/os.h"

#define MAX_STRING_LEN 1000

cmdioError_t g_last_cmdioError = cmdioSuccess;
// static void* g_cmdio_engine;
static os::LibHandle g_cmdio_engine; //  = os::LoadLib(name);

#define CMDAPI(CMD) \
static pfn_##CMD g_pfn_##CMD ;
#include "api.inc"
#undef CMDAPI


extern "C" int cmd_open(ioctl_open_args *args)
{
    // char* cmdio_engine_name = getenv("CMDIO_ENGINE");
    std::string cmdio_engine_name = os::GetEnvVar("CMDIO_ENGINE");

    if (cmdio_engine_name == "cpu") {
        //g_cmdio_engine =  dlopen("libcpu_engine.so", RTLD_LAZY);
        g_cmdio_engine = os::LoadLib("libcpu_engine.so");
    } else if (cmdio_engine_name == "model") {
        //g_cmdio_engine =  dlopen("libppu_engine.so", RTLD_LAZY);
        g_cmdio_engine = os::LoadLib("libcmdio_emu.so");
    } else {
        g_cmdio_engine = os::LoadLib("libcmdio_emu.so");
        // g_cmdio_engine =  dlopen("libcpu_engine.so", RTLD_LAZY);
        // fatal("please set env CMDIO_ENGINEto cpu or ppu");
    }

#define STR1(R) #R
#define STR2(R0, R1) STR1(R0##R1)

#define CMDAPI(CMD) \
    g_pfn_##CMD = (pfn_##CMD)os::GetExportAddress(g_cmdio_engine, STR2(cmd_, CMD));

#include "api.inc"
#undef CMDAPI

    g_last_cmdioError = cmdioSuccess;

    //ioctl_open_args args;
    //args.device_num = device_num;

    int ret = g_pfn_open(args);
    assert(ret == cmdioSuccess);
    // cmd_handle = args.handle;
    // return (void*)args.handle;
    if (ret == 0) {
        g_last_cmdioError = cmdioSuccess;
    } else {
        g_last_cmdioError = cmdioError_open;
    }

    return g_last_cmdioError;
}

#define CMDAPI_open 0
#define CMDAPI(CMD) \
extern "C" int cmd_##CMD(ioctl_##CMD##_args *args)    \
{                                                           \
    int ret = g_pfn_##CMD(args);                            \
                                                            \
    if (ret == 0 ) {                                              \
        g_last_cmdioError = cmdioSuccess;                   \
    } else {                                                \
        g_last_cmdioError = cmdioError_##CMD;               \
    }                                                       \
                                                            \
    return g_last_cmdioError;                               \
}

#include "api.inc"
#undef CMDAPI
#define CMDAPI_open 1


