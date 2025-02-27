#pragma once
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

#include <unordered_map>

#include "Common.h"
#include "HashMapHolder.h"
#include "Item.h"
#include "ItemTemplate.hpp"
#include "LockedQueue.h"
#include "Monster.h"
#include "Player.h"
#include "SharedMutex.h"
#include "Summon.h"

typedef std::unordered_map<uint32_t, Object *> UpdateMap;
class MemoryPoolMgr {
public:
    ~MemoryPoolMgr() = default;
    // Deleting the copy & assignment operators
    // Better safe than sorry
    MemoryPoolMgr(const MemoryPoolMgr &) = delete;
    MemoryPoolMgr &operator=(const MemoryPoolMgr &) = delete;

    static MemoryPoolMgr &Instance()
    {
        static MemoryPoolMgr instance;
        return instance;
    }

    template<class T>
    T *GetObjectInWorld(uint32_t handle)
    {
        uint32_t idbase = handle & 0xE0000000;
        switch (idbase) {
        case 0x00000000:
            return dynamic_cast<T *>(HashMapHolder<Item>::Find(handle));
        case 0x20000000:
            return dynamic_cast<T *>(HashMapHolder<Object>::Find(handle));
        case 0x40000000:
            return dynamic_cast<T *>(HashMapHolder<Monster>::Find(handle));
        case 0x80000000:
            return dynamic_cast<T *>(HashMapHolder<Player>::Find(handle));
        case 0xC0000000:
            return dynamic_cast<T *>(HashMapHolder<Summon>::Find(handle));
        default:
            return nullptr;
        }
    }

    template<class T>
    void RemoveObject(T *object)
    {
        HashMapHolder<T>::Remove(object);
    }

    template<class T>
    void AddObject(T *object)
    {
        HashMapHolder<T>::Insert(object);
        if (!object->IsItem() && !object->IsFieldProp())
            addUpdateQueue.add(object);
    }

    Item *AllocItem();
    Item *AllocGold(int64_t gold, GenerateCode gcode);
    void AllocMiscHandle(Object *obj);
    void AllocItemHandle(Item *item);
    Player *AllocPlayer();
    Summon *AllocSummon(uint32_t);
    Monster *AllocMonster(uint32_t idx);
    Summon *AllocNewSummon(Player *, Item *);

    void Destroy();
    void Update(uint32_t diff);
    // when using this, you must use the HashMapHolders lock!
    const HashMapHolder<Player>::MapType &GetPlayers();

private:
    template<class T>
    void _unload();
    void AddToDeleteList(Object *obj);
    std::set<Object *> i_objectsToRemove{};
    LockedQueue<Object *> addUpdateQueue;

    UpdateMap i_objectsToUpdate{};

    uint32_t m_nMiscTop{0x20000001};
    uint32_t m_nMonsterTop{0x40000001};
    uint32_t m_nPlayerTop{0x80000001};
    uint32_t m_nSummonTop{0xC0000001};
#if EPIC >= EPIC_5_1
    uint32_t m_nPetTop{0xE0000001};
#endif
    uint32_t m_nItemTop{0x00000001};

protected:
    MemoryPoolMgr() = default;
};

#define sMemoryPool MemoryPoolMgr::Instance()
