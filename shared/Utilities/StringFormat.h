#pragma once
/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
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
#include "fmt/printf.h"

namespace NGemity {
    /// Default NG string format function.
template <typename... Args>
    std::string StringFormat(const fmt::format_string<Args...> s, Args&&... args)
    {
        return fmt::format(s, std::forward<Args>(args)...);
    }

    /// Default TC string format function.
    template<typename Format, typename... Args>
    inline std::string StringFormatTC(Format &&fmt, Args &&...args)
    {
        return fmt::sprintf(std::forward<Format>(fmt), std::forward<Args>(args)...);
    }
    /// Returns true if the given char pointer is null.
    inline bool IsFormatEmptyOrNull(const char *fmt)
    {
        return fmt == nullptr;
    }

    /// Returns true if the given std::string is empty.
    inline bool IsFormatEmptyOrNull(std::string const &fmt)
    {
        return fmt.empty();
    }
} // namespace NGemity