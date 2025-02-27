#pragma once
/*
 * Copyright (C) 2011-2017 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2017 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2017 MaNGOS <https://www.getmangos.eu/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
/** ByteConverter reverse your byte order.  This is use
    for cross platform where they have different endians.
*/

#include <algorithm>

#include "Define.h"

namespace ByteConverter {
    template<size_t T>
    inline void convert(char *val)
    {
        std::swap(*val, *(val + T - 1));
        convert<T - 2>(val + 1);
    }

    template<>
    inline void convert<0>(char *)
    {
    }

    template<>
    inline void convert<1>(char *)
    {
    } // ignore central byte

    template<typename T>
    inline void apply(T *val)
    {
        convert<sizeof(T)>((char *)(val));
    }
} // namespace ByteConverter

#if NGEMITY_ENDIAN == NGEMITY_BIGENDIAN
template<typename T>
inline void EndianConvert(T &val)
{
    ByteConverter::apply<T>(&val);
}
template<typename T>
inline void EndianConvertReverse(T &)
{
}
template<typename T>
inline void EndianConvertPtr(void *val)
{
    ByteConverter::apply<T>(val);
}
template<typename T>
inline void EndianConvertPtrReverse(void *)
{
}
#else

template<typename T>
inline void EndianConvert(T &)
{
}

template<typename T>
inline void EndianConvertReverse(T &val)
{
    ByteConverter::apply<T>(&val);
}

template<typename T>
inline void EndianConvertPtr(void *)
{
}

template<typename T>
inline void EndianConvertPtrReverse(void *val)
{
    ByteConverter::apply<T>(val);
}

#endif

template<typename T>
void EndianConvert(T *); // will generate link error
template<typename T>
void EndianConvertReverse(T *); // will generate link error

inline void EndianConvert(uint8_t &) {}

inline void EndianConvert(int8_t &) {}

inline void EndianConvertReverse(uint8_t &) {}

inline void EndianConvertReverse(int8_t &) {}