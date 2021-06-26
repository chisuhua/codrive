#pragma once

// TODO mlvm #include "sim/aasim/lib/cmd_packet.h"
#include "grid_engine/cpu/lib/cmd_packet.h"

namespace dla_driver
{

typedef CmdDispatchPacket CmdPkt;

// TODO: Need to define CmdPkt
//       Currently we use simulator packet definition
#if 0
struct CmdPkt
{
    uint8_t  type;
    uint64_t length;
};
#endif


}
