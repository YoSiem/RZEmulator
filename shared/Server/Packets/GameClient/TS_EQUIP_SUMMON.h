#ifndef PACKETS_TS_EQUIP_SUMMON_H
#define PACKETS_TS_EQUIP_SUMMON_H

#include "Server/Packets/PacketDeclaration.h"

#define TS_EQUIP_SUMMON_DEF(_) \
    _(simple) (uint8_t, open_dialog) \
	_(array) (uint32_t, card_handle, 6)

CREATE_PACKET(TS_EQUIP_SUMMON, 303);

#endif // PACKETS_TS_EQUIP_SUMMON_H
