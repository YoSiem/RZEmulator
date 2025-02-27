#ifndef PACKETS_TS_SC_TAMING_INFO_H
#define PACKETS_TS_SC_TAMING_INFO_H

#include "Server/Packets/PacketDeclaration.h"

#define TS_SC_TAMING_INFO_DEF(_) \
	_(simple)(int8_t, mode) \
	_(simple)(uint32_t, tamer_handle) \
	_(simple)(uint32_t, target_handle)

CREATE_PACKET(TS_SC_TAMING_INFO, 310);

#endif // PACKETS_TS_SC_TAMING_INFO_H
