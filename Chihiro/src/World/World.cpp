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

#include "World.h"

#include "ClientPackets.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "FieldPropManager.h"
#include "GameContent.h"
#include "GameRule.h"
#include "GroupManager.h"
#include "ItemCollector.h"
#include "Log.h"
#include "Maploader.h"
#include "MemPool.h"
#include "Messages.h"
#include "NPC.h"
#include "ObjectMgr.h"
#include "Packets/PacketEpics.h"
#include "Player.h"
#include "Scripting/XLua.h"
#include "Skill.h"
#include "WorldSession.h"

std::atomic<bool> World::m_stopEvent{false};
std::atomic<uint32_t> World::m_worldLoopCounter{0};
uint8_t World::m_ExitCode{SHUTDOWN_EXIT_CODE};

World::World()
    : startTime(getMSTime())
{
    srand((uint32_t)time(nullptr));
}

World::~World() {}

uint32_t World::GetArTime()
{
    return GetMSTimeDiffToNow(startTime) / 10;
}

void World::InitWorld()
{
    NG_LOG_INFO("server.worldserver", "Initializing world...");
    LoadConfigSettings(false);

    uint32_t oldFullTime = getMSTime();
    NG_LOG_INFO("server.worldserver", "Initializing region system...");
    sRegion.InitRegion(sWorld.getIntConfig(CONFIG_MAP_WIDTH), sWorld.getIntConfig(CONFIG_MAP_HEIGHT));

    auto oldTime = getMSTime();
    NG_LOG_INFO("server.worldserver", "Initializing game content...");

    // Dörti häckz, plz ihgnoar
    s_nItemIndex = CharacterDatabase.Query("SELECT MAX(sid) FROM Item;").get()->Fetch()->GetUInt64();
    s_nPlayerIndex = CharacterDatabase.Query("SELECT MAX(sid) FROM `Character`;").get()->Fetch()->GetUInt64();
    s_nSkillIndex = CharacterDatabase.Query("SELECT MAX(sid) FROM `Skill`;").get()->Fetch()->GetUInt64();
    s_nSummonIndex = CharacterDatabase.Query("SELECT MAX(sid) FROM `Summon`;").get()->Fetch()->GetUInt64();
    s_nStateIndex = CharacterDatabase.Query("SELECT MAX(sid) FROM `State`;").get()->Fetch()->GetUInt64();
    sGroupManager.InitGroupSystem();

    sObjectMgr.InitGameContent();
    NG_LOG_INFO("server.worldserver", "Initialized game content in %u ms", GetMSTimeDiffToNow(oldTime));

    oldTime = getMSTime();
    NG_LOG_INFO("server.worldserver", "Initializing scripting...");
    sScriptingMgr.InitializeLua();
    sMapContent.LoadMapContent();
    sMapContent.InitMapInfo();
    NG_LOG_INFO("server.worldserver", "Initialized scripting in %u ms", GetMSTimeDiffToNow(oldTime));

    for (auto &ri : sObjectMgr.g_vRespawnInfo) {
        MonsterRespawnInfo nri(ri);
        float cx = (nri.right - nri.left) * 0.5f + nri.left;
        float cy = (nri.top - nri.bottom) * 0.5f + nri.bottom;
        auto ro = new RespawnObject{nri};
        m_vRespawnList.emplace_back(ro);
    }
    GameContent::AddNPCToWorld();

    NG_LOG_INFO("server.worldserver", "World fully initialized in %u ms!", GetMSTimeDiffToNow(oldFullTime));
}

void World::LoadConfigSettings(bool reload)
{
    if (reload) {
        std::string configError;
        if (!sConfigMgr->Reload(configError)) {
            NG_LOG_ERROR("server.worldserver", "World settings reload fail: can't read settings from %s.", sConfigMgr->GetFilename().c_str());
            return;
        }
        sLog->LoadFromConfig();
    }

    // Bool configs
    m_bool_configs[CONFIG_PK_SERVER] = sConfigMgr->GetBoolDefault("Game.PKServer", true);
    m_bool_configs[CONFIG_SERVICE_SERVER] = sConfigMgr->GetBoolDefault("Game.ServiceServer", false);
    m_bool_configs[CONFIG_DISABLE_TRADE] = sConfigMgr->GetBoolDefault("Game.DisableTrade", false);
    m_bool_configs[CONFIG_MONSTER_WANDERING] = sConfigMgr->GetBoolDefault("Game.MonsterWandering", true);
    m_bool_configs[CONFIG_MONSTER_COLLISION] = sConfigMgr->GetBoolDefault("Game.MonsterCollision", true);
    m_bool_configs[CONFIG_MONSTER_PATHFINDING] = sConfigMgr->GetBoolDefault("Game.MonsterCollision", false);
    m_bool_configs[CONFIG_IGNORE_RANDOM_DAMAGE] = sConfigMgr->GetBoolDefault("Game.IgnoreRandomDamage", false);
    m_bool_configs[CONFIG_NO_COLLISION_CHECK] = sConfigMgr->GetBoolDefault("Game.NoCollisionCheck", false);
    m_bool_configs[CONFIG_NO_SKILL_COOLTIME] = sConfigMgr->GetBoolDefault("Game.NoSkillCooltime", false);

    // int32_t configs
    m_int_configs[CONFIG_CELL_SIZE] = (uint32_t)sConfigMgr->GetIntDefault("Game.CellSize", 6);
    m_int_configs[CONFIG_MAP_REGION_SIZE] = (uint32_t)sConfigMgr->GetIntDefault("Game.RegionSize", 180);
    m_int_configs[CONFIG_MAP_WIDTH] = (uint32_t)sConfigMgr->GetIntDefault("Game.MapWidth", 700000);
    m_int_configs[CONFIG_MAP_HEIGHT] = (uint32_t)sConfigMgr->GetIntDefault("Game.MapHeight", 1000000);
    m_int_configs[CONFIG_REGION_SIZE] = (uint32_t)sConfigMgr->GetIntDefault("Game.RegionSize", 180);
    m_int_configs[CONFIG_TILE_SIZE] = (uint32_t)sConfigMgr->GetIntDefault("Game.TileSize", 42);
    m_int_configs[CONFIG_ITEM_HOLD_TIME] = (uint32_t)sConfigMgr->GetIntDefault("Game.ItemHoldTime", 18000);
    m_int_configs[CONFIG_LOCAL_FLAG] = (uint32_t)sConfigMgr->GetIntDefault("Game.LocalFlag", 4);
    m_int_configs[CONFIG_MAX_LEVEL] = (uint32_t)sConfigMgr->GetIntDefault("Game.MaxLevel", 150);
    m_int_configs[CONFIG_SERVER_INDEX] = (uint32_t)sConfigMgr->GetIntDefault("Game.ServerIndex", 1);

    // Float Configs
    setFloatConfig(CONFIG_MAP_LENGTH, sConfigMgr->GetFloatDefault("Game.MapLength", 16128.0f));

    // Rates
    rate_values[RATES_EXP] = sConfigMgr->GetFloatDefault("Game.EXPRate", 1.0f);
    rate_values[RATES_ITEM_DROP] = sConfigMgr->GetFloatDefault("Game.ItemDropRate", 1.0f);
    rate_values[RATES_CREATURE_DROP] = sConfigMgr->GetFloatDefault("Game.CreatureCardDropRate", 1.0f);
    rate_values[RATES_CHAOS_DROP] = sConfigMgr->GetFloatDefault("Game.ChaosDropRate", 1.0f);
    rate_values[RATES_GOLD_DROP] = sConfigMgr->GetFloatDefault("Game.GoldDropRate", 1.0f);
    rate_values[RATES_PVP_DAMAGE_FOR_PLAYER] = sConfigMgr->GetFloatDefault("Game.PVPDamageRateForPlayer", 1.0f);
    rate_values[RATES_PVP_DAMAGE_FOR_SUMMON] = sConfigMgr->GetFloatDefault("Game.PVPDamageRateForSummon", 1.0f);
    rate_values[RATES_STAMINA_BONUS] = sConfigMgr->GetFloatDefault("Game.StaminaBonusRate", 1.0f);
}

/// Find a session by its id
WorldSession *World::FindSession(uint32_t id) const
{
    SessionMap::const_iterator itr = m_sessions.find(id);

    if (itr != m_sessions.end())
        return itr->second; // also can return NULL for kicked session
    else
        return nullptr;
}

/// Remove a given session
bool World::RemoveSession(uint32_t id)
{
    ///- Find the session, kick the user, but we can't delete session at this moment to prevent iterator invalidation
    SessionMap::const_iterator itr = m_sessions.find(id);

    if (itr != m_sessions.end() && itr->second) {
        itr->second->KickPlayer();
    }

    return true;
}

void World::AddSession(WorldSession *s)
{
    addSessQueue.add(s);
}

uint64_t World::GetItemIndex()
{
    return ++s_nItemIndex;
}

uint64_t World::GetPlayerIndex()
{
    return ++s_nPlayerIndex;
}

uint64_t World::GetStateIndex()
{
    return ++s_nStateIndex;
}

uint64_t World::GetPetIndex()
{
    return ++s_nPetIndex;
}

uint64_t World::GetSummonIndex()
{
    return ++s_nSummonIndex;
}

uint64_t World::GetSkillIndex()
{
    return ++s_nSkillIndex;
}

bool World::SetMultipleMove(Unit *pUnit, Position curPos, std::vector<Position> newPos, uint8_t speed, bool bAbsoluteMove, uint32_t t, bool bBroadcastMove)
{
    Position oldPos{};
    bool result{false};
    if (bAbsoluteMove || true /* onSetMove Quadtreepotato*/) {
        oldPos.m_positionX = pUnit->GetPositionX();
        oldPos.m_positionY = pUnit->GetPositionY();
        oldPos.m_positionZ = pUnit->GetPositionZ();
        oldPos._orientation = pUnit->GetOrientation();

        pUnit->SetCurrentXY(curPos.GetPositionX(), curPos.GetPositionY());
        curPos.m_positionX = pUnit->GetPositionX();
        curPos.m_positionY = pUnit->GetPositionY();
        curPos.m_positionZ = pUnit->GetPositionZ();
        curPos.SetOrientation(pUnit->GetOrientation());

        onMoveObject(pUnit, oldPos, curPos);
        enterProc(pUnit, (uint32_t)(oldPos.GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(oldPos.GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)));
        pUnit->SetMultipleMove(newPos, speed, t);

        if (bBroadcastMove) {
            SetMoveFunctor fn;
            fn.obj = pUnit;
            sRegion.DoEachVisibleRegion((uint32_t)(pUnit->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)),
                (uint32_t)(pUnit->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), pUnit->GetLayer(), fn);

            if (pUnit->IsMonster()) {
                pUnit->As<Monster>()->m_bNearClient = fn.nCnt != 0;
            }
        }
        result = true;
    }
    return result;
}

bool World::SetMove(Unit *obj, Position curPos, Position newPos, uint8_t speed, bool bAbsoluteMove, uint32_t t, bool bBroadcastMove)
{
    Position oldPos{};
    Position curPos2{};

    if (bAbsoluteMove) {
        if (obj->bIsMoving && obj->IsInWorld()) {
            oldPos = obj->GetPosition();
            obj->SetCurrentXY(curPos.GetPositionX(), curPos.GetPositionY());
            curPos2 = obj->GetPosition();
            onMoveObject(obj, oldPos, curPos2);
            enterProc(obj, (uint32_t)(oldPos.GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(oldPos.GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)));
            obj->SetMove(newPos, speed, t);
        }
        else {
            obj->SetMove(newPos, speed, t);
        }
        if (bBroadcastMove) {
            SetMoveFunctor fn;
            fn.obj = obj;
            sRegion.DoEachVisibleRegion(
                (uint32_t)(obj->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(obj->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), obj->GetLayer(), fn);

            if (obj->IsMonster()) {
                obj->As<Monster>()->m_bNearClient = fn.nCnt != 0;
            }
        }
        return true;
    }
    return false;
}

void World::onMoveObject(WorldObject *pUnit, Position oldPos, Position newPos)
{
    auto prev_rx = (uint32_t)(oldPos.GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
    auto prev_ry = (uint32_t)(oldPos.GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
    if (prev_rx != (uint32_t)(newPos.GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)) || prev_ry != (uint32_t)(newPos.GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE))) {
        auto oldRegion = sRegion.GetRegion(prev_rx, prev_ry, pUnit->GetLayer());
        oldRegion->RemoveObject(pUnit);
        auto newRegion = sRegion.GetRegion(pUnit);
        newRegion->AddObject(pUnit);
    }
}

void World::enterProc(WorldObject *pUnit, uint32_t prx, uint32_t pry)
{
    auto rx = (uint32_t)(pUnit->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
    auto ry = (uint32_t)(pUnit->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
    if (rx != prx || ry != pry) {
        AddObjectFunctor fn(rx, ry, prx, pry, pUnit->GetLayer(), pUnit);
        fn.Run2();

        if (fn.bSend && pUnit->IsMonster()) {
            pUnit->As<Monster>()->m_bNearClient = true;
        }
        if (pUnit->IsPlayer()) {
            Messages::SendRegionAckMessage(dynamic_cast<Player *>(pUnit), rx, ry);
        }
    }
}

void World::AddObjectToWorld(WorldObject *obj)
{
    Region *region = sRegion.GetRegion(obj);
    if (region == nullptr)
        return;

    AddObjectFunctor rf(
        (uint32_t)(obj->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(obj->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), obj->GetLayer(), obj);
    rf.Run();

    if (obj->pRegion != nullptr)
        NG_LOG_INFO("server.worldserver", "Region not nullptr!!!");
    region->AddObject(obj);
}

void World::onRegionChange(WorldObject *obj, uint32_t update_time, bool bIsStopMessage)
{
    auto oldx = (uint32_t)(obj->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
    auto oldy = (uint32_t)(obj->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
    step(obj, (uint32_t)(update_time + obj->lastStepTime + (bIsStopMessage ? 0xA : 0)));

    if ((uint32_t)(obj->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)) != oldx || (uint32_t)(obj->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)) != oldy) {
        enterProc(obj, oldx, oldy);
    }
}

void World::RemoveObjectFromWorld(WorldObject *obj)
{
    // Create & set leave packet
    TS_SC_LEAVE leavePct{};
    leavePct.handle = obj->GetHandle();

    BroadcastFunctor<TS_SC_LEAVE> broadcastFunctor;
    broadcastFunctor.packet = leavePct;

    // Remove the object from the region
    sRegion.GetRegion(obj)->RemoveObject(obj);

    // Send one to each player in visible region
    sRegion.DoEachVisibleRegion((uint32_t)(obj->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(obj->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)),
        obj->GetLayer(), NG_REGION_FUNCTOR(broadcastFunctor), (uint8_t)RegionVisitor::ClientVisitor);
}

void World::step(WorldObject *obj, uint32_t tm)
{
    Position oldPos = obj->GetPosition();
    obj->Step(tm);
    Position newPos = obj->GetPosition();

    onMoveObject(obj, oldPos, newPos);
    obj->lastStepTime = tm;
}

bool World::onSetMove(WorldObject *pObject, Position curPos, Position lastpos)
{
    return true;
}

void World::Update(uint32_t diff)
{
    ///- Update Sessions
    UpdateSessions(diff);

    ///- Update for WorldObjects (Player, Monster, ...)
    sMemoryPool.Update(diff);

    ///- Temporary hack for respawn list
    ///- @todo Rewrite (re)spawning
    for (auto &ro : m_vRespawnList) {
        ro->Update(diff);
        // m_vRespawnList.erase(std::remove(m_vRespawnList.begin(), m_vRespawnList.end(), ro), m_vRespawnList.end());
    }

    for (auto &timer : m_timers) {
        if (timer.GetCurrent() >= 0)
            timer.Update(diff);
        else
            timer.SetCurrent(0);
    }

    /*
    if(m_timers[WUPDATE_WORLDLOCATION].Passed())
    {
        m_timers[WUPDATE_WORLDLOCATION].Reset();
        // @todo: Update worldlocation
    }
  */
}

void World::UpdateSessions(uint32_t diff)
{
    ///- Add new sessions
    WorldSession *sess = nullptr;
    while (addSessQueue.next(sess))
        AddSession_(sess);

    ///- Then send an update signal to remaining ones
    for (SessionMap::iterator itr = m_sessions.begin(), next; itr != m_sessions.end(); itr = next) {
        next = itr;
        ++next;

        ///- and remove not active sessions from the list
        WorldSession *pSession = itr->second;

        if (!pSession->Update(diff)) // As interval = 0
        {
            pSession->KickPlayer();
            m_sessions.erase(itr);
            delete pSession;
        }
    }
}

void World::AddSummonToWorld(Summon *pSummon)
{
    pSummon->SetFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
    // pSummon->AddToWorld();
    AddObjectToWorld(pSummon);
    // pSummon->m_bIsSummoned = true;
    pSummon->RemoveFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
}

void World::WarpBegin(Player *pPlayer)
{
    if (pPlayer->IsInWorld())
        RemoveObjectFromWorld(pPlayer);
    if (pPlayer->m_pMainSummon != nullptr && pPlayer->m_pMainSummon->IsInWorld())
        RemoveObjectFromWorld(pPlayer->m_pMainSummon);
    // Same for sub summon
    // same for pet
}

void World::WarpEnd(Player *pPlayer, Position pPosition, uint8_t layer)
{
    if (pPlayer == nullptr)
        return;

    uint32_t ct = GetArTime();

    if (layer != pPlayer->GetLayer()) {
        // TODO Layer management
    }
    pPlayer->SetCurrentXY(pPosition.GetPositionX(), pPosition.GetPositionY());
    pPlayer->StopMove();

    Messages::SendWarpMessage(pPlayer);
    if (pPlayer->m_pMainSummon != nullptr)
        WarpEndSummon(pPlayer, pPosition, layer, pPlayer->m_pMainSummon, false);

    ((Unit *)pPlayer)->SetFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
    AddObjectToWorld(pPlayer);
    pPlayer->RemoveFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
    Position pos = pPlayer->GetCurrentPosition(ct);
    SetMove(pPlayer, pos, pos, 0, true, ct, true);
    Messages::SendPropertyMessage(pPlayer, pPlayer, "channel", 0);
    pPlayer->ChangeLocation(pPlayer->GetPositionX(), pPlayer->GetPositionY(), false, true);
    pPlayer->Save(true);
}

void World::WarpEndSummon(Player *pPlayer, Position pos, uint8_t layer, Summon *pSummon, bool)
{
    uint32_t ct = GetArTime();
    if (pSummon == nullptr)
        return;
    pSummon->SetCurrentXY(pos.GetPositionX(), pos.GetPositionY());
    pSummon->SetLayer(layer);
    pSummon->StopMove();
    pSummon->SetFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
    pSummon->AddNoise(rand32(), rand32(), 35);
    AddObjectToWorld(pSummon);
    pSummon->RemoveFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
    SetMove(pSummon, pos, pos, 0, true, ct, true);
}

void World::AddMonsterToWorld(Monster *pMonster)
{
    pMonster->SetFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
    AddObjectToWorld(pMonster);
    pMonster->RemoveFlag(UNIT_FIELD_STATUS, STATUS_FIRST_ENTER);
}

void World::KickAll()
{
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr) {
        itr->second->KickPlayer();
    }
}

void World::addEXP(Unit *pCorpse, Player *pPlayer, int32_t exp, float jp)
{
    float fJP = 0;
    if (pPlayer->GetHealth() != 0) {
        float fJP = jp;
        // TODO: Remove immorality points
        if (pPlayer->GetInt32Value(PLAYER_FIELD_IP) > 0) {
            if (pCorpse->GetLevel() >= pPlayer->GetLevel()) {
                float fIPDec = -1.0f;
            }
        }
    }

    int32_t levelDiff = pPlayer->GetLevel() - pCorpse->GetLevel();
    if (levelDiff > 0) {
        exp = (1.0f - (float)levelDiff * 0.05f) * exp;
        fJP = (1.0f - (float)levelDiff * 0.05f) * jp;
    }

    uint32_t ct = GetArTime();
    Position posPlayer = pPlayer->GetCurrentPosition(ct);
    Position posCorpse = pCorpse->GetCurrentPosition(ct);
    if (posCorpse.GetExactDist2d(&posPlayer) <= 500.0f) {
        if (exp < 1.0f)
            exp = 1.0f;
        if (fJP < 0.0f)
            fJP = 0.0f;

        auto mob = dynamic_cast<Monster *>(pCorpse);
        if (pCorpse->IsMonster() && mob->GetTamer() == pPlayer->GetHandle()) {
            if (mob->m_bTamedSuccess) {
                exp *= mob->GetBase()->taming_exp_mod;
                jp *= mob->GetBase()->taming_exp_mod;
            }
        }
    }

    pPlayer->AddEXP(GameRule::GetIntValueByRandomInt64(GameRule::GetEXPRate() * exp), (uint32_t)GameRule::GetIntValueByRandomInt(GameRule::GetEXPRate() * jp), true);
}

void World::MonsterDropItemToWorld(Unit *pUnit, Item *pItem)
{
    if (pUnit == nullptr || pItem == nullptr)
        return;
    TS_SC_ITEM_DROP_INFO itemPct{};
    itemPct.item_handle = pItem->GetHandle();
    itemPct.monster_handle = pUnit->GetHandle();
    Broadcast(
        (uint32_t)(pItem->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(pItem->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), pItem->GetLayer(), itemPct);
    AddItemToWorld(pItem);
}

void World::AddItemToWorld(Item *pItem)
{
    sItemCollector.RegisterItem(pItem);
    AddObjectToWorld(pItem);
    pItem->m_nDropTime = GetArTime();
}

bool World::RemoveItemFromWorld(Item *pItem)
{
    if (sItemCollector.UnregisterItem(pItem)) {
        RemoveObjectFromWorld(pItem);
        pItem->m_nDropTime = 0;
        return true;
    }
    return false;
}

uint32_t World::procAddItem(Player *pClient, Item *pItem, bool bIsPartyProcess)
{
    uint32_t item_handle = 0;
    int32_t code = pItem->GetItemInstance().GetCode();
    Item *pNewItem{nullptr};
    if (code != 0 || (pClient->GetGold() + pItem->GetItemInstance().GetCount()) < MAX_GOLD_FOR_INVENTORY) {
        pItem->GetItemInstance().SetIdx(0);
        pItem->m_bIsNeedUpdateToDB = true;
        pNewItem = pClient->PushItem(pItem, pItem->GetItemInstance().GetCount(), false);
    }
    if (pNewItem != nullptr && pItem->GetItemInstance().GetCode() != 0)
        item_handle = pNewItem->GetHandle();
    else
        item_handle = pItem->GetHandle();

    if (pNewItem != nullptr && pNewItem->GetHandle() != pItem->GetHandle()) {
        Item::PendFreeItem(pItem);
    }
    return item_handle;
}

bool World::checkDrop(Unit *pKiller, int32_t code, int32_t percentage, float fDropRatePenalty, float fPCBangDropRateBonus)
{
    float fCreatureCardMod = 1.0f;
    float fMod = pKiller->GetItemChance() * 0.01f + 1.0f;
    if (code > 0) {
        if (sObjectMgr.GetItemBase(code)->eGroup == ItemGroup::GROUP_SUMMONCARD)
            fCreatureCardMod = getRate(RATES_CREATURE_DROP); /* Usually 1.0f on retail, but why not use it when it's available anyway?*/
    }

    return (irand(1, 100000000) > (percentage * fMod * GameRule::GetItemDropRate() * fDropRatePenalty * fPCBangDropRateBonus) * fCreatureCardMod) ? false : true;
}

int32_t World::ShowQuestMenu(Player *pPlayer)
{
    auto npc = sMemoryPool.GetObjectInWorld<NPC>(pPlayer->GetLastContactLong("npc"));
    if (npc != nullptr) {
        int32_t m_QuestProgress{0};
        auto functor = [&m_QuestProgress](Player *pPlayer, QuestLink *linkInfo) {
            std::string szBuf{};
            std::string szButtonName{};
            auto qbs = sObjectMgr.GetQuestBase(linkInfo->code);
            if ((qbs->nType != QuestType::QUEST_RANDOM_KILL_INDIVIDUAL && qbs->nType != QuestType::QUEST_RANDOM_COLLECT) || (m_QuestProgress != 0)) {
                int32_t qpid = linkInfo->nStartTextID;
                if (m_QuestProgress == 1)
                    qpid = linkInfo->nInProgressTextID;
                else if (m_QuestProgress == 2)
                    qpid = linkInfo->nEndTextID;
                szBuf = NGemity::StringFormat("quest_info( {}, {} )", linkInfo->code, qpid);
                szButtonName = NGemity::StringFormat("QUEST|{}|{}", qbs->nQuestTextID, m_QuestProgress);
                pPlayer->AddDialogMenu(szButtonName, szBuf);
            }
        };

        npc->DoEachStartableQuest(pPlayer, functor);
        m_QuestProgress = 1;
        npc->DoEachInProgressQuest(pPlayer, functor);
        m_QuestProgress = 2;
        npc->DoEachFinishableQuest(pPlayer, functor);
    }
    return 0;
}

bool World::ProcTame(Monster *pMonster)
{
    if (pMonster->GetTamer() == 0)
        return false;

    auto player = sMemoryPool.GetObjectInWorld<Player>(pMonster->GetTamer());
    if (player == nullptr || player->GetHealth() == 0) {
        Messages::BroadcastTamingMessage(nullptr, pMonster, 3);
        return false;
    }

    int32_t nTameItemCode = pMonster->GetTameItemCode();
    if (pMonster->GetExactDist2d(player) > 500.0f || nTameItemCode == 0) {
        ClearTamer(pMonster, false);
        Messages::BroadcastTamingMessage(player, pMonster, 3);
        return false;
    }

    Item *pItem = player->FindItem(nTameItemCode, (uint32_t)ITEM_FLAG_TAMING, true);
    if (pItem == nullptr) {
        NG_LOG_INFO("entities.skill", "ProcTame: A summon card used for taming is lost. [%s]", player->GetName());
        ClearTamer(pMonster, false);
        Messages::BroadcastTamingMessage(player, pMonster, 3);
        return false;
    }

    /*
     *
     * Technically there is a taming penalty added to the game.
     * However, since I'm not interested in having bs mechanics, I wont add it.
     * lPenalty = 0.05f * (float)((20 - pMonster->GetLevel()) + player->GetLevel());
     * lPenalty is multiplied with the TamePercentage here
     */
    float fTameProbability = pMonster->GetTamePercentage();
    auto pSkill = player->GetSkill(SKILL_CREATURE_TAMING);
    if (pSkill == nullptr) {
        // really, you shouldn't get here. If you do, you fucked up somewhere.
        ClearTamer(pMonster, false);
        Messages::BroadcastTamingMessage(player, pMonster, 3);
        return false;
    }

    fTameProbability *= (((pSkill->m_SkillBase->var[1] * pSkill->GetSkillEnhance()) + (pSkill->m_SkillBase->var[0] * pMonster->m_nTamingSkillLevel) + 1) * 1000000);
    NG_LOG_DEBUG("server.worldserver", "Taming success rate of %f percent.", fTameProbability / 1000000);
    if (fTameProbability < irand(1, 1000000)) {
        player->EraseItem(pItem, 1);
        ClearTamer(pMonster, false);
        Messages::BroadcastTamingMessage(player, pMonster, 3);
        return false;
    }

    pItem->GetItemInstance().SetFlag((pItem->GetItemInstance().GetFlag() & 0xDFFFFFFF) | 0x80000000);
    pItem->DBUpdate();
    Messages::SendItemMessage(player, pItem);
    Messages::BroadcastTamingMessage(player, pMonster, 2);
    ClearTamer(pMonster, false);
    return true;
}

void World::ClearTamer(Monster *pMonster, bool bBroadcastMsg)
{
    uint32_t tamer = pMonster->GetTamer();
    if (tamer != 0) {
        if (bBroadcastMsg)
            Messages::BroadcastTamingMessage(nullptr, pMonster, 1);

        auto player = sMemoryPool.GetObjectInWorld<Player>(tamer);
        if (player != nullptr) {
            player->m_hTamingTarget = 0;
        }
    }
    pMonster->SetTamer(0, 0);
}

bool World::SetTamer(Monster *pMonster, Player *pPlayer, int32_t nSkillLevel)
{
    if (pPlayer == nullptr || pPlayer->m_hTamingTarget != 0 || pMonster == nullptr)
        return false;

    int32_t tameCode = pMonster->GetTameItemCode();
    if (pMonster->GetHealth() == pMonster->GetMaxHealth() && pMonster->GetTamer() == 0 && tameCode != 0) {
        auto card = pPlayer->FindItem(tameCode, ITEM_FLAG_SUMMON, false);
        if (card != nullptr) {
            pMonster->SetTamer(pPlayer->GetHandle(), nSkillLevel);
            pPlayer->m_hTamingTarget = pMonster->GetHandle();
            card->GetItemInstance().SetFlag(card->GetItemInstance().GetFlag() | ITEM_FLAG_TAMING);
            Messages::BroadcastTamingMessage(pPlayer, pMonster, 0);
            return true;
        }
    }
    return false;
}

void World::AddSession_(WorldSession *s)
{
    ASSERT(s);

    if (!RemoveSession(s->GetAccountId())) {
        s->KickPlayer();
        delete s;
        return;
    }

    m_sessions[s->GetAccountId()] = s;
}

void World::addChaos(Unit *pCorpse, Player *pPlayer, float chaos)
{
    if (pPlayer == nullptr || pCorpse == nullptr || pPlayer->GetChaos() >= pPlayer->GetMaxChaos())
        return;

    uint32_t ct = GetArTime();
    Position playerPos = pPlayer->GetCurrentPosition(ct);
    Position corpsePos = pCorpse->GetCurrentPosition(ct);
    if (corpsePos.GetExactDist2d(&playerPos) <= 500.0f) {
        int32_t nChaos = GameRule::GetIntValueByRandomInt(chaos);

        if (chaos > 0.0f) {
            TS_SC_GET_CHAOS chaosPct{};
            chaosPct.hPlayer = pPlayer->GetHandle();
            chaosPct.hCorpse = pCorpse->GetHandle();
            chaosPct.nChaos = nChaos;
            chaosPct.nBonus = 0;
            chaosPct.nBonusPercent = 0;
            chaosPct.nBonusType = 0;
            Broadcast((uint32_t)(pCorpse->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(pCorpse->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)),
                pCorpse->GetLayer(), chaosPct);
            pPlayer->AddChaos(nChaos);
        }
    }
}

void World::addEXP(Unit *pCorpse, int32_t nPartyID, int32_t exp, float jp)
{
    int32_t nMinLevel = 255;
    int32_t nMaxLevel = 0;
    int32_t nTotalLevel = 0;
    int32_t nCount = 0;
    int32_t nTotalCount = 0;
    float fLevelPenalty = 0;
    Player *pOneManPlayer{nullptr};
    sGroupManager.DoEachMemberTag(nPartyID, [&pCorpse, &nMinLevel, &nMaxLevel, &nTotalLevel, &nCount, &nTotalCount, &pOneManPlayer](PartyMemberTag &tag) {
        if (tag.bIsOnline && tag.pPlayer != nullptr) {
            nTotalCount++;
            if (tag.pPlayer->IsInWorld() && pCorpse->GetLayer() == tag.pPlayer->GetLayer() && pCorpse->GetExactDist2d(tag.pPlayer) <= 500.0f) {
                pOneManPlayer = tag.pPlayer;
                int32_t l = tag.pPlayer->GetLevel();
                if (nMaxLevel < l)
                    nMaxLevel = l;
                if (nMinLevel > l)
                    nMinLevel = l;
                nTotalLevel += l;
                nCount++;
            }
        }
    });

    if (nCount >= 1) {
        if (nCount < 2) {
            addEXP(pCorpse, pOneManPlayer, exp, jp);
            return;
        }

        int32_t levelDiff = nMaxLevel - nMinLevel;
        if (levelDiff < nTotalCount + 40) {
            if (levelDiff >= nTotalCount + 5) {
                fLevelPenalty = levelDiff - nCount - 5;
                fLevelPenalty = 1.0f - (float)pow(fLevelPenalty, 1.1) * 0.02f;
                exp = (int32_t)(exp * fLevelPenalty);
                jp = (int32_t)(jp * fLevelPenalty);
            }
        }
        else {
            exp = 0;
            jp = 0;
        }
        float lp = fLevelPenalty * 0.01f + 1.0f;
        auto nSharedEXP = (int32_t)(exp * lp);
        auto nSharedJP = (int32_t)(jp * lp);
        sGroupManager.DoEachMemberTag(nPartyID, [this, &nTotalLevel, &nSharedEXP, &nSharedJP, &nMaxLevel, &pCorpse](PartyMemberTag &tag) {
            if (tag.bIsOnline && tag.pPlayer != nullptr) {
                float ratio = (float)tag.pPlayer->GetLevel() / nTotalLevel;
                float fEXP = nSharedEXP * ratio;
                float fJP = nSharedJP * ratio;
                float penalty = 1.0f - 0.1f * ((float)(nMaxLevel - tag.pPlayer->GetLevel()) * 0.1f);
                penalty = std::max(0.0f, penalty >= 1.0f ? 1.0f : penalty);
                fEXP = (penalty * fEXP) * 1.0f; // @todo: partyexprate
                fJP = (penalty * fJP) * 1.0f;
                if (fEXP < 1.0f)
                    fEXP = 1.0f;
                addEXP(pCorpse, tag.pPlayer, fEXP, fJP);
            }
        });
    }
}

void World::procPartyShare(Player *pClient, Item *pItem)
{
    if (pClient == nullptr || pItem == nullptr)
        return;

    if (pClient->GetPartyID() == 0)
        return;

    auto mode = sGroupManager.GetShareMode(pClient->GetPartyID());
    if (mode == ITEM_SHARE_MODE::ITEM_SHARE_MONOPOLY) {
        procAddItem(pClient, pItem, true);
    }
    else if (mode == ITEM_SHARE_MODE::ITEM_SHARE_RANDOM) {
        std::vector<Player *> vList{};
        sGroupManager.GetNearMember(pClient, 500.0f, vList);
        auto idx = irand(0, (int32_t)vList.size() - 1);
        procAddItem(vList[idx], pItem, true);
    }
}

void World::EnumMovableObject(Position pos, uint8_t layer, float range, std::vector<uint32_t> &pvResult, bool bIncludeClient, bool bIncludeNPC)
{
    EnumMovableObjectRegionFunctor fn(pvResult, pos, range, bIncludeClient, bIncludeNPC);
    sRegion.DoEachVisibleRegion((uint32_t)(pos.GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(pos.GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), layer, fn);
}

void World::MoveObject(Unit *pObject, Position &newPos, float face)
{
    uint32_t tm = GetArTime();

    Position oldPos = pObject->GetPosition();

    auto rx = newPos.GetRX();
    auto ry = newPos.GetRY();
    auto prx = pObject->GetRX();
    auto pry = pObject->GetRY();

    pObject->SetCurrentXY(newPos.GetPositionX(), newPos.GetPositionY());
    pObject->SetOrientation(face);
    pObject->StopMove();

    Position _newPos = newPos;

    onMoveObject(pObject, oldPos, _newPos);

    pObject->lastStepTime = tm;

    if (prx != rx || pry != ry)
        enterProc(pObject, prx, pry);
}