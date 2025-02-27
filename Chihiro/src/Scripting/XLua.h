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
#include "sol.hpp"

class Unit;
class XLua {
public:
    static XLua &Instance()
    {
        static XLua instance;
        return instance;
    }

    bool InitializeLua();
    ~XLua() = default;
    // Deleting the copy & assignment operators
    // Better safe than sorry
    XLua(const XLua &) = delete;
    XLua &operator=(const XLua &) = delete;

    bool RunString(Unit *, std::string, std::string &);
    bool RunString(Unit *, std::string);
    bool RunString(std::string);

private:
    template<typename T>
    sol::object return_object(T &&value)
    {
        sol::stack::push(m_pState.lua_state(), std::forward<T>(value));
        sol::object r = sol::stack::pop<sol::object>(m_pState.lua_state());
        return r;
    }

    // Monster
    void SCRIPT_SetWayPointType(int, int32_t);
    void SCRIPT_AddWayPoint(int, int, int32_t);
    void SCRIPT_RespawnRareMob(sol::variadic_args);
    void SCRIPT_RespawnRoamingMob(sol::variadic_args);
    void SCRIPT_RespawnGuardian(int, int, int, int, int, int, int, int32_t);
    void SCRIPT_AddRespawnInfo(sol::variadic_args);
    void SCRIPT_CPrint(sol::variadic_args);
    void SCRIPT_AddMonster(int, int, int, int32_t);

    // Summon
    int32_t SCRIPT_GetCreatureHandle(int32_t);
    sol::object SCRIPT_GetCreatureValue(int, std::string);
    void SCRIPT_SetCreatureValue(int, std::string, sol::object);
    void SCRIPT_CreatureEvolution(int32_t);

    // NPC
    int32_t SCRIPT_GetNPCID();
    void SCRIPT_DialogTitle(std::string);
    void SCRIPT_DialogText(std::string);
    void SCRIPT_DialogTextWithoutQuestMenu(std::string);
    void SCRIPT_DialogMenu(std::string, std::string);
    void SCRIPT_DialogShow();
    int32_t SCRIPT_GetQuestProgress(int32_t);
    void SCRIPT_StartQuest(int, sol::variadic_args);
    void SCRIPT_EndQuest(int, int, sol::variadic_args);
    void SCRIPT_ShowSoulStoneCraftWindow();
    void SCRIPT_ShowSoulStoneRepairWindow();
    void SCRIPT_OpenStorage();

    // Teleporter
    void SCRIPT_EnterDungeon(int32_t);
    int32_t SCRIPT_GetOwnDungeonID();
    int32_t SCRIPT_GetSiegeDungeonID();

    // Values
    int32_t SCRIPT_GetLocalFlag();
    int32_t SCRIPT_GetServerCategory();

    // Blacksmith
    int32_t SCRIPT_GetWearItemHandle(int32_t);
    int32_t SCRIPT_GetItemLevel(uint32_t);
    int32_t SCRIPT_GetItemEnhance(uint32_t);
    int32_t SCRIPT_SetItemLevel(uint32_t, int32_t);
    int32_t SCRIPT_GetItemPrice(uint32_t);
    int32_t SCRIPT_GetItemRank(uint32_t);
    int32_t SCRIPT_GetItemNameID(int32_t);
    int32_t SCRIPT_GetItemCode(uint32_t);
    int32_t SCRIPT_UpdateGoldChaos();
    int32_t SCRIPT_LearnAllSkill();
    void SCRIPT_LearnCreatureAllSkill(sol::variadic_args);
    void SCRIPT_DropItem(sol::variadic_args args);

    void SCRIPT_SavePlayer();
    uint32_t SCRIPT_InsertItem(sol::variadic_args);

    sol::object SCRIPT_GetValue(sol::variadic_args);
    void SCRIPT_SetValue(std::string, sol::variadic_args);

    std::string SCRIPT_GetFlag(std::string);
    void SCRIPT_SetFlag(sol::variadic_args args);
    void SCRIPT_WarpToRevivePosition(sol::variadic_args);

    void SCRIPT_AddState(sol::variadic_args args);
    void SCRIPT_AddCreatureState(sol::variadic_args args);

    sol::object SCRIPT_GetEnv(std::string);
    void SCRIPT_ShowMarket(std::string);

    void SCRIPT_InsertGold(sol::variadic_args);

    int32_t SCRIPT_GetProperChannelNum(int32_t) { return 0; }

    int32_t SCRIPT_GetLayerOfChannel(int, int32_t) { return 0; }

    std::string SCRIPT_Conv(sol::variadic_args);
    void SCRIPT_Message(std::string);
    void SCRIPT_SetCurrentLocationID(int32_t);

    void SCRIPT_Warp(sol::variadic_args);

    // Quest
    void SCRIPT_QuestInfo(int32_t code, sol::variadic_args args);

    Unit *m_pUnit{nullptr};
    sol::state m_pState{};

protected:
    XLua();
};

#define sScriptingMgr XLua::Instance()
