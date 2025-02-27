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
#include "Common.h"

class TimeSynch {
public:
    TimeSynch(uint32_t L, uint32_t DC, uint32_t pMAX)
        : m_L(L)
        , m_DC(DC)
        , m_MAX(pMAX)
    {
        m_vT.reserve(m_MAX);
    };

    ~TimeSynch() = default;

    void onEcho(uint32_t t)
    {
        if (static_cast<uint32_t>(m_vT.size()) == m_MAX)
            *m_vT.begin() = t;
        else
            m_vT.emplace_back(t);
    }

    uint32_t GetInterval()
    {
        uint32_t tc = 0;
        uint32_t CDC = 0;
        auto size = static_cast<uint32_t>(m_vT.size());

        for (auto &i : m_vT) {
            if (i < m_L || CDC >= m_DC) {
                ++size;
                tc += i;
            }
            CDC++;
        }
        return tc / size >> 1;
    }

    uint32_t GetTestCount() { return 0; }

    std::vector<uint32_t> m_vT{};

private:
    uint32_t m_L{};
    uint32_t m_DC{};
    uint32_t m_MAX{};
};