#ifndef PACKETS_TS_CA_DISTRIBUTION_INFO_H
#define PACKETS_TS_CA_DISTRIBUTION_INFO_H

#include "Packets/PacketDeclaration.h"

#define TS_CA_DISTRIBUTION_INFO_DEF(_) \
    _(endstring)(distributionInfo, true)
CREATE_PACKET(TS_CA_DISTRIBUTION_INFO, 10026);

#endif // PACKETS_TS_CA_DISTRIBUTION_INFO_H
