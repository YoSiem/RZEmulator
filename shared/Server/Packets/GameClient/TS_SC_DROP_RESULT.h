#ifndef PACKETS_TS_SC_DROP_RESULT_H
#define PACKETS_TS_SC_DROP_RESULT_H

#include "Server/Packets/PacketDeclaration.h"

#define TS_SC_DROP_RESULT_DEF(_) \
	_(simple)(uint32_t, item_handle) \
    _(simple)(uint8_t, isAccepted)

CREATE_PACKET(TS_SC_DROP_RESULT, 205);

#endif // PACKETS_TS_SC_DROP_RESULT_H
