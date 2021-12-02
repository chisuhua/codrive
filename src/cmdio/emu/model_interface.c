#include <string>
#include <stdint.h>
#include <dlfcn.h>
#include "util/os.h"
#include <stdlib.h>
#include "inc/Model.h"


static pfn_create_model g_pfn_create_model;
static pfn_get_device_cnt g_pfn_get_device_cnt;
static pfn_destroy_model g_pfn_destroy_model;
static pfn_write_register g_pfn_write_register;
static pfn_read_register g_pfn_read_register;
static pfn_read_mmio g_pfn_read_mmio;
static pfn_write_mmio g_pfn_write_mmio;
static pfn_query_adapter_info g_pfn_query_adapter_info;

void* create_model(){
    const char *model_type_name = getenv("MODEL_TYPE");
    void* hModel;
    if (model_type_name) {
        const std::string model_type(model_type_name);
        if (model_type == "MODEL_BEHAVIOR") {
            hModel = dlopen("libbhmodel.so", RTLD_LAZY);
        } else if (model_type == "MODEL_COMODEL") {
            hModel = dlopen("libcomodel.so", RTLD_LAZY);
        } else {
            assert("MODEL_TYPE is not valid");
        }
    } else {
        hModel = dlopen("libbhmodel.so", RTLD_LAZY);
    }


    g_pfn_create_model = (pfn_create_model)os::GetExportAddress(hModel, "create_model");
    g_pfn_get_device_cnt = (pfn_get_device_cnt)os::GetExportAddress(hModel, "get_device_cnt");
    g_pfn_destroy_model = (pfn_destroy_model)os::GetExportAddress(hModel, "destroy_model");
    g_pfn_write_register = (pfn_write_register)os::GetExportAddress(hModel, "write_register");
    g_pfn_read_register = (pfn_read_register)os::GetExportAddress(hModel, "read_register");
    g_pfn_read_mmio = (pfn_read_mmio)os::GetExportAddress(hModel, "read_mmio");
    g_pfn_write_mmio = (pfn_write_mmio)os::GetExportAddress(hModel, "write_mmio");
    g_pfn_query_adapter_info = (pfn_query_adapter_info)os::GetExportAddress(hModel, "query_adapter_info");

    void* pModel = g_pfn_create_model(0);
    // Model* pModel = Model::Create(0);
    return pModel;
}

uint32_t get_device_cnt() {
    return g_pfn_get_device_cnt();
}

void destroy_model(void* handle) {
    g_pfn_destroy_model(handle);
}

void write_register(void* handle, uint32_t offset, uint32_t value){
    printf("DEBUG: WriteReg: write %x to address %x\n", value, offset);
    g_pfn_write_register(handle, offset, value);
}

uint32_t read_register(void* handle, uint32_t offset){
    return g_pfn_read_register(handle, offset);
}

void read_mmio(void* handle, uint32_t offset, void* data, uint32_t length, uint32_t bar) {
    g_pfn_read_mmio(handle, offset, data, length, bar);
}

void write_mmio(void* handle, uint32_t offset, void* data, uint32_t length, uint32_t bar) {
    g_pfn_write_mmio(handle, offset, data, length, bar);
}


void query_adapter_info(uint32_t deviceIdx, AdapterInfo* pInfo) {
    g_pfn_query_adapter_info(deviceIdx, pInfo);
}

