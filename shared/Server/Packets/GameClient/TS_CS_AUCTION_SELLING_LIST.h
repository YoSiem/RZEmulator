#ifndef PACKETS_TS_CS_AUCTION_SELLING_LIST_H
#define PACKETS_TS_CS_AUCTION_SELLING_LIST_H

#include "Server/Packets/PacketDeclaration.h"

#define TS_CS_AUCTION_SELLING_LIST_DEF(_) \
	_(simple)(int32_t, page_num)

CREATE_PACKET(TS_CS_AUCTION_SELLING_LIST, 1302);

#endif // PACKETS_TS_CS_AUCTION_SELLING_LIST_H
