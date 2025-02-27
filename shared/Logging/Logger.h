#pragma once
/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
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
#include <string>
#include <unordered_map>

#include "Define.h"
#include "LogCommon.h"

class Appender;
struct LogMessage;

class Logger {
public:
    Logger(std::string const &name, LogLevel level);

    void addAppender(uint8_t type, Appender *appender);
    void delAppender(uint8_t type);

    std::string const &getName() const;
    LogLevel getLogLevel() const;
    void setLogLevel(LogLevel level);
    void write(LogMessage *message) const;

private:
    std::string name;
    LogLevel level;
    std::unordered_map<uint8_t, Appender *> appenders;
};