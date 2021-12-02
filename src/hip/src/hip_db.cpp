
// TODO schi  #include <hc_am.hpp>
#include <inc/csq_pointer.h>


// void hipdbPrintMem(void* targetAddress) { hc::am_memtracker_print(targetAddress); };
void hipdbPrintMem(void* targetAddress) { csq::am_memtracker_print(targetAddress); };
