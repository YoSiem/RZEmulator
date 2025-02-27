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
#include <string>
#include <vector>

#include "Define.h"

class ConfigMgr {
    ConfigMgr() = default;
    ConfigMgr(ConfigMgr const &) = delete;
    ConfigMgr &operator=(ConfigMgr const &) = delete;
    ~ConfigMgr() = default;

public:
    struct CachedConfig {
        int packetVersion;
    };

public:
    /// Method used only for loading main configuration files (bnetserver.conf and worldserver.conf)
    bool LoadInitial(std::string const &file, std::vector<std::string> args, std::string &error);

    static ConfigMgr *instance();

    bool Reload(std::string &error);

    std::string GetStringDefault(std::string const &name, const std::string &def) const;
    bool GetBoolDefault(std::string const &name, bool def) const;
    int GetIntDefault(std::string const &name, int def) const;
    float GetFloatDefault(std::string const &name, float def) const;
    void SetPacketVersion(int packetVersion) { cachedConfig.packetVersion = packetVersion; }
    int GetPacketVersion() const { return cachedConfig.packetVersion; }

    std::string const &GetFilename();
    std::vector<std::string> const &GetArguments() const;
    std::vector<std::string> GetKeysByString(std::string const &name);
    std::string GetCorrectPath(std::string path);

    const CachedConfig &getCachedConfig() { return cachedConfig; }

private:
    template<class T>
    T GetValueDefault(std::string const &name, T def) const;

    CachedConfig cachedConfig;
};

#define sConfigMgr ConfigMgr::instance()
