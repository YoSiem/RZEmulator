/*
 *  Copyright (C) 2017-2020 NGemity <https://ngemity.org/>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "RespawnObject.h"

#include "GameContent.h"
#include "Log.h"
#include "MemPool.h"
#include "ObjectMgr.h"
#include "World.h"

RespawnObject::RespawnObject(MonsterRespawnInfo rh)
    : info(RespawnInfo{rh})
{
    m_nMaxRespawnNum = info.prespawn_count;
    lastDeadTime = 0;
}

void RespawnObject::Update(uint32_t diff)
{
    /// No need to update anything
    if (info.count >= m_nMaxRespawnNum)
        return;

    uint32_t ct = sWorld.GetArTime();

    /// Only update based on spawn rates (each X seconds after dead)
    if (lastDeadTime != 0 && lastDeadTime + info.interval > ct)
        return;

    auto respawn_count = std::min(m_nMaxRespawnNum - info.count, info.inc);

    if (lastDeadTime == 0) {
        lastDeadTime = ct;
        respawn_count = m_nMaxRespawnNum;
    }

    /// Do we need a respawn?
    if (respawn_count > 0) {
        int32_t try_cnt = 0;
        for (uint32_t i = 0; i < respawn_count; ++i) {
            /// Generate random respawn coordinates based on a rectangle
            int32_t x{};
            int32_t y{};

            do {
                x = irand((int32_t)info.left, (int32_t)info.right);
                y = irand((int32_t)info.top, (int32_t)info.bottom);

                if (++try_cnt > 500) {
                    NG_LOG_ERROR("server.worldserver", "Cannot respawn monster - try_cnt = 500");
                    return;
                }
            } while (GameContent::IsBlocked(x, y));

            /// Generate monster if not blocked
            auto monster = GameContent::RespawnMonster(x, y, info.layer, info.monster_id, info.is_wandering, info.way_point_id, this, true);

            /// Put it to the list when it's not blocked
            if (monster != nullptr) {
                if (info.dungeon_id != 0) {
                    // monster.m_nDungeonId = info.dungeon_id;
                }
                m_vRespawnedMonster.emplace_back(monster->GetHandle());
                info.count++;
            }
        }
    }
}

void RespawnObject::onMonsterDelete(Monster *mob)
{
    if (mob == nullptr)
        return;

    lastDeadTime = sWorld.GetArTime();
    --info.count;

    auto pos = std::find(m_vRespawnedMonster.begin(), m_vRespawnedMonster.end(), mob->GetHandle());
    if (pos != m_vRespawnedMonster.end()) {
        m_vRespawnedMonster.erase(pos);
        mob->m_pDeleteHandler = nullptr;
    }

    if (m_nMaxRespawnNum < info.max_num)
        ++m_nMaxRespawnNum;
}
