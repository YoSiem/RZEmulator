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
#include <memory>
#include <unordered_map>
#include <vector>

#include "AsioHacksFwd.h"
#include "Define.h"
#include "LogCommon.h"
#include "StringFormat.h"

class Appender;
class Logger;
struct LogMessage;

namespace NGemity {
    namespace Asio {
        class IoContext;
    }
} // namespace NGemity

#define LOGGER_ROOT "root"

typedef Appender *(*AppenderCreatorFn)(uint8_t id, std::string const &name, LogLevel level, AppenderFlags flags, std::vector<char const *> &&extraArgs);

template<class AppenderImpl>
Appender *CreateAppender(uint8_t id, std::string const &name, LogLevel level, AppenderFlags flags, std::vector<char const *> &&extraArgs)
{
    return new AppenderImpl(id, name, level, flags, std::forward<std::vector<char const *>>(extraArgs));
}

class Log {
private:
    Log();
    ~Log();
    Log(Log const &) = delete;
    Log(Log &&) = delete;
    Log &operator=(Log const &) = delete;
    Log &operator=(Log &&) = delete;

public:
    static Log *instance();

    void Initialize(NGemity::Asio::IoContext *ioContext);
    void SetSynchronous(); // Not threadsafe - should only be called from main() after all threads are joined
    void LoadFromConfig();
    void Close();
    bool ShouldLog(std::string const &type, LogLevel level) const;
    bool SetLogLevel(std::string const &name, char const *level, bool isLogger = true);

    template<typename Format, typename... Args>
    inline void outMessage(std::string const &filter, LogLevel const level, Format &&fmt, Args &&...args)
    {
        outMessage(filter, level, NGemity::StringFormatTC(std::forward<Format>(fmt), std::forward<Args>(args)...));
    }

    template<typename Format, typename... Args>
    void outCommand(uint32_t account, Format &&fmt, Args &&...args)
    {
        if (!ShouldLog("commands.gm", LOG_LEVEL_INFO))
            return;

        outCommand(NGemity::StringFormatTC(std::forward<Format>(fmt), std::forward<Args>(args)...), std::to_string(account));
    }

    void outCharDump(char const *str, uint32_t account_id, uint64_t guid, char const *name);

    void SetRealmId(uint32_t id);

    template<class AppenderImpl>
    void RegisterAppender()
    {
        using Index = typename AppenderImpl::TypeIndex;
        RegisterAppender(Index::value, &CreateAppender<AppenderImpl>);
    }

    std::string const &GetLogsDir() const { return m_logsDir; }

    std::string const &GetLogsTimestamp() const { return m_logsTimestamp; }

private:
    static std::string GetTimestampStr();
    void write(std::unique_ptr<LogMessage> &&msg) const;

    Logger const *GetLoggerByType(std::string const &type) const;
    Appender *GetAppenderByName(std::string const &name);
    uint8_t NextAppenderId();
    void CreateAppenderFromConfig(std::string const &name);
    void CreateLoggerFromConfig(std::string const &name);
    void ReadAppendersFromConfig();
    void ReadLoggersFromConfig();
    void RegisterAppender(uint8_t index, AppenderCreatorFn appenderCreateFn);
    void outMessage(std::string const &filter, LogLevel const level, std::string &&message);
    void outCommand(std::string &&message, std::string &&param1);

    std::unordered_map<uint8_t, AppenderCreatorFn> appenderFactory;
    std::unordered_map<uint8_t, std::unique_ptr<Appender>> appenders;
    std::unordered_map<std::string, std::unique_ptr<Logger>> loggers;
    uint8_t AppenderId;
    LogLevel lowestLogLevel;

    std::string m_logsDir;
    std::string m_logsTimestamp;

    NGemity::Asio::IoContext *_ioContext;
    NGemity::Asio::Strand *_strand;
};

#define sLog Log::instance()

#define LOG_EXCEPTION_FREE(filterType__, level__, ...)                                                                         \
    {                                                                                                                          \
        try {                                                                                                                  \
            sLog->outMessage(filterType__, level__, __VA_ARGS__);                                                              \
        }                                                                                                                      \
        catch (std::exception & e) {                                                                                           \
            sLog->outMessage("server", LOG_LEVEL_ERROR, "Wrong format occurred (%s) at %s:%u.", e.what(), __FILE__, __LINE__); \
        }                                                                                                                      \
    }

#if PLATFORM != PLATFORM_WINDOWS
void check_args(const char *, ...) ATTR_PRINTF(1, 2);
void check_args(std::string const &, ...);

// This will catch format errors on build time
#define NG_LOG_MESSAGE_BODY(filterType__, level__, ...)             \
    do {                                                            \
        if (sLog->ShouldLog(filterType__, level__)) {               \
            if (false)                                              \
                check_args(__VA_ARGS__);                            \
                                                                    \
            LOG_EXCEPTION_FREE(filterType__, level__, __VA_ARGS__); \
        }                                                           \
    } while (0)
#else
#define NG_LOG_MESSAGE_BODY(filterType__, level__, ...)             \
    __pragma(warning(push)) __pragma(warning(disable : 4127)) do    \
    {                                                               \
        if (sLog->ShouldLog(filterType__, level__))                 \
            LOG_EXCEPTION_FREE(filterType__, level__, __VA_ARGS__); \
    }                                                               \
    while (0)                                                       \
    __pragma(warning(pop))
#endif

#define NG_LOG_TRACE(filterType__, ...) NG_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_TRACE, __VA_ARGS__)

#define NG_LOG_DEBUG(filterType__, ...) NG_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_DEBUG, __VA_ARGS__)

#define NG_LOG_INFO(filterType__, ...) NG_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_INFO, __VA_ARGS__)

#define NG_LOG_WARN(filterType__, ...) NG_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_WARN, __VA_ARGS__)

#define NG_LOG_ERROR(filterType__, ...) NG_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_ERROR, __VA_ARGS__)

#define NG_LOG_FATAL(filterType__, ...) NG_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_FATAL, __VA_ARGS__)

////////////////////////////////////////////////////////////////////