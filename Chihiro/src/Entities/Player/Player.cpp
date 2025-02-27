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

#include "Player.h"

#include "ClientPackets.h"
#include "DatabaseEnv.h"
#include "DungeonManager.h"
#include "GameContent.h"
#include "GameRule.h"
#include "GroupManager.h"
#include "MemPool.h"
#include "Messages.h"
#include "NPC.h"
#include "ObjectMgr.h"
#include "RegionContainer.h"
#include "Scripting/XLua.h"
#include "Skill.h"
#include "World.h"
#include "WorldLocation.h"
#include "WorldSession.h"

// we can disable this warning for this since it only
// causes undefined behavior when passed to the base class constructor
#ifdef _MSC_VER
#pragma warning(disable : 4355)
#endif

Player::Player(uint32_t handle)
    : Unit(true)
    , m_TS(TimeSynch(200, 2, 10))
    , m_session(nullptr)
{
#ifdef _MSC_VER
#pragma warning(default : 4355)
#endif
    _mainType = MT_Player;
    _subType = ST_Player;
    _objType = OBJ_CLIENT;
    _valuesCount = PLAYER_END;

    _InitValues();
    _InitTimerFieldsAndStatus();

    SetUInt32Value(PLAYER_LAST_STAMINA_UPDATE_TIME, sWorld.GetArTime());
    SetUInt32Value(UNIT_FIELD_HANDLE, handle);
    clearPendingBonusMsg();

    m_QuestManager.m_pHandler = this;
    m_Inventory.m_pEventReceiver = this;
    m_Storage.m_pEventReceiver = this;
}

Player::~Player()
{
    /*if(m_pSubSummon != nullptr) {
        sWorld.RemoveObjectFromWorld(m_pSubSummon);
        m_pSubSummon = nullptr;
    }*/
}

void Player::CleanupsBeforeDelete()
{
    CharacterDatabase.DirectPExecute("UPDATE `Character` SET logout_time = NOW() WHERE sid = %u", GetUInt32Value(UNIT_FIELD_UID));
    if (IsInWorld()) {
        RemoveAllSummonFromWorld();
        sWorld.RemoveObjectFromWorld(this);
    }

    if (GetPartyID() != 0)
        sGroupManager.onLogout(GetPartyID(), this);

    for (auto &t : m_vStorageSummonList) {
        if (t != nullptr) {
            auto pos = std::find(m_vSummonList.begin(), m_vSummonList.end(), t);
            if (pos != m_vSummonList.end())
                m_vSummonList.erase(pos);
        }
        t->DeleteThis();
    }

    for (auto &t : m_Inventory.m_vList) {
        Item::PendFreeItem(t);
    }
    m_Inventory.m_vList.clear();
    m_Inventory.m_vExpireItemList.clear();

    for (auto &t : m_Storage.m_vList) {
        Item::PendFreeItem(t);
    }
    m_Storage.m_vList.clear();
    m_Storage.m_vExpireItemList.clear();

    for (auto &t : m_vSummonList) {
        if (t != nullptr) {
            State::DB_ClearState(t);
            for (auto &state : t->m_vStateList)
                State::DB_InsertState(t, state);
            t->DeleteThis();
        }
    }
    m_vSummonList.clear();

    State::DB_ClearState(this);
    for (auto &state : m_vStateList)
        State::DB_InsertState(this, state);

    for (auto &q : m_QuestManager.m_vActiveQuest) {
        Quest::DB_Insert(this, q);
        delete q;
    }
    m_QuestManager.m_vActiveQuest.clear();
}

void Player::EnterPacket(TS_SC_ENTER &pEnterPct, Player *pPlayer, Player *pReceiver)
{
    TS_SC_ENTER__PLAYER_INFO playerInfo{};
    Unit::EnterPacket(playerInfo.creatureInfo, pPlayer, pReceiver);
    playerInfo.sex = pPlayer->GetInt32Value(UNIT_FIELD_SEX);
    playerInfo.faceId = pPlayer->GetInt32Value(UNIT_FIELD_MODEL + 1);
    playerInfo.hairId = pPlayer->GetInt32Value(UNIT_FIELD_MODEL);
    playerInfo.szName = pPlayer->GetNameAsString();
    playerInfo.job_id = pPlayer->GetCurrentJob();
    playerInfo.ride_handle = pPlayer->GetRideHandle();
    playerInfo.guild_id = pPlayer->GetInt32Value(PLAYER_FIELD_GUILD_ID);
    pEnterPct.playerInfo = playerInfo;

    if (pPlayer->GetRideHandle() != 0)
        Messages::sendEnterMessage(pPlayer, pPlayer->GetRideObject(), true);
}

bool Player::ReadCharacter(const std::string &_name, int32_t _race)
{
    int32_t mainSummon{0};
    int32_t subSummon{0};

    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_CHARACTER);
    stmt->setString(0, _name);
    stmt->setInt32(1, m_session->GetAccountId());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        SetName(_name);
        SetUInt32Value(UNIT_FIELD_UID, (*result)[0].GetUInt32());
        m_szAccount = (*result)[1].GetString();
        int32_t permission = (*result)[2].GetInt32();
        if (m_session->m_nPermission > permission)
            permission = m_session->m_nPermission;
        SetInt32Value(PLAYER_FIELD_PERMISSION, permission);
        SetInt32Value(PLAYER_FIELD_PARTY_ID, (*result)[3].GetInt32());
        SetInt32Value(PLAYER_FIELD_GUILD_ID, (*result)[4].GetInt32());
        Relocate((float)(*result)[5].GetInt32(), (float)(*result)[6].GetInt32(), (float)(*result)[7].GetInt32(), 0);
        SetLayer((*result)[8].GetUInt8());
        SetInt32Value(UNIT_FIELD_RACE, (*result)[9].GetInt32());
        SetInt32Value(UNIT_FIELD_SEX, (*result)[10].GetInt32());
        SetInt32Value(UNIT_FIELD_LEVEL, (*result)[11].GetInt32());
        SetUInt64Value(UNIT_FIELD_EXP, (int32_t)(*result)[12].GetUInt64());
        SetInt32Value(UNIT_FIELD_HEALTH, (*result)[13].GetInt32());
        SetInt32Value(UNIT_FIELD_MANA, (*result)[14].GetInt32());
        SetInt32Value(UNIT_FIELD_STAMINA, (*result)[15].GetInt32());
        SetInt32Value(UNIT_FIELD_HAVOC, (*result)[16].GetInt32());
        SetInt32Value(UNIT_FIELD_JOB_DEPTH, (*result)[17].GetInt8());
        SetInt32Value(UNIT_FIELD_JOBPOINT, (*result)[18].GetInt32());
        for (int32_t i = 0; i < 3; i++) {
            SetInt32Value(UNIT_FIELD_PREV_JOB + i, (*result)[19 + i].GetInt32());
            SetInt32Value(UNIT_FIELD_PREV_JLV + i, (*result)[22 + i].GetInt32());
        }
        SetInt32Value(PLAYER_FIELD_IP, (int32_t)(*result)[25].GetFloat());
        SetInt32Value(PLAYER_FIELD_CHA, (*result)[26].GetInt32());
        SetInt32Value(PLAYER_FIELD_PKC, (*result)[27].GetInt32());
        SetInt32Value(PLAYER_FIELD_DKC, (*result)[28].GetInt32());
        for (int32_t i = 0; i < 6; i++) {
            SetInt32Value(PLAYER_FIELD_SUMMON + i, (*result)[29 + i].GetInt32());
            SetInt32Value(PLAYER_FIELD_BELT + i, (*result)[41 + i].GetInt32());
        }
        SetInt32Value(UNIT_FIELD_SKIN_COLOR, (*result)[35].GetInt32());
        for (int32_t i = 0; i < 5; i++) {
            SetInt32Value(UNIT_FIELD_MODEL + i, (*result)[36 + i].GetInt32());
        }
        SetUInt64Value(PLAYER_FIELD_GOLD, (*result)[47].GetUInt64());
        SetInt32Value(PLAYER_FIELD_CHAOS, (*result)[48].GetInt32());
        std::string flag_list = (*result)[50].GetString();
        Tokenizer token(flag_list, '\n');
        for (auto iter : token) {
            Tokenizer flag(iter, ':');
            if (flag.size() == 2) {
                SetCharacterFlag(flag[0], flag[1]);
            }
        }
        mainSummon = (*result)[51].GetInt32();
        subSummon = (*result)[52].GetInt32();
        SetInt32Value(PLAYER_FIELD_REMAIN_SUMMON_TIME, (*result)[53].GetInt32());
        SetInt32Value(PLAYER_FIELD_PET, (*result)[54].GetInt32());
        SetUInt64Value(PLAYER_FIELD_CHAT_BLOCK_TIME, (*result)[55].GetUInt64());
        SetUInt64Value(PLAYER_FIELD_GUILD_BLOCK_TIME, (*result)[56].GetUInt64());
        SetInt32Value(PLAYER_FIELD_PK_MODE, (*result)[57].GetInt8());
        SetInt32Value(UNIT_FIELD_JOB, (*result)[58].GetInt32());
        SetInt32Value(UNIT_FIELD_JLV, (*result)[59].GetInt32());
        m_szClientInfo = (*result)[60].GetString();
        SetInt32Value(PLAYER_FIELD_ACCOUNT_ID, m_session->GetAccountId());

        if (GetLevel() == 0) {
            SetLevel(1);
            SetCurrentJLv(1);
        }
        /*if (!ReadWearInfo(GetInt32Value(UNIT_FIELD_HANDLE)))
        {
            return false;
        }*/
        if (!ReadItemList(GetInt32Value(UNIT_FIELD_UID)) || !ReadItemCoolTimeList(GetInt32Value(UNIT_FIELD_UID)) || !ReadSummonList(GetInt32Value(UNIT_FIELD_UID))) {
            return false;
        }

        if (!ReadEquipItem() || !ReadSkillList(this) || !ReadQuestList() || !ReadStateList(this))
            return false;

        int32_t nSummonIdx = 0;
        CalculateStat();
        for (int32_t i = 0; i < 6; ++i) {
            if (GetInt32Value(PLAYER_FIELD_SUMMON + i) != 0) {
                auto pSummon = GetSummon(GetInt32Value(PLAYER_FIELD_SUMMON + i));
                if (pSummon != nullptr) {
                    // nSummonHP[nSummonIdx] = pSummon.m_nHP;
                    pSummon->m_cSlotIdx = (uint8_t)nSummonIdx;
                    // nSummonMP[nSummonIdx] = pSummon.m_fMP;
                    pSummon->CalculateStat();
                    m_aBindSummonCard[nSummonIdx] = pSummon->m_pItem;
                    nSummonIdx++;
                }
                else {
                    NG_LOG_ERROR("entities.player", "Invalid Summon Bind!");
                }
            }
        }

        if (mainSummon != 0) {
            m_pMainSummon = GetSummon(mainSummon);
        }

        CalculateStat();
        Messages::SendHPMPMessage(this, this, GetHealth(), GetMana(), true);
        // Messages::sendEnterMessage(this, this, false);
    }
    else {
        return false;
    }
    return true;
}

void Player::DB_ReadStorage(bool bReload)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_STORAGE);
    stmt->setInt32(0, GetInt32Value(PLAYER_FIELD_ACCOUNT_ID));
    int32_t nItemIndex = 0;
    std::vector<Summon *> vList{};
    ReadStorageSummonList(vList);
    bool bIsGoldExist{false};
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            int32_t fidx = 0;
            Field *field = result->Fetch();
            int64_t sid = field[fidx++].GetInt64();
            int32_t idx = field[fidx++].GetInt32();
            int32_t code = field[fidx++].GetInt32();
            int64_t cnt = field[fidx++].GetInt64();
            int32_t level = field[fidx++].GetInt32();
            int32_t enhance = field[fidx++].GetInt32();
            int32_t endurance = field[fidx++].GetInt32();
            int32_t flag = field[fidx++].GetInt32();
            auto genCode = (GenerateCode)field[fidx++].GetInt32();
            int32_t socket_0 = field[fidx++].GetInt32();
            int32_t socket_1 = field[fidx++].GetInt32();
            int32_t socket_2 = field[fidx++].GetInt32();
            int32_t socket_3 = field[fidx++].GetInt32();
            int32_t remain_time = field[fidx].GetInt32();

            Item *curItem = m_Storage.FindBySID(sid);
            if (curItem == nullptr)
                curItem = m_Inventory.FindBySID(sid);

            if (bReload && code != 0) {
                auto pPrevItem = m_Storage.FindBySID(sid);
                if (pPrevItem == nullptr) {
                    pPrevItem = m_Inventory.FindBySID(sid);
                    ASSERT(pPrevItem == nullptr, "ReadStorageList: Item disappeared from storage/inventory!!!");
                }

                if (pPrevItem != nullptr) {
                    pPrevItem->SetIdx(++nItemIndex);
                    if (pPrevItem->GetIdx() == idx)
                        pPrevItem->m_bIsNeedUpdateToDB = false;

                    Messages::SendItemMessage(this, pPrevItem);
                    continue;
                }
            }

            Item *newItem = Item::AllocItem(sid, code, cnt, genCode, level, enhance, flag, socket_0, socket_1, socket_2, socket_3, remain_time);
            if (newItem == nullptr) {
                NG_LOG_ERROR("entities.player", "ItemID Invalid! %d", code);
                continue;
            }
            newItem->GetItemInstance().SetFlag(newItem->GetItemInstance().GetFlag() & ITEM_FLAG_TAMING);
            newItem->SetCurrentEndurance(endurance);
            newItem->SetOwnerInfo(GetHandle(), 0, GetAccountID());
            if (code == 0) {
                if (bReload) {
                    bIsGoldExist = true;
                    newItem->DeleteThis();
                    continue;
                }

                if (ChangeStorageGold(GetStorageGold() + cnt) == TS_RESULT_SUCCESS) {
                    if (bIsGoldExist) {
                        newItem->SetOwnerInfo(0, 0, 0);
                        newItem->DBUpdate();
                        DB_UpdateStorageGold();
                    }
                    else {
                        bIsGoldExist = true;
                        SetUInt64Value(PLAYER_FIELD_STORAGE_GOLD_SID, newItem->GetItemUID());
                    }
                }
                else {
                    NG_LOG_ERROR("entites.player", "DB_ReadStorageList failed!!!");
                }
                newItem->DeleteThis();
                continue;
            }

            newItem->SetIdx(++nItemIndex);
            if (newItem->GetIdx() == idx)
                newItem->m_bIsNeedUpdateToDB = false;

            auto pJoinableItem = m_Storage.FindByCode(newItem->GetItemCode());
            if (newItem->IsJoinable() && pJoinableItem != nullptr) {
                pJoinableItem->SetCount(pJoinableItem->GetCount() + newItem->GetCount());
                newItem->SetOwnerInfo(0, 0, 0);
                newItem->DBUpdate();
                newItem->DeleteThis();

                pJoinableItem->DBUpdate();

                --nItemIndex;
                Messages::SendItemMessage(this, pJoinableItem);
            }
            else {
                if (newItem->IsSummonCard()) {
                    for (auto it = m_vSummonList.begin(); it != m_vSummonList.end(); ++it) {
                        if ((*it)->GetSummonSID() == newItem->GetSummonSID()) {
                            auto pSummon = (*it);
                            m_vSummonList.erase(it);

                            pSummon->m_pItem = newItem;
                            newItem->SetSummonSID(pSummon->GetSummonSID());
                            // ReadCreatureSkillList

                            pSummon->SetFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE);
                            break;
                        }
                    }
                }

                if (newItem->GetIdx() == 0)
                    newItem->SetIdx(m_Storage.IssueNewIndex());

                m_Storage.Push(newItem, newItem->GetCount(), false);
            }
        } while (result->NextRow());
    }

    if (!bIsGoldExist) {
        auto storageItem = Item::AllocItem(sWorld.GetItemIndex(), 0, 0, GenerateCode::BY_BASIC);
        SetUInt64Value(PLAYER_FIELD_STORAGE_GOLD_SID, storageItem->GetItemUID());
        storageItem->SetOwnerInfo(GetHandle(), 0, GetAccountID());
        storageItem->DBInsert();
        storageItem->DeleteThis();
        DB_UpdateStorageGold();
    }

    m_bIsStorageLoaded = true;
    OpenStorage();
}

bool Player::ReadItemList(int32_t sid)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_ITEMLIST);
    stmt->setInt32(0, sid);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        int32_t unInv = 0;
        int32_t Inv = 0;
        do {
            Field *fields = result->Fetch();
            int32_t i = 0, invIdx = 0;

            uint64_t uid = fields[i++].GetUInt64();
            int32_t idx = fields[i++].GetInt32();
            int32_t code = fields[i++].GetInt32();
            uint64_t cnt = fields[i++].GetUInt64();
            int32_t gcode = fields[i++].GetInt32();
            int32_t level = fields[i++].GetInt32();
            int32_t enhance = fields[i++].GetInt32();
            int32_t flag = fields[i++].GetInt32();
            int32_t summon_id = fields[i++].GetInt32();
            int32_t socket_0 = fields[i++].GetInt32();
            int32_t socket_1 = fields[i++].GetInt32();
            int32_t socket_2 = fields[i++].GetInt32();
            int32_t socket_3 = fields[i++].GetInt32();
            int32_t remain_time = fields[i++].GetInt32();

            auto item = Item::AllocItem(uid, code, cnt, (GenerateCode)gcode, level, enhance, flag, socket_0, socket_1, socket_2, socket_3, remain_time);
            if (item == nullptr) {
                NG_LOG_ERROR("entities.player", "ItemID Invalid! %d", code);
                continue;
            }
            item->GetItemInstance().SetFlag(item->GetItemInstance().GetFlag() & 0xDFFFFFFF);
            // item->SetCurrentEndurance(endurance);

            if (code != 0) {
                item->GetItemInstance().SetWearInfo((ItemWearType)fields[i].GetInt32());
                item->m_unInventoryIndex = (uint32_t)unInv;
                unInv++;
                Inv++;
                item->GetItemInstance().SetIdx(Inv);
                item->m_bIsNeedUpdateToDB = idx != Inv;
                Item *citem = PushItem(item, item->GetItemInstance().GetCount(), false);
                if (item->IsJoinable() && citem->GetHandle() != item->GetHandle()) {
                    item->SetOwnerInfo(0, 0, 0);
                    item->DBUpdate();
                    Item::PendFreeItem(item);
                    citem->DBUpdate();
                    Inv--;
                }
            }
            else {
                item->SetOwnerInfo(0, 0, 0);
                item->DBUpdate();
                Item::PendFreeItem(item);
            }
        } while (result->NextRow());
    }
    return true;
}

bool Player::ReadStateList(Unit *pUnit)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_STATE);
    auto uid = pUnit->GetUInt32Value(UNIT_FIELD_UID);
    stmt->setInt32(0, pUnit->IsPlayer() ? uid : 0);
    stmt->setInt32(1, pUnit->IsSummon() ? uid : 0);
    uint32_t ct = sWorld.GetArTime();
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            Field *fields = result->Fetch();
            int32_t idx = 3;

            uint32_t duration[3] = {0};
            uint16_t levels[3] = {0};
            int32_t base_damage[3] = {0};
            int32_t remain_times[3] = {0};

            int32_t code = fields[idx++].GetInt32();
            levels[0] = fields[idx++].GetUInt16();
            int32_t level_2 = fields[idx++].GetInt32();
            int32_t level_3 = fields[idx++].GetInt32();
            duration[0] = fields[idx++].GetUInt32();
            int32_t duration_2 = fields[idx++].GetInt32();
            int32_t duration_3 = fields[idx++].GetInt32();
            remain_times[0] = fields[idx++].GetUInt32();
            int32_t remain_time_2 = fields[idx++].GetInt32();
            int32_t remain_time_3 = fields[idx++].GetInt32();
            base_damage[0] = fields[idx++].GetInt32();
            int32_t base_damage_2 = fields[idx++].GetInt32();
            int32_t base_damage_3 = fields[idx++].GetInt32();
            int32_t remain_fire_time = fields[idx].GetInt32();
            State *state = new State{};
            auto si = sObjectMgr.GetStateInfo(code);
            m_nCurrentStateUID++;
            state->SetState(
                code, m_nCurrentStateUID, pUnit->GetHandle(), levels, duration, remain_times, (uint32_t)(sWorld.GetArTime() + remain_fire_time - 100 * si->fire_interval), base_damage, 0, "");
            sMemoryPool.AllocMiscHandle(state);
            pUnit->m_vStateList.emplace_back(state);
        } while (result->NextRow());
    }
    return true;
}

bool Player::ReadItemCoolTimeList(int32_t uid)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_ITEMCOOLTIME);
    stmt->setInt32(0, uid);
    uint32_t ct = sWorld.GetArTime();
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            Field *fields = result->Fetch();
            int32_t idx = 1;
            for (auto &cd : m_nItemCooltime) {
                cd = fields[idx++].GetInt32() + ct;
            }
        } while (result->NextRow());
    }
    return true;
}

bool Player::ReadQuestList()
{
    {
        PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_QUEST);
        stmt->setInt32(0, GetUInt32Value(UNIT_FIELD_UID));
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
            do {
                Field *fields = result->Fetch();
                int32_t idx = 0;
                int32_t nID = fields[idx++].GetInt32();
                int32_t Code = fields[idx++].GetInt32();
                int32_t nStartID = fields[idx++].GetInt32();
                int32_t nStatus[MAX_QUEST_STATUS] = {0, 0, 0};

                for (int32_t &nStatu : nStatus)
                    nStatu = fields[idx++].GetInt32();

                auto progress = (QuestProgress)fields[idx].GetInt32();
                auto q = Quest::AllocQuest(this, nID, Code, nStatus, progress, nStartID);
                if (!m_QuestManager.AddQuest(q)) {
                    delete q;
                    NG_LOG_ERROR("entities.player", "Player::ReadQuestList: Failed to alloc Quest!");
                    return false;
                }
            } while (result->NextRow());
        }
    }

    {
        PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_MAX_QUEST_ID);
        stmt->setInt32(0, GetUInt32Value(UNIT_FIELD_UID));
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
            int32_t idx = result->Fetch()[0].GetInt32();
            m_QuestManager.SetMaxQuestID(idx);
        }
    }
    return true;
}

bool Player::ReadSkillList(Unit *pUnit)
{
    PreparedStatement *stmt{nullptr};
    if (pUnit->IsPlayer())
        stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_SKILL);
    else
        stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_SUMMONSKILL);
    stmt->setInt32(0, pUnit->GetInt32Value(UNIT_FIELD_UID));
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            Field *fields = result->Fetch();
            auto sid = fields[0].GetInt32();
            // idx 1 = owner_id, idx 2 = summon_id
            auto skill_id = fields[3].GetInt32();
            auto skill_level = fields[4].GetInt32();
            auto cool_time = fields[5].GetInt32();
            pUnit->SetSkill(sid, skill_id, skill_level, cool_time);
        } while (result->NextRow());
    }
    if (pUnit->IsPlayer()) {
        SetSkill(-1, 6001, 20, 0);
        SetSkill(-1, 6002, 20, 0);
        SetSkill(-1, 6003, 20, 0);
        SetSkill(-1, 6004, 20, 0);
        SetSkill(-1, 6005, 20, 0);
        SetSkill(-1, 6006, 20, 0);
        SetSkill(-1, 6007, 20, 0);
        SetSkill(-1, 6008, 20, 0);
        SetSkill(-1, 6009, 20, 0);
        SetSkill(-1, 6010, 20, 0);
        SetSkill(-1, 6013, 20, 0);
        SetSkill(-1, 6014, 20, 0);
        SetSkill(-1, 6015, 20, 0);
        SetSkill(-1, 6016, 20, 0);
        SetSkill(-1, 6017, 20, 0);
        SetSkill(-1, 6018, 20, 0);
        SetSkill(-1, 6019, 20, 0);
        SetSkill(-1, 6020, 20, 0);
        SetSkill(-1, 6021, 20, 0);
        SetSkill(-2, 6901, 20, 0);
        SetSkill(-2, 6902, 20, 0);
        SetSkill(-2, 6903, 20, 0);
        SetSkill(-2, 6904, 20, 0);
        SetSkill(-2, 6905, 20, 0);
        SetSkill(-2, 6906, 20, 0);
        SetSkill(-1, 6022, 20, 0);
        SetSkill(-1, 6023, 20, 0);
        SetSkill(-1, 6024, 20, 0);
        SetSkill(-1, 6025, 20, 0);
        SetSkill(-1, 6026, 20, 0);
        SetSkill(-1, 6027, 20, 0);
        SetSkill(-1, 6028, 20, 0);
        SetSkill(-1, 6029, 20, 0);
        SetSkill(-1, 6030, 20, 0);
        SetSkill(-1, 6031, 20, 0);
        SetSkill(-1, 6032, 20, 0);
        SetSkill(-1, 6033, 20, 0);
        SetSkill(-1, 6034, 20, 0);
        SetSkill(-1, 6035, 20, 0);
        SetSkill(-1, 6036, 20, 0);
        SetSkill(-1, 6037, 20, 0);
        SetSkill(-1, 6038, 20, 0);
        SetSkill(-1, 6039, 20, 0);
        SetSkill(-1, 6040, 20, 0);
        SetSkill(-1, 6041, 20, 0);
        SetSkill(-1, 6042, 20, 0);
        SetSkill(-1, 6043, 20, 0);
        SetSkill(-1, 6044, 20, 0);
        SetSkill(-1, 6045, 20, 0);
        SetSkill(-1, 6046, 20, 0);
        SetSkill(-1, 6047, 20, 0);
        SetSkill(-1, 6048, 20, 0);
        SetSkill(-1, 6049, 20, 0);
        SetSkill(-1, 6061, 20, 0);
        SetSkill(-1, 6062, 20, 0);
        SetSkill(-1, 6063, 20, 0);
        SetSkill(-1, 6064, 20, 0);
        SetSkill(-1, 6065, 20, 0);
        SetSkill(-1, 6066, 20, 0);
        SetSkill(-1, 10009, 20, 0);
        SetSkill(-1, 10010, 20, 0);
    }
    return true;
}

bool Player::ReadEquipItem()
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_EQUIP_ITEM);
    stmt->setInt32(0, GetInt32Value(UNIT_FIELD_UID));
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            Field *fields = result->Fetch();
            uint32_t sid = fields[0].GetUInt32();
            int32_t summon_id = fields[1].GetInt32();
            int32_t idx = fields[2].GetInt32();
            int32_t wear_info = idx;

            Unit *unit = nullptr;
            if (summon_id == 0)
                unit = this;
            else
                unit = GetSummon(summon_id);
            Item *item = FindItemBySID(sid);
            std::vector<int32_t> indices{};
            if (item != nullptr && unit != nullptr) {
                if (unit->m_anWear[wear_info] == nullptr) {
                    auto iwt = static_cast<ItemWearType>(wear_info);
                    if (unit->TranslateWearPosition(iwt, item, &indices)) {
                        m_anWear[wear_info] = item;
                        if (iwt == ItemWearType::WEAR_SHIELD && item->GetItemGroup() == ItemGroup::GROUP_WEAPON)
                            Unit::SetFlag(UNIT_FIELD_STATUS, STATUS_USING_DOUBLE_WEAPON);
                        item->GetItemInstance().SetWearInfo((ItemWearType)wear_info);
                        item->m_bIsNeedUpdateToDB = true;
                        if (unit->IsSummon()) {
                            item->GetItemInstance().SetOwnSummonHandle(unit->GetHandle());
                            item->GetItemInstance().SetOwnSummonUID((int32_t)unit->GetInt32Value(UNIT_FIELD_UID));
                        }
                    }
                }
            }

        } while (result->NextRow());
    }
    return true;
}

bool Player::ReadSummonList(int32_t UID)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_SUMMONLIST);
    stmt->setInt32(0, UID);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            Field *fields = result->Fetch();
            int32_t i = 0;
            // PrepareStatement(CHARACTER_GET_SUMMONLIST, "SELECT sid, account_id, code, card_uid, exp, jp,
            // last_decreased_exp, name, transform, lv, jlv, max_level, fp, prev_level_01, prev_level_02,
            // prev_id_01, prev_id_02, sp, hp, mp FROM Summon WHERE owner_id = ?", CONNECTION_SYNCH);
            uint32_t sid = fields[i++].GetUInt32();
            int32_t account_id = fields[i++].GetInt32();
            int32_t code = fields[i++].GetInt32();
            uint32_t card_uid = fields[i++].GetUInt32();
            uint32_t exp = fields[i++].GetUInt32();
            int32_t jp = fields[i++].GetInt32();
            uint32_t last_decreased_exp = fields[i++].GetUInt32();
            std::string name = fields[i++].GetString();
            int32_t transform = fields[i++].GetInt32();
            int32_t lv = fields[i++].GetInt32();
            int32_t jlv = fields[i++].GetInt32();
            int32_t max_level = fields[i++].GetInt32();
            int32_t fp = fields[i++].GetInt32();
            int32_t prev_level_01 = fields[i++].GetInt32();
            int32_t prev_level_02 = fields[i++].GetInt32();
            int32_t prev_id_01 = fields[i++].GetInt32();
            int32_t prev_id_02 = fields[i++].GetInt32();
            int32_t sp = fields[i++].GetInt32();
            int32_t hp = fields[i++].GetInt32();
            int32_t mp = fields[i++].GetInt32();

            auto summon = Summon::AllocSummon(this, code);
            summon->SetUInt32Value(UNIT_FIELD_UID, sid);
            summon->m_nSummonInfo = code;
            summon->m_nCardUID = card_uid;
            summon->SetUInt64Value(UNIT_FIELD_EXP, exp);
            summon->SetJP(jp);
            summon->SetName(name);
            summon->SetLevel(lv);
            summon->SetCurrentJLv(jlv);
            summon->SetInt32Value(UNIT_FIELD_PREV_JOB, prev_id_01);
            summon->SetInt32Value(UNIT_FIELD_PREV_JOB + 1, prev_id_02);
            summon->SetInt32Value(UNIT_FIELD_PREV_JLV, prev_level_01);
            summon->SetInt32Value(UNIT_FIELD_PREV_JLV + 1, prev_level_02);
            summon->SetInt32Value(UNIT_FIELD_HEALTH, hp);
            summon->SetInt32Value(UNIT_FIELD_MANA, mp);
            summon->m_nTransform = transform;
            summon->SetFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE);
            summon->CalculateStat();
            Item *card = FindItemBySID(card_uid);
            if (card == nullptr) {
                NG_LOG_ERROR("entities.player", "Invalid summon: Not itembound, owner still exists! [UID: %d , SummonUID: %d]", card_uid, sid);
                summon->DeleteThis();
            }
            if (card != nullptr) {
                card->m_pSummon = summon;
                card->GetItemInstance().SetSocketIndex(0, sid);
                card->m_bIsNeedUpdateToDB = true;
                card->GetItemInstance().SetOwnSummonHandle(summon->GetHandle());
                summon->m_pItem = card;
                AddSummon(summon, false);
                /*m_player.AddSummon(summon, false);
                readCreatureSkillList(summon);
                readStateInfo(summon);
                summon.SetLoginComplete();
                summon.m_nSP = sp;
                summon.m_nHP = hp;
                summon.m_fMP = mp;*/
                if (!ReadSkillList(summon)) {
                    RemoveSummon(summon);
                    summon->DeleteThis();
                    return false;
                }
            }
        } while (result->NextRow());
    }
    return true;
}

void Player::SendLoginProperties()
{
    Unit::SetFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE);
    CalculateStat();
    CharacterDatabase.DirectPExecute("UPDATE `Character` SET login_time = NOW() WHERE sid = %u", GetUInt32Value(UNIT_FIELD_UID));

    if (GetPartyID() != 0)
        sGroupManager.onLogin(GetPartyID(), this);

    Messages::SendQuestList(this);
    for (auto &summon : m_vSummonList) {
        summon->CalculateStat();
        Messages::SendAddSummonMessage(this, summon);
    }

    Messages::SendItemList(this, false);
    Messages::SendCreatureEquipMessage(this, false);

    Messages::SendSkillList(this, this, -1);
    Messages::SendItemCoolTimeInfo(this);
    // TODO Summon Skill Msg

    SendWearInfo();
    SendGoldChaosMessage();
    Messages::SendPropertyMessage(this, this, "chaos", (int64_t)GetChaos());
    Messages::SendLevelMessage(this, this);
    Messages::SendEXPMessage(this, this);
    SendJobInfo();

    Messages::SendStatInfo(this, this);

    Messages::SendPropertyMessage(this, this, "pk_count", (int64_t)GetUInt32Value(PLAYER_FIELD_PKC));
    Messages::SendPropertyMessage(this, this, "dk_count", (int64_t)GetUInt32Value(PLAYER_FIELD_DKC));
    Messages::SendPropertyMessage(this, this, "immoral", (int64_t)GetUInt32Value(PLAYER_FIELD_IP));
    Messages::SendPropertyMessage(this, this, "channel", (int64_t)0);
    Messages::SendPropertyMessage(this, this, "client_info", m_szClientInfo);
    Messages::SendGameTime(this);
    ChangeLocation(GetPositionX(), GetPositionY(), false, false);

    for (auto &item : m_Inventory.m_vList) {
        if (item->GetItemGroup() == ItemGroup::GROUP_SKILLCARD) {
            if ((uint32_t)item->GetItemInstance().GetSocketIndex(0) == GetUInt32Value(UNIT_FIELD_UID)) {
                BindSkillCard(item);
            }
            else if (item->GetItemInstance().GetSocketIndex(1) != 0) {
                auto summon = GetSummon(item->GetItemInstance().GetOwnSummonUID());
                if (summon != nullptr)
                    summon->BindSkillCard(item);
            }
        }
    }

    if (!_bIsInWorld) {
        sWorld.AddObjectToWorld(this);
    }

    if (m_pMainSummon != nullptr) {
        m_pMainSummon->SetFlag(UNIT_FIELD_STATUS, STATUS_INVINCIBLE);
        m_pMainSummon->SetCurrentXY(GetPositionX(), GetPositionY());
        m_pMainSummon->AddNoise(rand32(), rand32(), 50);
        m_pMainSummon->SetLayer(GetLayer());
        sWorld.AddSummonToWorld(m_pMainSummon);
    }
    for (auto &s : m_vStateList) {
        onUpdateState(s, false);
    }
    Messages::SendPropertyMessage(this, this, "stamina", GetStamina());
    Messages::SendPropertyMessage(this, this, "max_stamina", GetInt32Value(PLAYER_FIELD_MAX_STAMINA));
    Messages::SendPropertyMessage(this, this, "stamina_regen", GetStaminaRegenRate());
    Messages::BroadcastStatusMessage(this);
}

void Player::SendGoldChaosMessage()
{
    TS_SC_GOLD_UPDATE goldPct{};
    goldPct.gold = GetGold();
    goldPct.chaos = GetChaos();
    SendPacket(goldPct);
}

void Player::SendJobInfo()
{
    Messages::SendPropertyMessage(this, this, "job", GetCurrentJob());
    Messages::SendPropertyMessage(this, this, "jlv", GetCurrentJLv());
    for (int32_t i = 0; i < 3; ++i) {
        Messages::SendPropertyMessage(this, this, NGemity::StringFormat("job_{}", i), GetPrevJobId(i));
        Messages::SendPropertyMessage(this, this, NGemity::StringFormat("jlv_{}", i), GetPrevJobLv(i));
    }
}

void Player::SendWearInfo()
{
    TS_SC_WEAR_INFO wearInfoPct{};
    wearInfoPct.handle = GetHandle();
    for(auto i = 0; i < MAX_ITEM_WEAR; i++) {
        int32_t wear_info = (m_anWear[i] != nullptr ? m_anWear[i]->GetItemInstance().GetCode() : 0);
        if (i == 2 && wear_info == 0)
            wear_info = GetInt32Value(UNIT_FIELD_MODEL + 2);
        if (i == 4 && wear_info == 0)
            wear_info = GetInt32Value(UNIT_FIELD_MODEL + 3);
        if (i == 5 && wear_info == 0)
            wear_info = GetInt32Value(UNIT_FIELD_MODEL + 4);
        wearInfoPct.item_code[i] = wear_info;

        wearInfoPct.item_enhance[i] = (m_anWear[i] == nullptr ? 0 : m_anWear[i]->GetItemInstance().GetEnhance());
        wearInfoPct.item_level[i] = (m_anWear[i] == nullptr ? 0 : m_anWear[i]->GetItemInstance().GetLevel());
    }
    SendPacket(wearInfoPct);
}

void Player::Save(bool bOnlyPlayer)
{
    // "UPDATE `Character` SET x = ?, y = ?, z = ?, layer = ?, exp = ?, lv = ?, hp = ?, mp = ?, stamina = ?, jlv = ?, jp = ?, total_jp = ?, job_0 = ?, job_1 = ?, job_2 = ?,
    // jlv_0 = ?, jlv_1 = ?, jlv_2 = ?, permission = ?, job = ?, gold = ?, party_id = ?, guild_id = ? WHERE sid = ?"
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_UPD_CHARACTER);
    uint8_t i = 0;
    stmt->setFloat(i++, GetPositionX());
    stmt->setFloat(i++, GetPositionY());
    stmt->setFloat(i++, GetPositionZ());
    stmt->setInt32(i++, GetLayer());
    stmt->setInt64(i++, GetEXP());
    stmt->setInt32(i++, GetLevel());
    stmt->setInt32(i++, GetHealth());
    stmt->setInt32(i++, GetMana());
    stmt->setInt32(i++, GetStamina());
    stmt->setInt32(i++, GetCurrentJLv());
    stmt->setInt32(i++, GetJP());
    stmt->setInt32(i++, GetTotalJP());
    stmt->setInt32(i++, GetPrevJobId(0));
    stmt->setInt32(i++, GetPrevJobId(1));
    stmt->setInt32(i++, GetPrevJobId(2));
    stmt->setInt32(i++, GetPrevJobLv(0));
    stmt->setInt32(i++, GetPrevJobLv(1));
    stmt->setInt32(i++, GetPrevJobLv(2));
    stmt->setInt32(i++, GetPermission());
    stmt->setInt32(i++, GetCurrentJob());
    stmt->setInt64(i++, GetGold());
    stmt->setInt32(i++, GetPartyID());
    stmt->setInt32(i++, GetGuildID());
    for (auto summon : m_aBindSummonCard)
        stmt->setInt32(i++, summon != nullptr && summon->m_pSummon != nullptr ? summon->m_pSummon->GetInt32Value(UNIT_FIELD_UID) : 0);
    stmt->setInt32(i++, m_pMainSummon != nullptr ? m_pMainSummon->GetInt32Value(UNIT_FIELD_UID) : 0);
    stmt->setInt32(i++, 0); // Sub Summon
    stmt->setInt32(i++, 0); // Pet
    stmt->setInt32(i++, GetInt32Value(PLAYER_FIELD_CHAOS));
    stmt->setString(i++, m_szClientInfo);
    std::string flaglist{};
    for (auto &flag : m_lFlagList)
        flaglist.append(NGemity::StringFormat("{}:{}\n", flag.first, flag.second));
    stmt->setString(i++, flaglist);
    stmt->setInt32(i, GetInt32Value(UNIT_FIELD_UID));
    CharacterDatabase.Execute(stmt);

    if (!bOnlyPlayer) {
        for (auto &item : m_Inventory.m_vList) {
            // if (item->m_bIsNeedUpdateToDB)
            item->DBUpdate();
        }

        // REPLACE query - acts as insert & update
        DB_ItemCoolTime(this);

        for (auto &summon : m_vSummonList) {
            if (summon == nullptr)
                continue;
            Summon::DB_UpdateSummon(this, summon);
        }

        for (auto &q : m_QuestManager.m_vActiveQuest) {
            if (q == nullptr)
                continue;
            Quest::DB_Insert(this, q);
        }
    }
}

void Player::applyJobLevelBonus()
{
    int32_t levels[4]{};
    int32_t jobs[4]{};
    int32_t i = 0;
    CreatureStat stat{};

    if (GetCurrentJob() != 0) {
        int32_t jobDepth = GetJobDepth();
        for (i = 0; i < jobDepth; i++) {
            jobs[i] = GetPrevJobId(i);
            levels[i] = GetPrevJobLv(i);
        }
        // i++;

        jobs[i] = GetCurrentJob();
        levels[i] = GetCurrentJLv();
        stat = sObjectMgr.GetJobLevelBonus(jobDepth, jobs, levels);
        m_cStat = m_cStat + stat;
    }
}

Item *Player::FindItemByCode(int32_t id)
{
    return m_Inventory.FindByCode(id);
}

Item *Player::FindItemBySID(int64_t uid)
{
    return m_Inventory.FindBySID(uid);
}

Item *Player::FindItemByHandle(uint32_t handle)
{
    return m_Inventory.FindByHandle(handle);
}

uint16_t Player::putonItem(ItemWearType pos, Item *item)
{
    if (pos == ItemWearType::WEAR_SHIELD) {
        if (item->GetItemGroup() == ItemGroup::GROUP_WEAPON)
            Unit::SetFlag(UNIT_FIELD_STATUS, STATUS_USING_DOUBLE_WEAPON);
    }
    uint16_t result = Unit::putonItem(pos, item);
    auto nPos = static_cast<int32_t>(pos);
    if (result == 0) {
        if (m_anWear[nPos] != nullptr && m_anWear[nPos]->GetHandle() == item->GetHandle()) {
            m_Inventory.m_fWeightModifier -= item->GetWeight();
        }
        UpdateWeightWithInventory();
        UpdateQuestStatusByItemUpgrade();
        result = 0;
    }
    return result;
}

uint16_t Player::putoffItem(ItemWearType pos)
{
    auto item = m_anWear[(int32_t)pos];
    if (item == nullptr)
        return 1;

    switch (pos) {
    case ItemWearType::WEAR_ARMULET:
        if (GetChaos() != 0)
            return TS_RESULT_ACCESS_DENIED;
        break;
    case ItemWearType::WEAR_SHIELD:
        if (m_anWear[1]->GetItemGroup() == ItemGroup::GROUP_WEAPON)
            RemoveFlag(UNIT_FIELD_STATUS, STATUS_USING_DOUBLE_WEAPON);
        if (m_anWear[15] != nullptr)
            putoffItem(ItemWearType::WEAR_DECO_SHIELD);
        break;
    case ItemWearType::WEAR_WEAPON:
        if (HasFlag(UNIT_FIELD_STATUS, STATUS_USING_DOUBLE_WEAPON))
            putoffItem(ItemWearType::WEAR_SHIELD);
        putoffItem(ItemWearType::WEAR_DECO_SHIELD);
        break;
    case ItemWearType::WEAR_DECO_WEAPON:
        if (HasFlag(UNIT_FIELD_STATUS, STATUS_USING_DOUBLE_WEAPON) && m_anWear[15] != nullptr)
            putoffItem(ItemWearType::WEAR_DECO_SHIELD);
        break;
    default:
        break;
    }
    auto result = Unit::putoffItem(pos);
    if (item != nullptr)
        m_Inventory.m_fWeightModifier += item->GetWeight();
    UpdateWeightWithInventory();
    UpdateQuestStatusByItemUpgrade();
    return result;
}

void Player::SendItemWearInfoMessage(Item *item, Unit *u)
{
    TS_SC_ITEM_WEAR_INFO itemWearInfoPct{};
    itemWearInfoPct.item_handle = item->GetHandle();
    itemWearInfoPct.wear_position = item->GetItemInstance().GetItemWearType();
    itemWearInfoPct.target_handle = (u != nullptr ? u->GetHandle() : 0);
    itemWearInfoPct.enhance = item->GetItemInstance().GetEnhance();
    SendPacket(itemWearInfoPct);
}

Summon *Player::GetSummon(int32_t summon_sid)
{
    for (auto summon : m_vSummonList) {
        if (summon != nullptr)
            if (summon->GetInt32Value(UNIT_FIELD_UID) == summon_sid)
                return summon;
    }
    return nullptr;
}

void Player::SetLastContact(const std::string &szKey, const std::string &szValue)
{
    m_hsContact[szKey] = szValue;
}

void Player::SetLastContact(const std::string &szKey, uint32_t nValue)
{
    SetLastContact(szKey, std::to_string(nValue));
}

std::string Player::GetLastContactStr(const std::string &szKey)
{
    std::string res{};
    if (m_hsContact.count(szKey) == 1)
        return m_hsContact[szKey];
    return res;
}

uint32_t Player::GetLastContactLong(const std::string &szKey)
{
    auto szValue = GetLastContactStr(szKey);
    return (uint32_t)std::stoul(szValue);
}

void Player::SetDialogTitle(const std::string &szTitle, int32_t type)
{
    if (!szTitle.empty()) {
        m_nDialogType = type;

        if (type != 0)
            m_bNonNPCDialog = true;

        m_szDialogTitle = szTitle;
        m_szDialogMenu = "";
        m_szSpecialDialogMenu = "";
    }
}

void Player::SetDialogText(const std::string &szText)
{
    if (!szText.empty()) {
        m_szDialogText = szText;
        m_szDialogMenu = "";
        m_szSpecialDialogMenu = "";
    }
}

void Player::AddDialogMenu(const std::string &szKey, const std::string &szValue)
{
    if (!szKey.empty()) {
        if (szKey.find('\t') == std::string::npos && szValue.find('\t') == std::string::npos) {
            m_szDialogMenu += "\t";
            m_szDialogMenu += szKey;
            m_szDialogMenu += "\t";
            m_szDialogMenu += (szValue.empty() ? "" : szValue);
            m_szDialogMenu += "\t";
        }
    }
}

void Player::ShowDialog()
{
    if (m_szDialogTitle.length() > 0 || m_szDialogText.length() > 0) {
        uint32_t npc = GetLastContactLong("npc");
        Messages::SendDialogMessage(this, npc, m_nDialogType, m_szDialogTitle, m_szDialogText, m_szDialogMenu);
        m_nDialogType = 0;
        m_szDialogTitle = "";
        m_szDialogText = "";
    }
}

bool Player::IsValidTrigger(const std::string &szTrigger)
{
    Tokenizer tokenizer(m_szDialogMenu, '\t');
    for (const auto &s : tokenizer) {
        if (s == szTrigger)
            return true;
    }
    return false;
}

uint16_t Player::ChangeGold(int64_t nGold)
{
    if (nGold != GetGold()) {
        if (nGold > MAX_GOLD_FOR_INVENTORY)
            return TS_RESULT_TOO_MUCH_MONEY;
        if (nGold < 0)
            return TS_RESULT_TOO_CHEAP;
        SetUInt64Value(PLAYER_FIELD_GOLD, (uint64_t)nGold);
        SendGoldChaosMessage();
    }
    return TS_RESULT_SUCCESS;
}

/********************** INVENTORY BEGIN **********************/

void Player::onAdd(Inventory *pInventory, Item *pItem, bool bSkipUpdateItemToDB)
{
    int32_t oldOwner = pItem->GetItemInstance().GetOwnerUID();
    int32_t oldAccount = pItem->GetAccountID();

    if (pInventory == &m_Inventory) {
        pItem->SetOwnerInfo(GetHandle(), GetUInt32Value(UNIT_FIELD_UID), 0);
        if (pItem->IsSummonCard() && pItem->m_pSummon != nullptr) {
            AddSummon(pItem->m_pSummon, true);
            Messages::SendSkillList(this, pItem->m_pSummon, -1);
        }
#if EPIC >= EPIC_5_1
        else if (pItem->GetItemTemplate()->group == PetCage) {
        }
#endif
        if (pItem->GetWearType() == ItemWearType::WEAR_RIDE_ITEM && HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE) && m_anWear[(ItemWearType::WEAR_RIDE_ITEM)] == nullptr)
            putonItem(ItemWearType::WEAR_RIDE_ITEM, pItem);

        if (pItem->IsCharm()) {
            m_vCharmList.emplace_back(pItem);
            if (HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE)) {
                CalculateStat();
                if (IsRiding()) {
                    auto pSummon = GetRideObject();
                    if (pSummon != nullptr)
                        pSummon->CalculateStat();
                }
            }
        }
        // EPIC 5/6: ElementalEffectType()
    }
    else {
        pItem->SetOwnerInfo(GetHandle(), 0, GetAccountID());
        if (pItem->IsSummonCard() && pItem->m_pSummon != nullptr) {
            AddSummonToStorage(pItem->m_pSummon);
        }
#if EPIC >= EPIC_5_1
        else if (pItem->GetItemTemplate()->group == PetCage) {
        }
#endif
    }

    if (pItem->GetItemUID() == 0) {
        sMemoryPool.AllocItemHandle(pItem);
        if (!bSkipUpdateItemToDB)
            pItem->DBInsert();
    }
    else {
        if (!bSkipUpdateItemToDB && (oldOwner != pItem->GetOwnerUID() || oldAccount != pItem->GetAccountID()) && HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE))
            pItem->DBUpdate();
    }

    if (HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE))
        Messages::SendItemMessage(this, pItem);
    UpdateWeightWithInventory();
}

void Player::onRemove(Inventory *pInventory, Item *pItem, bool bSkipUpdateItemToDB)
{
    if (HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE)) {
        pItem->SetOwnerInfo(0, 0, 0);
        if (!bSkipUpdateItemToDB && pItem->GetItemUID() != 0)
            pItem->DBUpdate();

        if (pInventory == &m_Inventory) {
            if (pItem->IsSummonCard() && pItem->m_pSummon != nullptr)
                RemoveSummon(pItem->m_pSummon);

            if (pItem->IsCharm()) {
                auto pos = std::find(m_vCharmList.begin(), m_vCharmList.end(), pItem);
                if (pos != m_vCharmList.end())
                    m_vCharmList.erase(pos);
                if (HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE)) {
                    CalculateStat();
                    if (IsRiding()) {
                        auto pSummon = GetRideObject();
                        if (pSummon != nullptr)
                            pSummon->CalculateStat();
                    }
                }
            }

            if (pItem->IsSkillCard() && pItem->GetBindedCreatureHandle() != 0) {
                auto scu = sMemoryPool.GetObjectInWorld<Unit>(pItem->GetBindedCreatureHandle());
                if (scu != nullptr)
                    scu->UnBindSkillCard(pItem);
            }
        }
        else {
            if (pItem->IsSummonCard() && pItem->m_pSummon != nullptr)
                RemoveSummonFromStorage(pItem->m_pSummon);
        }
        Messages::SendItemDestroyMessage(this, pItem);
    }
    UpdateWeightWithInventory();
}

void Player::onChangeCount(Inventory * /*pInventory*/, Item *pItem, bool bSkipUpdateItemToDB)
{
    if (HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE))
        Messages::SendItemCountMessage(this, pItem);

    if (!bSkipUpdateItemToDB && pItem->IsInStorage())
        pItem->DBUpdate();

    UpdateWeightWithInventory();
}

Item *Player::PushItem(Item *pItem, int64_t count, bool bSkipUpdateToDB)
{
    if ((uint32_t)pItem->GetItemInstance().GetOwnerUID() == GetUInt32Value(UNIT_FIELD_UID)) {
        NG_LOG_ERROR("entities.player", "Player::PushItem(): tried to push already owned Item: %d, %s", pItem->GetItemInstance().GetOwnerUID(), GetName());
        return nullptr;
    }

    // In this case gold
    if (pItem->GetItemInstance().GetCode() == 0) {
        int64_t nPrevGoldAmount = GetGold();
        int64_t gold = GetGold() + pItem->GetItemInstance().GetCount();
        if (ChangeGold(gold) != TS_RESULT_SUCCESS) {
            NG_LOG_ERROR("entities.player", "ChangeGold failed! Player[%s], Curr[%ld], Add [%ld]", GetName(), nPrevGoldAmount, gold);
        }
        Item::PendFreeItem(pItem);
        return nullptr;
    }

    if (pItem->GetItemInstance().GetIndex() == 0) {
        m_Inventory.m_nIndex++;
        pItem->GetItemInstance().SetIdx(m_Inventory.m_nIndex);
        pItem->m_bIsNeedUpdateToDB = true;
    }

    Item *ni = m_Inventory.Push(pItem, count, bSkipUpdateToDB);
    m_QuestManager.UpdateQuestStatusByItemCount(ni->GetItemInstance().GetCode(), ni->GetItemInstance().GetCount());
    return ni;
}

Item *Player::PopItem(Item *pItem, int64_t cnt, bool bSkipUpdateToDB)
{
    if (pItem != nullptr && cnt != 0 && pItem->GetItemInstance().GetCount() >= cnt && pItem->GetItemInstance().GetOwnerHandle() == GetHandle() && pItem->IsInInventory()) {
        Item *nc = popItem(pItem, cnt, bSkipUpdateToDB);
        if (nc != nullptr) {
            nc->GetItemInstance().SetIdx(0);
            nc->m_bIsNeedUpdateToDB = true;
            if (nc->GetHandle() != pItem->GetHandle())
                nc->SetOwnerInfo(0, 0, 0);
            return nc;
        }
    }
    return nullptr;
}

Item *Player::popItem(Item *pItem, int64_t cnt, bool bSkipUpdateToDB)
{
    if (pItem->GetItemInstance().GetCount() >= cnt) {
        m_QuestManager.UpdateQuestStatusByItemCount(pItem->GetItemInstance().GetCode(), pItem->GetItemInstance().GetCount() - cnt);
        return m_Inventory.Pop(pItem, cnt, bSkipUpdateToDB);
    }
    return nullptr;
}

bool Player::EraseItem(Item *pItem, int64_t count)
{
    return m_Inventory.Erase(pItem, count, false);
}

uint32_t Player::GetItemCount() const
{
    return (uint32_t)m_Inventory.m_vList.size();
}

uint32_t Player::GetStorageItemCount() const
{
    return (uint32_t)m_Storage.m_vList.size();
}

Item *Player::GetItem(uint32_t idx)
{
    return m_Inventory.m_vList[idx];
}

Item *Player::GetStorageItem(uint32_t idx)
{
    return m_Storage.m_vList[idx];
}

/********************** INVENTORY END **********************/

void Player::ChangeLocation(float x, float y, bool bByRequest, bool bBroadcast)
{
    Position client_pos{};
    client_pos.Relocate(x, y, 0, 0);
    uint32_t ct = sWorld.GetArTime();
    Position pos = this->GetCurrentPosition(ct);
    if (client_pos.GetExactDist2d(&pos) < 120.0f) {
        pos.m_positionX = client_pos.m_positionX;
        pos.m_positionY = client_pos.m_positionY;
        pos.m_positionZ = client_pos.m_positionZ;
        pos._orientation = client_pos._orientation;
    }
    int32_t nl = GameContent::GetLocationID(x, y);
    TS_SC_CHANGE_LOCATION changeLocationPct{};
    changeLocationPct.prev_location_id = m_nWorldLocationId;
    changeLocationPct.cur_location_id = nl;
    SendPacket(changeLocationPct);

    if (m_nWorldLocationId != nl) {
        if (m_nWorldLocationId != 0) {
            sWorldLocationMgr.RemoveFromLocation(this);
            this->m_WorldLocation = nullptr;
        }
        if (nl != 0) {
            this->m_WorldLocation = sWorldLocationMgr.AddToLocation(nl, this);
        }
        m_nWorldLocationId = nl;
    }
}

void Player::Update(uint32_t diff)
{
    if (!IsInWorld())
        return;

    uint32_t ct = sWorld.GetArTime();

    bool bIsMoving = IsMoving(ct);
    if (HasFlag(UNIT_FIELD_STATUS, STATUS_MOVE_PENDED)) {
        processPendingMove();
    }
    if (!bIsMoving) {
        onAttackAndSkillProcess();
    }

    Unit::Update(diff);
}

void Player::OnUpdate()
{
    uint32_t ct = sWorld.GetArTime();
    if (GetUInt32Value(PLAYER_LAST_STAMINA_UPDATE_TIME) + 6000 < ct) {
        uint32_t lst = (ct - GetUInt32Value(PLAYER_LAST_STAMINA_UPDATE_TIME)) / 0x1770;
        SetUInt32Value(PLAYER_LAST_STAMINA_UPDATE_TIME, GetUInt32Value(PLAYER_LAST_STAMINA_UPDATE_TIME) + 6000 * lst);
        AddStamina((int32_t)(GetStaminaRegenRate() * lst));
    }
    if (GetUInt32Value(UNIT_LAST_SAVE_TIME) + 30000 < ct) {
        this->Save(false);
        Position pos = GetCurrentPosition(ct);
        ChangeLocation(pos.GetPositionX(), pos.GetPositionY(), false, true);
        SetUInt32Value(UNIT_LAST_SAVE_TIME, ct);
    }

    for (auto &summon : m_aBindSummonCard) {
        if (summon != nullptr && summon->m_pSummon != nullptr)
            summon->m_pSummon->OnUpdate();
    }

    Unit::OnUpdate();
}

void Player::onRegisterSkill(int64_t skillUID, int32_t skill_id, int32_t prev_level, int32_t skill_level)
{
    auto sb = sObjectMgr.GetSkillBase((uint32_t)skill_id);
    if (sb == nullptr || sb->IsSystemSkill())
        return;

    Skill::DB_InsertSkill(this, skillUID, skill_id, skill_level, GetRemainCoolTime(skill_id));

    int32_t skill_type = sb->GetSkillEffectType();

    if (skill_id == SKILL_CREATURE_MASTERY || skill_type == EF_INCREASE_SUMMON_HP_MP_SP || skill_type == EF_AMPLIFY_SUMMON_HP_MP_SP) {
        setSummonUpdate();
    }

    m_QuestManager.UpdateQuestStatusBySkillLevel(skill_id, skill_level);
    Messages::SendSkillList(this, this, skill_id);
}

void Player::onExpChange()
{
    int32_t level = 1;
    auto exp = GetEXP();
    if (sObjectMgr.GetNeedExp(1) <= exp) {
        do {
            if (level >= 300)
                break;
            ++level;
        } while (sObjectMgr.GetNeedExp(level) <= exp);
    }
    level -= 1;
    Messages::SendEXPMessage(this, this);
    sendBonusEXPJPMsg();
    int32_t oldLevel = GetLevel();
    if (level != 0 && level != oldLevel) {
        SetLevel((uint8_t)level);
        if (level < oldLevel) {
            this->CalculateStat();
        }
        else {
            sScriptingMgr.RunString(this, "on_player_level_up()");

            /*if(GetLevel() > GetUInt32Value(UNIT_FIELD_MAX_REACHED_LEVEL))
                SetUInt64Value(UNIT_FIELD_MAX_REACHED_LEVEL)*/

            this->CalculateStat();
            if (GetHealth() != 0) {
                SetHealth(GetMaxHealth());
                SetMana(GetMaxMana());
            }
            Messages::BroadcastHPMPMessage(this, 0, 0, false);
            // TODO: Guild & Party level update
        }
        this->Save(false);
        Messages::BroadcastLevelMsg(this);
    }
    else {
        if (GetUInt32Value(UNIT_LAST_SAVE_TIME) + 3000 < sWorld.GetArTime())
            Save(true);
    }
}

void Player::onChangeProperty(std::string key, int32_t value)
{
    if (key == "hp") {
        Messages::BroadcastHPMPMessage(this, value, 0, false);
        return;
    }
    else if (key == "lvl" || key == "lv" || key == "level") {
        SetEXP(sObjectMgr.GetNeedExp(value));
        return;
    }
    else if (key == "exp") {
        onExpChange();
        return;
    }
    else if (key == "gold") {
        SendGoldChaosMessage();
        return;
    }
    else if (key == "job") {
        this->CalculateStat();
        // return;
    }
    else if (key == "jlvl" || key == "jlv" || key == "job_level") {
        Messages::SendPropertyMessage(this, this, "job_level", GetCurrentJLv());
        return;
    }
    Messages::SendPropertyMessage(this, this, key, value);
}

void Player::AddSummon(Summon *pSummon, bool bSendMsg)
{
    pSummon->m_pMaster = this;
    m_vSummonList.emplace_back(pSummon);
    if (bSendMsg)
        Messages::SendAddSummonMessage(this, pSummon);
    if (pSummon->HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE)) {
        Summon::DB_UpdateSummon(this, pSummon);
    }
}

bool Player::RemoveSummon(Summon *pSummon)
{
    auto pos = std::find(m_vSummonList.begin(), m_vSummonList.end(), pSummon);
    if (pos != m_vSummonList.end())
        m_vSummonList.erase(pos);

    pSummon->m_pMaster = nullptr;
    Messages::SendRemoveSummonMessage(this, pSummon);
    Summon::DB_UpdateSummon(this, pSummon);
    return true;
}

Summon *Player::GetSummonByHandle(uint32_t handle)
{
    for (auto s : m_vSummonList) {
        if (s == nullptr)
            continue;
        if (s->GetHandle() == handle)
            return s;
    }
    return nullptr;
}

void Player::PendWarp(int32_t x, int32_t y, uint8_t layer)
{
    Unit::SetFlag(UNIT_FIELD_STATUS, STATUS_INVINCIBLE);

    if (m_pMainSummon != nullptr)
        m_pMainSummon->SetFlag(UNIT_FIELD_STATUS, STATUS_INVINCIBLE);
    // PendWarp end, ProcessWarp start
    if (x >= 0.0f && y >= 0.0f /*MapWidth check*/) {
        if (IsInWorld()) {
            Position pos{};
            pos.Relocate(x, y, 0);

            sWorld.WarpBegin(this);
            sWorld.WarpEnd(this, pos, layer);
            ClearDialogMenu();
        }
    }
}

void Player::ClearDialogMenu()
{
    m_szDialogMenu = "";
}

void Player::LogoutNow(int32_t /*callerIdx*/)
{
    if (IsInWorld()) {
        // RemoveAllSummonFromWorld();
        // sWorld.RemoveObjectFromWorld(this);
    }
    Save(false);
}

void Player::RemoveAllSummonFromWorld()
{
    for (auto &s : m_vSummonList) {
        if (s == nullptr)
            continue;
        if (s->IsInWorld()) {
            sWorld.RemoveObjectFromWorld(s);
        }
    }
}

void Player::DoSummon(Summon *pSummon, Position pPosition)
{
    /*            if (!this.m_bIsSummonable)
                return false;*/
    if (pSummon->IsInWorld() /*|| m_pMainSummon != nullptr*/)
        return;

    DoUnSummon(m_pMainSummon);

    // TODO Do Subsummon here
    m_pMainSummon = pSummon;
    pSummon->SetCurrentXY(pPosition.GetPositionX(), pPosition.GetPositionY());
    pSummon->m_nLayer = this->GetLayer();
    pSummon->StopMove();
    if (pSummon->GetHealth() == 0)
        pSummon->SetUInt32Value(UNIT_FIELD_DEAD_TIME, sWorld.GetArTime());
    sWorld.AddSummonToWorld(pSummon);
    pSummon->SetFlag(UNIT_FIELD_STATUS, STATUS_NEED_TO_CALCULATE_STAT);
}

void Player::DoUnSummon(Summon *pSummon)
{
    if (pSummon == nullptr)
        return;

    if (!pSummon->IsInWorld())
        return;

    m_pMainSummon = nullptr;

    TS_SC_UNSUMMON usPct{};
    usPct.summon_handle = pSummon->GetHandle();
    sWorld.Broadcast((uint32_t)(pSummon->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(pSummon->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)),
        pSummon->GetLayer(), usPct);
    if (sRegion.IsVisibleRegion((uint32_t)(pSummon->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(pSummon->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)),
            (uint32_t)(GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE))) == 0) {
        SendPacket(usPct);
    }
    sWorld.RemoveObjectFromWorld(pSummon);
}

void Player::onCantAttack(uint32_t target, uint32_t t)
{
    if (!bIsMoving || !IsInWorld()) {
        if (GetUInt32Value(UNIT_LAST_CANT_ATTACK_TIME) + 100 < t) {
            SetUInt32Value(UNIT_LAST_SAVE_TIME, t);
            Messages::SendCantAttackMessage(this, this->GetHandle(), target, TS_RESULT_TOO_FAR);
        }
    }
}

bool Player::TranslateWearPosition(ItemWearType &pos, Item *pItem, std::vector<int32_t> *vpOverlappItemList)
{
    auto nPos = static_cast<int32_t>(pos);
    if (nPos >= MAX_ITEM_WEAR && nPos < MAX_SPARE_ITEM_WEAR)
        return true;

    if (!Unit::TranslateWearPosition(pos, pItem, vpOverlappItemList))
        return false;

    if ((pItem->GetItemInstance().GetFlag() & 1) != 0)
        return false;

    bool bWearable = false;

    if (pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_HUNTER) && IsHunter())
        bWearable = true;
    if (pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_FIGHTER) && IsFighter())
        bWearable = true;
    if (pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_MAGICIAN) && IsMagician())
        bWearable = true;
    if (pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_SUMMONER) && IsSummoner())
        bWearable = true;

    if (!bWearable)
        return false;

    if (GetRace() == static_cast<int32_t>(JOB_RACE::GAIA))
        if (!(pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_GAIA)))
            return false;
    if (GetRace() == static_cast<int32_t>(JOB_RACE::DEVA))
        if (!(pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_DEVA)))
            return false;
    if (GetRace() == static_cast<int32_t>(JOB_RACE::ASURA))
        if (!(pItem->GetItemBase()->nLimit & static_cast<int32_t>(LIMIT_FLAG::LIMIT_ASURA)))
            return false;

    if (pos == ItemWearType::WEAR_LEFTHAND && pItem->IsBullet()) {
        if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)] == nullptr)
            return false;

        if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() != ItemClass::CLASS_CROSSBOW && m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() != ItemClass::CLASS_LIGHT_BOW &&
            m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() != ItemClass::CLASS_HEAVY_BOW) {
            return false;
        }
    }

    if (pos == ItemWearType::WEAR_LEFTHAND && pItem->IsWeapon()) {
        if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)] == nullptr)
            return false;

        if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() == ItemClass::CLASS_ONEHAND_SWORD) {
            if (pItem->GetItemClass() == ItemClass::CLASS_ONEHAND_SWORD) {
                int32_t expert_level = GetCurrentSkillLevel(SKILL_DUAL_SWORD_EXPERT);

                if (expert_level < 1)
                    expert_level = GetCurrentSkillLevel(SKILL_TWIN_BLADE_EXPERT);

                if (expert_level < 1)
                    pos = ItemWearType::WEAR_WEAPON;
            }
            else {
                pos = ItemWearType::WEAR_WEAPON;
            }
        }
        else if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() == ItemClass::CLASS_DAGGER) {
            if (pItem->GetItemClass() == ItemClass::CLASS_DAGGER) {
                int32_t expert_level = GetCurrentSkillLevel(SKILL_TWIN_BLADE_EXPERT);

                if (expert_level < 1)
                    pos = ItemWearType::WEAR_WEAPON;
            }
            else {
                pos = ItemWearType::WEAR_WEAPON;
            }
        }
        else if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() == ItemClass::CLASS_ONEHAND_AXE) {
            if (pItem->GetItemClass() == ItemClass::CLASS_ONEHAND_AXE) {
                int32_t expert_level = GetCurrentSkillLevel(SKILL_TWIN_AXE_EXPERT);

                if (expert_level < 1)
                    pos = ItemWearType::WEAR_WEAPON;
            }
            else {
                pos = ItemWearType::WEAR_WEAPON;
            }
        }
        else {
            pos = ItemWearType::WEAR_WEAPON;
        }
    }

    if (pos == ItemWearType::WEAR_DECO_SHIELD) {
        if (pItem->GetItemClass() == ItemClass::CLASS_DECO_ONEHAND_SWORD || pItem->GetItemClass() == ItemClass::CLASS_DECO_TWOHAND_SWORD || pItem->GetItemClass() == ItemClass::CLASS_DECO_DAGGER ||
            pItem->GetItemClass() == ItemClass::CLASS_DECO_TWOHAND_SPEAR || pItem->GetItemClass() == ItemClass::CLASS_DECO_TWOHAND_AXE || pItem->GetItemClass() == ItemClass::CLASS_DECO_ONEHAND_MACE ||
            pItem->GetItemClass() == ItemClass::CLASS_DECO_TWOHAND_MACE || pItem->GetItemClass() == ItemClass::CLASS_DECO_HEAVY_BOW || pItem->GetItemClass() == ItemClass::CLASS_DECO_LIGHT_BOW ||
            pItem->GetItemClass() == ItemClass::CLASS_DECO_CROSSBOW || pItem->GetItemClass() == ItemClass::CLASS_DECO_ONEHAND_STAFF || pItem->GetItemClass() == ItemClass::CLASS_DECO_TWOHAND_STAFF ||
            pItem->GetItemClass() == ItemClass::CLASS_DECO_ONEHAND_AXE) {
            if (m_anWear[(ItemWearType::WEAR_LEFTHAND)] != nullptr && m_anWear[(ItemWearType::WEAR_LEFTHAND)]->GetItemClass() != ItemClass::CLASS_ONEHAND_SWORD &&
                m_anWear[(ItemWearType::WEAR_LEFTHAND)]->GetItemClass() != ItemClass::CLASS_DAGGER && m_anWear[(ItemWearType::WEAR_LEFTHAND)]->GetItemClass() != ItemClass::CLASS_ONEHAND_AXE) {
                pos = ItemWearType::WEAR_DECO_WEAPON;
            }
            else if (m_anWear[(ItemWearType::WEAR_RIGHTHAND)] == nullptr ||
                (m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() != ItemClass::CLASS_ONEHAND_SWORD && m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() != ItemClass::CLASS_DAGGER &&
                    m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->GetItemClass() != ItemClass::CLASS_ONEHAND_AXE)) {
                pos = ItemWearType::WEAR_DECO_WEAPON;
            }
            else if (m_anWear[(ItemWearType::WEAR_DECO_WEAPON)] == nullptr) {
                pos = ItemWearType::WEAR_DECO_WEAPON;
            }
            else {
                int32_t expert_level = GetCurrentSkillLevel(SKILL_DUAL_SWORD_EXPERT);

                if (expert_level < 1)
                    expert_level = GetCurrentSkillLevel(SKILL_TWIN_BLADE_EXPERT);
                if (expert_level < 1)
                    expert_level = GetCurrentSkillLevel(SKILL_TWIN_AXE_EXPERT);

                if (expert_level < 1)
                    pos = ItemWearType::WEAR_DECO_WEAPON;
            }
        }
        else {
            if ((m_anWear[(ItemWearType::WEAR_LEFTHAND)] != nullptr && m_anWear[(ItemWearType::WEAR_LEFTHAND)]->GetItemClass() != ItemClass::CLASS_SHIELD) ||
                (m_anWear[(ItemWearType::WEAR_RIGHTHAND)] != nullptr && m_anWear[(ItemWearType::WEAR_RIGHTHAND)]->IsTwoHandItem())) {
                return false;
            }
        }
    }

    if (pItem->GetWearType() == ItemWearType::WEAR_TWOHAND)
        pos = ItemWearType::WEAR_WEAPON;

    if (pos == ItemWearType::WEAR_TWOFINGER_RING)
        pos = ItemWearType::WEAR_RING;
    if (m_anWear[(ItemWearType::WEAR_RING)] != nullptr && m_anWear[(ItemWearType::WEAR_RING)]->GetWearType() != ItemWearType::WEAR_TWOFINGER_RING && pItem->GetWearType() == ItemWearType::WEAR_RING)
        pos = ItemWearType::WEAR_SECOND_RING;

    if (pos == ItemWearType::WEAR_BAG_SLOT && m_anWear[(ItemWearType::WEAR_BAG_SLOT)] != nullptr) {
        if (m_anWear[(ItemWearType::WEAR_BAG_SLOT)]->GetItemCode() != pItem->GetItemCode()) {
            auto current_bag_base = m_anWear[(ItemWearType::WEAR_BAG_SLOT)]->GetItemBase();
            int32_t current_bag_capacity{0};
            auto new_bag_base = pItem->GetItemBase();
            int32_t new_bag_capacity{0};

            for (int32_t i = 0; i < MAX_OPTION_NUMBER; ++i) {
                if (current_bag_base->opt_type[i] == static_cast<int16_t>(ITEM_EFFECT_PASSIVE::CARRY_WEIGHT))
                    current_bag_capacity += current_bag_base->opt_var[i][0];
                if (new_bag_base->opt_type[i] == static_cast<int16_t>(ITEM_EFFECT_PASSIVE::CARRY_WEIGHT))
                    new_bag_capacity += new_bag_base->opt_var[i][0];
            }

            if (current_bag_capacity != new_bag_capacity) {
                if (((GetMaxWeight() >= GetWeight()) && (GetMaxWeight() - current_bag_capacity + new_bag_capacity < GetWeight())) ||
                    ((GetMaxWeight() < GetWeight()) && current_bag_capacity > new_bag_capacity)) {
                    return false;
                }
            }
        }
    }

    if (pos == ItemWearType::WEAR_DECO_WEAPON || pos == ItemWearType::WEAR_DECO_SHIELD) {
        ItemWearType nRelativePos = (pos == ItemWearType::WEAR_DECO_WEAPON) ? ItemWearType::WEAR_WEAPON : ItemWearType::WEAR_LEFTHAND;

        if (!m_anWear[(nRelativePos)])
            return false;

        switch (m_anWear[(nRelativePos)]->GetItemClass()) {
        case ItemClass::CLASS_ONEHAND_SWORD:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_ONEHAND_SWORD)
                return false;
            break;
        case ItemClass::CLASS_TWOHAND_SWORD:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_TWOHAND_SWORD)
                return false;
            break;
        case ItemClass::CLASS_DAGGER:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_DAGGER)
                return false;
            break;
        case ItemClass::CLASS_TWOHAND_SPEAR:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_TWOHAND_SPEAR)
                return false;
            break;
        case ItemClass::CLASS_TWOHAND_AXE:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_TWOHAND_AXE)
                return false;
            break;
        case ItemClass::CLASS_ONEHAND_MACE:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_ONEHAND_MACE)
                return false;
            break;
        case ItemClass::CLASS_TWOHAND_MACE:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_TWOHAND_MACE)
                return false;
            break;
        case ItemClass::CLASS_HEAVY_BOW:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_HEAVY_BOW)
                return false;
            break;
        case ItemClass::CLASS_LIGHT_BOW:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_LIGHT_BOW)
                return false;
            break;
        case ItemClass::CLASS_CROSSBOW:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_CROSSBOW)
                return false;
            break;
        case ItemClass::CLASS_ONEHAND_STAFF:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_ONEHAND_STAFF)
                return false;
            break;
        case ItemClass::CLASS_TWOHAND_STAFF:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_TWOHAND_STAFF)
                return false;
            break;
        case ItemClass::CLASS_ONEHAND_AXE:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_ONEHAND_AXE)
                return false;
            break;
        case ItemClass::CLASS_SHIELD:
            if (pItem->GetItemClass() != ItemClass::CLASS_DECO_SHIELD)
                return false;
            break;
        default:
            return false;
        }
    }

    if ((pos) >= MAX_ITEM_WEAR || (pos) < 0)
        return false;

    if (vpOverlappItemList != nullptr) {
        if (m_anWear[(pos)] != nullptr)
            vpOverlappItemList->emplace_back(pos);

        if (pItem->IsTwoHandItem() && m_anWear[(ItemWearType::WEAR_LEFTHAND)] != nullptr) {
            if ((!pItem->IsBow() && !pItem->IsCrossBow()) || !m_anWear[(ItemWearType::WEAR_LEFTHAND)]->IsBullet())
                vpOverlappItemList->emplace_back(ItemWearType::WEAR_LEFTHAND);

            if (m_anWear[(ItemWearType::WEAR_DECO_SHIELD)] != nullptr)
                vpOverlappItemList->emplace_back(ItemWearType::WEAR_DECO_SHIELD);
        }

        if (pos == ItemWearType::WEAR_SHIELD) {
            if (pItem->GetItemClass() == ItemClass::CLASS_SHIELD && m_anWear[(ItemWearType::WEAR_DECO_SHIELD)] != nullptr &&
                m_anWear[(ItemWearType::WEAR_DECO_SHIELD)]->GetItemClass() != ItemClass::CLASS_DECO_SHIELD)
                vpOverlappItemList->emplace_back(ItemWearType::WEAR_DECO_SHIELD);

            if (m_anWear[(ItemWearType::WEAR_DECO_SHIELD)] != nullptr && m_anWear[(ItemWearType::WEAR_DECO_SHIELD)]->GetItemClass() == ItemClass::CLASS_DECO_SHIELD)
                vpOverlappItemList->emplace_back(ItemWearType::WEAR_DECO_SHIELD);
        }

        if (m_anWear[(ItemWearType::WEAR_WEAPON)] != nullptr && m_anWear[(ItemWearType::WEAR_WEAPON)]->IsTwoHandItem() && pos == ItemWearType::WEAR_LEFTHAND) {
            if ((!m_anWear[(ItemWearType::WEAR_WEAPON)]->IsBow() && !m_anWear[(ItemWearType::WEAR_WEAPON)]->IsCrossBow()) || !pItem->IsBullet())
                vpOverlappItemList->emplace_back(ItemWearType::WEAR_RIGHTHAND);
        }

        if (m_anWear[(ItemWearType::WEAR_SECOND_RING)] != nullptr && pItem->GetWearType() == ItemWearType::WEAR_TWOFINGER_RING) {
            vpOverlappItemList->emplace_back(ItemWearType::WEAR_SECOND_RING);
        }

        if (m_anWear[(ItemWearType::WEAR_RING)] != nullptr && m_anWear[(ItemWearType::WEAR_RING)]->GetWearType() == ItemWearType::WEAR_TWOFINGER_RING &&
            pItem->GetWearType() == ItemWearType::WEAR_RING) {
            vpOverlappItemList->emplace_back(ItemWearType::WEAR_RING);
        }
    }
    return true;
}

bool Player::IsHunter()
{
    int32_t job_id = GetCurrentJob();
    if (job_id == 0) {
        switch (GetRace()) {
        case 3:
            job_id = 100;
            break;
        case 4:
            job_id = 200;
            break;
        case 5:
            job_id = 300;
            break;
        default:
            break;
        }
    }
    auto info = sObjectMgr.GetJobInfo((uint32_t)job_id);

    if (info != nullptr)
        return info->job_class == 2;
    return false;
}

bool Player::IsFighter()
{
    int32_t job_id = GetCurrentJob();
    if (job_id == 0) {
        switch (GetRace()) {
        case 3:
            job_id = 100;
            break;
        case 4:
            job_id = 200;
            break;
        case 5:
            job_id = 300;
            break;
        default:
            break;
        }
    }
    auto info = sObjectMgr.GetJobInfo((uint32_t)job_id);

    if (info != nullptr)
        return info->job_class == 1;
    return false;
}

bool Player::IsMagician()
{
    int32_t job_id = GetCurrentJob();
    if (job_id == 0) {
        switch (GetRace()) {
        case 3:
            job_id = 100;
            break;
        case 4:
            job_id = 200;
            break;
        case 5:
            job_id = 300;
            break;
        default:
            break;
        }
    }
    auto info = sObjectMgr.GetJobInfo((uint32_t)job_id);

    if (info != nullptr)
        return info->job_class == 3;
    return false;
}

bool Player::IsSummoner()
{
    int32_t job_id = GetCurrentJob();
    if (job_id == 0) {
        switch (GetRace()) {
        case 3:
            job_id = 100;
            break;
        case 4:
            job_id = 200;
            break;
        case 5:
            job_id = 300;
            break;
        default:
            break;
        }
    }
    auto info = sObjectMgr.GetJobInfo((uint32_t)job_id);

    if (info != nullptr)
        return info->job_class == 4;
    return false;
}

bool Player::IsInProgressQuest(int32_t code)
{
    bool result{false};

    auto q = m_QuestManager.FindQuest(code);
    if (q != nullptr)
        result = !q->IsFinishable();
    else
        result = false;
    return result;
}

bool Player::IsStartableQuest(int32_t code, bool bForQuestMark)
{
    auto qbs = sObjectMgr.GetQuestBase(code);
    if (qbs == nullptr)
        return false;

    if ((qbs->nLimitLevel - (int32_t)GetLevel() > 4 || qbs->nLimitJobLevel > GetCurrentJLv()) || (bForQuestMark && qbs->nLimitIndication != 0 && (int32_t)GetLevel() - qbs->nLimitLevel > 12))
        return false;

    if (qbs->nLimitJob != 0) {
        if (qbs->nLimitJob != GetCurrentJob())
            return false;
    }
    else {
        if ((!IsHunter() || (qbs->LimitFlag & 0x20) == 0) && (!IsFighter() || (qbs->LimitFlag & 0x10) == 0) && (!IsMagician() || (qbs->LimitFlag & 0x40) == 0) &&
            (!IsSummoner() || (qbs->LimitFlag & 0x80) == 0))
            return false;
    }
    if ((GetRace() != 3 || (qbs->LimitFlag & 8) == 0) && (GetRace() != 4 || (qbs->LimitFlag & 2) == 0) && (GetRace() != 5 || (qbs->LimitFlag & 4) == 0))
        return false;
    int32_t fgid = qbs->nLimitFavorGroupID;
    if (fgid == 999) {
        auto npc = sMemoryPool.GetObjectInWorld<NPC>(GetLastContactLong("npc"));

        if (npc != nullptr)
            fgid = npc->m_pBase->id;
        else
            fgid = 0;
    }
    // TODO: Favor
    return m_QuestManager.IsStartableQuest(code);
}

bool Player::IsFinishableQuest(int32_t code)
{
    return CheckFinishableQuestAndGetQuestStruct(code);
}

bool Player::CheckFinishableQuestAndGetQuestStruct(int32_t code)
{
    auto q1 = m_QuestManager.FindQuest(code);
    return q1 != nullptr && q1->IsFinishable();
}

void Player::onStatusChanged(Quest *quest, int32_t nOldStatus, int32_t nNewStatus)
{
    if (quest->IsFinishable()) {
        if (quest->m_QuestBase->nEndType == 2) {
            // EndQuest(quest->m_QuestBase->nCode, 0, false);
        }

        Messages::SendNPCStatusInVisibleRange(this);
    }
    Messages::SendQuestStatus(this, quest);
}

void Player::onProgressChanged(Quest *quest, QuestProgress oldProgress, QuestProgress newProgress) {}

Quest *Player::FindQuest(int32_t code)
{
    return m_QuestManager.FindQuest(code);
}

int32_t Player::GetMoveSpeed()
{
    float fWT = GetFloatValue(PLAYER_FIELD_WEIGHT) / m_Attribute.nMaxWeight;
    if (fWT >= 1.0f || fWT < 0.0f) {
        return (int32_t)((float)Unit::GetMoveSpeed() * 0.1f);
    }
    else {
        if (fWT < 0.75f)
            return Unit::GetMoveSpeed();
        return (int32_t)((float)Unit::GetMoveSpeed() * 0.5f);
    }
}

void Player::onModifyStatAndAttribute()
{
    Messages::SendStatInfo(this, this);
    Messages::SendPropertyMessage(this, this, "max_chaos", GetMaxChaos());
}

uint16_t Player::IsUseableItem(Item *pItem, Unit *pTarget)
{
    uint32_t ct = sWorld.GetArTime();
    if (pItem->GetItemTemplate()->cool_time_group < 0 || pItem->GetItemTemplate()->cool_time_group > 40 ||
        (pItem->GetItemTemplate()->cool_time_group != 0 && m_nItemCooltime[pItem->GetItemTemplate()->cool_time_group - 1] > ct))
        return TS_RESULT_COOL_TIME;
    // Ride IDX
    if (pItem->GetItemTemplate()->use_max_level != 0 && pItem->GetItemTemplate()->use_max_level < GetLevel())
        return TS_RESULT_LIMIT_MAX;
    if (pItem->GetItemTemplate()->use_min_level <= GetLevel()) {
        if (pTarget == nullptr)
            return TS_RESULT_SUCCESS;

        if (pItem->GetItemTemplate()->target_max_level != 0 && pItem->GetItemTemplate()->target_max_level < pTarget->GetLevel())
            return TS_RESULT_LIMIT_MAX;
        if (pItem->GetItemTemplate()->target_min_level <= pTarget->GetLevel())
            return TS_RESULT_SUCCESS;
    }
    return TS_RESULT_LIMIT_MIN;
}

uint16_t Player::UseItem(Item *pItem, Unit *pTarget, const std::string &szParameter)
{
    if (pTarget == nullptr)
        pTarget = this;

    if (pItem->GetItemInstance().GetCount() < 1)
        return TS_RESULT_ACCESS_DENIED;

    uint16_t result{TS_RESULT_SUCCESS};

    for (int32_t i = 0; i < MAX_OPTION_NUMBER; ++i) {
        if (pItem->GetItemTemplate()->base_type[i] != 0) {
            result = pTarget->onItemUseEffect(this, pItem, pItem->GetItemTemplate()->base_type[i], pItem->GetItemTemplate()->base_var[i][0], pItem->GetItemTemplate()->base_var[i][1], szParameter);
            if (result != TS_RESULT_SUCCESS)
                return result;
        }

        if (pItem->GetItemTemplate()->opt_type[i] != 0) {
            result = pTarget->onItemUseEffect(this, pItem, pItem->GetItemTemplate()->opt_type[i], pItem->GetItemTemplate()->opt_var[i][0], pItem->GetItemTemplate()->opt_var[i][1], szParameter);
            if (result != TS_RESULT_SUCCESS)
                return result;
        }
    }

    if (!pItem->GetItemTemplate()->script_text.empty() && pItem->GetItemTemplate()->script_text != "0") {
        std::string szOnUseItem = pItem->GetItemTemplate()->script_text;

        if (szOnUseItem.find("on_use_item") != std::string::npos) {
            auto targetType = Item::TARGET_TYPE_UNKNOWN;
            int32_t nCode{0};
            if (pTarget->IsPlayer()) {
                targetType = Item::TARGET_TYPE_PLAYER;
            }
            else if (pTarget->IsSummon()) {
                targetType = Item::TARGET_TYPE_SUMMON;
                nCode = pTarget->As<Summon>()->GetSummonCode();
            }
            else if (pTarget->IsMonster()) {
                targetType = Item::TARGET_TYPE_MONSTER;
                nCode = pTarget->As<Monster>()->GetMonsterID();
            }
            else if (pTarget->IsNPC()) {
                targetType = Item::TARGET_TYPE_NPC;
                nCode = pTarget->As<NPC>()->GetNPCID();
            }

            szOnUseItem = NGemity::StringFormat("on_use_item({}, {}, {}, {}, {}, {})", pItem->GetItemInstance().GetCode(), GetHandle(), static_cast<int32_t>(targetType),
                (pTarget != nullptr ? pTarget->GetHandle() : 0), nCode, (targetType == Item::TARGET_TYPE_SUMMON && pTarget->As<Summon>()->GetMaster() == this) ? 1 : 0);
        }

        auto nNPCHandle = GetLastContactLong("npc");
        SetLastContact("npc", 0);
        sScriptingMgr.RunString(szOnUseItem);
        SetLastContact("npc", nNPCHandle);
    }

    if (pItem->GetItemTemplate()->cool_time_group != 0) {
        m_nItemCooltime[pItem->GetItemTemplate()->cool_time_group - 1] = sWorld.GetArTime() + (pItem->GetItemTemplate()->cool_time * 100);
        Messages::SendItemCoolTimeInfo(this);
    }

    switch (pItem->GetItemInstance().GetCode()) {
    case ITEM_CODE_FEATHER_OF_RETURN:
    case ITEM_CODE_FEATHER_OF_REINSTATEMENT:
    case ITEM_CODE_FEATHER_OF_RETURN_EVENT:
        return TS_RESULT_SUCCESS;
    default:
        break;
    }

    if (pItem->GetItemTemplate()->eType != ItemType::TYPE_USE)
        EraseItem(pItem, 1);

    if (pItem->IsCashItem())
        Save(false);

    return result;
}

CreatureStat *Player::GetBaseStat() const
{
    uint32_t stat_id = 0;
    auto job = sObjectMgr.GetJobInfo(GetCurrentJob());
    if (job != nullptr)
        stat_id = job->stat_id;
    return sObjectMgr.GetStatInfo(stat_id);
}

Item *Player::FindItem(uint32_t code, uint32_t flag, bool bFlag)
{
    return m_Inventory.Find(code, flag, bFlag);
}

void Player::DoEachPlayer(const std::function<void(Player *)> &fn)
{
    NG_SHARED_GUARD readGuard(*HashMapHolder<Player>::GetLock());
    auto const &m = sMemoryPool.GetPlayers();
    for (auto &itr : m) {
        if (itr.second != nullptr)
            fn(itr.second);
    }
}

Player *Player::FindPlayer(const std::string &szName)
{
    NG_SHARED_GUARD readGuard(*HashMapHolder<Player>::GetLock());
    auto const &m = sMemoryPool.GetPlayers();

    auto pos = std::find_if(m.begin(), m.end(), [&szName](const auto &player) { return iequals(szName, player.second->GetNameAsString()); });

    return pos == m.end() ? nullptr : pos->second;
}

void Player::StartQuest(int32_t code, int32_t nStartQuestID, bool bForce)
{
    auto rQuestBase = sObjectMgr.GetQuestBase(code);
    if (rQuestBase == nullptr)
        return;

    if (m_QuestManager.m_vActiveQuest.size() >= 20) {
        auto str = NGemity::StringFormat("START|FAIL|QUEST_NUMBER_EXCEED|{}", rQuestBase->nQuestTextID);
        Messages::SendQuestMessage(120, this, str);
        return;
    }

    if (!bForce && !IsStartableQuest(code, false)) {
        auto str = NGemity::StringFormat("START|FAIL|NOT_STARTABLE|{}", rQuestBase->nQuestTextID);
        Messages::SendQuestMessage(120, this, str);
        return;
    }

    // Random Quest not implemented yet
    // bool bHasRandomQuest{false};
    // if (Quest::IsRandomQuest(code) && m_QuestManager.HasRandomQuestInfo(code))
    //     bHasRandomQuest = true;
    if (m_QuestManager.StartQuest(code, nStartQuestID)) {
        auto q = m_QuestManager.FindQuest(code);
        if (q == nullptr) {
            return;
        }

        onStartQuest(q);
        Quest::DB_Insert(this, q);

        if (!Quest::IsRandomQuest(q->m_Instance.Code)) {
            auto str = NGemity::StringFormat("START|SUCCESS|{}", rQuestBase->nCode);
            Messages::SendQuestMessage(120, this, str);
            Messages::SendQuestList(this);
            if (!q->m_QuestBase->strAcceptScript.empty()) {
                sScriptingMgr.RunString(this, q->m_QuestBase->strAcceptScript);
            }
            return;
        }
    }

    auto str = NGemity::StringFormat("START|FAIL|NOT_STARTABLE|{}", rQuestBase->nQuestTextID);
    Messages::SendQuestMessage(120, this, str);
}

void Player::onStartQuest(Quest *pQuest)
{
    updateQuestStatus(pQuest);
    Messages::SendNPCStatusInVisibleRange(this);
}

void Player::onEndQuest(Quest *pQuest)
{
    m_QuestManager.PopFromActiveQuest(pQuest);
    Messages::SendNPCStatusInVisibleRange(this);
}

void Player::updateQuestStatus(Quest *pQuest)
{
    int32_t nMaxItemCollectTypeCount = 0;
    int32_t nItemCode = 0;

    QuestType qt = pQuest->m_QuestBase->nType;
    if (qt == QuestType::QUEST_COLLECT || qt == QuestType::QUEST_HUNT_ITEM || qt == QuestType::QUEST_HUNT_ITEM_FROM_ANY_MONSTERS) {
        switch (qt) {
        case QuestType::QUEST_COLLECT:
            nMaxItemCollectTypeCount = 2;
            break;
        case QuestType::QUEST_HUNT_ITEM_FROM_ANY_MONSTERS:
        case QuestType::QUEST_HUNT_ITEM:
            nMaxItemCollectTypeCount = 3;
            break;
        default:
            break;
        }

        for (int32_t i = 0; i < nMaxItemCollectTypeCount; ++i) {
            nItemCode = pQuest->GetValue(2 * i);
            if (nItemCode != 0) {
                auto item = FindItemByCode(nItemCode);
                if (item != nullptr) {
                    m_QuestManager.UpdateQuestStatusByItemCount(nItemCode, item->GetItemInstance().GetCount());
                }
            }
        }
    }
    if (qt == QuestType::QUEST_LEARN_SKILL) {
        m_QuestManager.UpdateQuestStatusBySkillLevel(pQuest->GetValue(0), GetBaseSkillLevel(pQuest->GetValue(0)));
        m_QuestManager.UpdateQuestStatusBySkillLevel(pQuest->GetValue(2), GetBaseSkillLevel(pQuest->GetValue(2)));
        m_QuestManager.UpdateQuestStatusBySkillLevel(pQuest->GetValue(4), GetBaseSkillLevel(pQuest->GetValue(4)));
    }
    if (qt == QuestType::QUEST_JOB_LEVEL) {
        m_QuestManager.UpdateQuestStatusByJobLevel(GetJobDepth(), GetCurrentJLv());
    }
    if (qt == QuestType::QUEST_PARAMETER) {
        m_QuestManager.UpdateQuestStatusByParameter(99, GetChaos());
    }
    UpdateQuestStatusByItemUpgrade();
}

void Player::UpdateQuestStatusByMonsterKill(int32_t monster_id)
{
    m_QuestManager.UpdateQuestStatusByMonsterKill(monster_id);
}

void Player::GetQuestByMonster(int32_t monster_id, std::vector<Quest *> &vQuest, int32_t type)
{
    m_QuestManager.GetRelatedQuestByMonster(monster_id, vQuest, type);
}

void Player::EndQuest(int32_t code, int32_t nRewardID, bool bForce)
{
    Quest *q{nullptr};
    if (!CheckFinishableQuestAndGetQuestStruct(code, q, bForce)) {
        Messages::SendQuestMessage(120, this, "END|FAIL|0");
        return;
    }
    auto nPrevGold = GetGold();
    float fMod{0.0f};
    if (Quest::IsRandomQuest(q->m_Instance.Code)) {
        int32_t i = 0;
        int32_t nMaxItemCollectTypeCount = 3;
        do {
            fMod += (float)(q->GetRandomValue(i++) * q->GetValue(nMaxItemCollectTypeCount)) / 100.0f;
            nMaxItemCollectTypeCount += 4;
        } while (nMaxItemCollectTypeCount <= 15);
    }
    else {
        fMod = 1.0f;
    }
    auto res = ChangeGold((int64_t)(q->m_QuestBase->nGold * fMod) + GetGold());
    if (res != TS_RESULT_SUCCESS) {
        Messages::SendQuestMessage(120, this, NGemity::StringFormat("END|TOO_MUCH_MONEY|{}", res));
        return;
    }
    if (m_QuestManager.EndQuest(q)) {
        auto str = NGemity::StringFormat("END|EXP|{}|{}|{}|{}", q->m_Instance.Code, q->m_QuestBase->nEXP, q->m_QuestBase->nJP, q->m_QuestBase->nGold);
        Messages::SendQuestMessage(120, this, str);

        auto nRewardEXP = q->m_QuestBase->nEXP;
        if (GameRule::GetMaxLevel() > 0) {
            if (q->m_QuestBase->nEXP + GetEXP() >= sObjectMgr.GetNeedExp(GameRule::GetMaxLevel() - 1)) {
                if (GetEXP() < sObjectMgr.GetNeedExp(GameRule::GetMaxLevel() - 1)) {
                    nRewardEXP = sObjectMgr.GetNeedExp(GameRule::GetMaxLevel() - 1) - GetEXP();
                }
                else {
                    nRewardEXP = 0;
                }
            }
        }

        auto nRewardJP = (uint32_t)((float)q->m_QuestBase->nJP * fMod);
        if (nRewardEXP > 0) {
            // @todo: max JP
        }

        Unit::AddEXP(static_cast<int64_t>(nRewardEXP * fMod), nRewardJP, false);
        // @todo: favor

        if (q->m_QuestBase->nType == QuestType::QUEST_COLLECT || q->m_QuestBase->nType == QuestType::QUEST_HUNT_ITEM || q->m_QuestBase->nType == QuestType::QUEST_HUNT_ITEM_FROM_ANY_MONSTERS) {
            int32_t nItemCode{0};
            for (int32_t i = 0; i < ((q->m_QuestBase->nType == QuestType::QUEST_COLLECT) ? 6 : 3); ++i) {
                nItemCode = q->GetValue(2 * i);
                if (nItemCode != 0) {
                    auto pItem = FindItemByCode(nItemCode);
                    if (pItem != nullptr)
                        EraseItem(pItem, (uint64_t)q->GetValue((2 * i) + 1));
                }
            }
        }

        if (q->m_QuestBase->DefaultReward.nItemCode != 0) {
            auto pItem = Item::AllocItem(0, q->m_QuestBase->DefaultReward.nItemCode, (uint64_t)(q->m_QuestBase->DefaultReward.nQuantity * fMod), GenerateCode::BY_QUEST,
                q->m_QuestBase->DefaultReward.nLevel < 1 ? 1 : q->m_QuestBase->DefaultReward.nLevel, -1, -1, 0, 0, 0, 0, 0);

            PushItem(pItem, pItem->GetItemInstance().GetCount(), false);

            Messages::SendQuestMessage(120, this, NGemity::StringFormat("END|REWARD|{}", pItem->GetItemInstance().GetCode()));
        }
        if (nRewardID >= 0 && nRewardID < MAX_OPTIONAL_REWARD && q->m_QuestBase->OptionalReward[nRewardID].nItemCode != 0) {
            auto reward = q->m_QuestBase->OptionalReward[nRewardID];
            auto pItem = Item::AllocItem(0, reward.nItemCode, (uint64_t)(reward.nQuantity * fMod), GenerateCode::BY_QUEST, reward.nLevel < 1 ? 1 : reward.nLevel, -1, -1, 0, 0, 0, 0, 0);

            PushItem(pItem, pItem->GetItemInstance().GetCount(), false);
            Messages::SendQuestMessage(120, this, NGemity::StringFormat("END|REWARD|{}", pItem->GetItemInstance().GetCode()));
        }
        // if(q->m_QuestBase->nIsMagicPointQuest != 0)
        // UpdateQuestByQuestEnd(q);

        // DB_Insert is a "REPLACE INSERT", it acts like an Update in this case
        Quest::DB_Insert(this, q);
        onEndQuest(q);
        Messages::SendQuestList(this);
        if (!q->m_QuestBase->strClearScript.empty())
            sScriptingMgr.RunString(this, q->m_QuestBase->strClearScript);
        Save(false);
    }
    else {
        if (ChangeGold(nPrevGold) != TS_RESULT_SUCCESS) {
            NG_LOG_ERROR("server.quest", "ChangeGold/ChangeStorageGold Failed: Case[6], Player[%s}, Info[Owned(%ld), Target(%ld)]", GetName(), GetGold(), nPrevGold);
        }
        Messages::SendQuestMessage(120, this, "END|FAIL|0");
    }
}

bool Player::CheckFinishableQuestAndGetQuestStruct(int32_t code, Quest *&pQuest, bool bForce)
{
    auto q1 = m_QuestManager.FindQuest(code);
    if (q1 != nullptr && (q1->IsFinishable() || bForce)) {
        pQuest = q1;
        return true;
    }
    else {
        pQuest = nullptr;
        return false;
    }
}

int32_t Player::GetQuestProgress(int32_t nQuestID)
{
    if (m_QuestManager.IsStartableQuest(nQuestID)) {
        return 0;
    }
    else if (m_QuestManager.IsFinishedQuest(nQuestID)) {
        return 255;
    }
    else {
        auto q = m_QuestManager.FindQuest(nQuestID);
        if (q != nullptr)
            return q->IsFinishable() ? 2 : 1;
        else
            return -1;
    }
}

void Player::onJobLevelUp()
{
    Messages::SendPropertyMessage(this, this, "job_level", GetCurrentJLv());
    m_QuestManager.UpdateQuestStatusByJobLevel(GetJobDepth(), GetCurrentJLv());
}

void Player::onItemWearEffect(Item *pItem, bool bIsBaseVar, int32_t type, float var1, float var2, float fRatio)
{
    switch (type) {
    case 26:
        // @todo: set max beltslot
        break;
    case 27:
        if ((pItem->GetItemInstance().GetFlag() & ITEM_FLAG_NON_CHAOS_STONE) == 0)
            SetInt32Value(PLAYER_FIELD_MAX_CHAOS, (int32_t)(var1 + pItem->GetItemTemplate()->level * var2));
        break;
    default:
        Unit::onItemWearEffect(pItem, bIsBaseVar, type, var1, var2, fRatio);
        break;
    }
}

int32_t Player::GetMaxChaos() const
{
    return GetInt32Value(PLAYER_FIELD_MAX_CHAOS);
}

void Player::AddChaos(int32_t chaos)
{
    SetInt32Value(PLAYER_FIELD_CHAOS, GetChaos() + chaos);
    if (GetChaos() > GetMaxChaos())
        SetInt32Value(PLAYER_FIELD_CHAOS, GetMaxChaos());
    if (GetChaos() < 0)
        SetInt32Value(PLAYER_FIELD_CHAOS, 0);
    m_QuestManager.UpdateQuestStatusByParameter(99, GetChaos());
    Messages::SendPropertyMessage(this, this, "chaos", GetChaos());
}

int32_t Player::GetChaos() const
{
    return GetInt32Value(PLAYER_FIELD_CHAOS);
}

void Player::UpdateQuestStatusByItemUpgrade()
{
    std::vector<Quest *> vQuestList{};
    m_QuestManager.GetRelatedQuest(vQuestList, 64);
    for (auto &q : vQuestList) {
        for (int32_t i = 0; i < MAX_VALUE_NUMBER / 2; i += 2) {
            int32_t level = q->GetValue(i + 1);
            if (level > 0) {
                int32_t id = q->GetValue(i);
                if (id < 24) {
                    auto item = GetWornItem((ItemWearType)id);
                    if (item != nullptr) {
                        int32_t qv = item->GetItemInstance().GetLevel();
                        if (level > qv)
                            level = qv;
                        q->UpdateStatus(i / 2, level);
                    }
                    else {
                        q->UpdateStatus(i / 2, 0);
                    }
                }
            }
        }
    }
}

Position Player::GetLastTownPosition()
{
    Position pos{};
    pos.m_positionX = std::stof(GetCharacterFlag("rx"));
    pos.m_positionY = std::stof(GetCharacterFlag("ry"));
    if (pos.GetPositionX() == 0 || pos.GetPositionY() == 0) {
        switch (GetRace()) {
        case 0:
            pos.Relocate(irand(0, 100) + 6625, irand(0, 100) + 6980);
            break;
        case 1:
            pos.Relocate(irand(0, 100) + 116799, irand(0, 100) + 58205);
            break;
        case 2:
            pos.Relocate(irand(0, 100) + 153513, irand(0, 100) + 77203);
            break;
        default:
            break;
        }
    }
    return pos;
}

void Player::SetCharacterFlag(const std::string &key, const std::string &value)
{
    m_lFlagList[key] = value;
}

void Player::DB_ItemCoolTime(Player *pPlayer)
{
    if (pPlayer == nullptr)
        return;

    uint8_t idx = 0;
    int32_t cool_down = 0;
    uint32_t ct = sWorld.GetArTime();
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_REP_ITEMCOOLTIME);
    stmt->setInt32(idx++, pPlayer->GetUInt32Value(UNIT_FIELD_UID));
    for (auto &cd : pPlayer->m_nItemCooltime) {
        cool_down = cd - ct;
        if (cool_down < 0)
            cool_down = 0;
        stmt->setInt32(idx++, cool_down);
    }
    CharacterDatabase.Execute(stmt);
}

bool Player::IsUsingBow() const
{
    return m_anWear[0] != nullptr ? m_anWear[0]->IsBow() : false;
}

bool Player::IsUsingCrossBow() const
{
    return m_anWear[0] != nullptr ? m_anWear[0]->IsCrossBow() : false;
}

bool Player::EraseBullet(int64_t count)
{
    auto item = GetWornItem(ItemWearType::WEAR_SHIELD);
    if (item != nullptr && item->GetItemGroup() == ItemGroup::GROUP_BULLET && item->GetItemInstance().GetCount() >= count) {
        int64_t nc = item->GetItemInstance().GetCount() - count;
        m_QuestManager.UpdateQuestStatusByItemCount(item->GetItemInstance().GetCode(), nc);
        if (item->GetItemInstance().GetCount() == count)
            Putoff(ItemWearType::WEAR_SHIELD);

        return EraseItem(item, count);
    }
    return false;
}

void Player::AddEXP(int64_t exp, uint32_t jp, bool bApplyStamina)
{
    // @todo immoral

    // @todo summon level exp

    int64_t gain_exp = exp;
    int64_t bonus_exp = 0;
    int32_t bonus_jp = 0;
    if (exp != 0) {
        if (bApplyStamina) {
            gain_exp = (int64_t)((float)gain_exp / GameRule::GetStaminaRatio(GetLevel()));
            auto s = GetStamina();
            if (s >= gain_exp || m_bStaminaActive) {
                bonus_exp = (int64_t)((float)exp * GameRule::GetStaminaBonus());
                bonus_jp = (int32_t)((float)jp * GameRule::GetStaminaBonus());
                if (bonus_exp != 0 || bonus_jp != 0) {
                    setBonusMsg(BONUS_TYPE::BONUS_STAMINA, (int32_t)(GameRule::GetStaminaBonus() * 100.0f), bonus_exp, bonus_jp);
                    exp += bonus_exp;
                    jp += bonus_jp;
                }
                if (!m_bStaminaActive)
                    AddStamina((int32_t)(0 - gain_exp));
            }
        }

        uint32_t ct = sWorld.GetArTime();

        std::vector<Summon *> vDeActiveSummonList{}, vActiveSummonList{};

        for (auto currSummon : m_aBindSummonCard) {
            if (currSummon != nullptr && currSummon->m_pSummon != nullptr && currSummon->m_pSummon->GetHealth() != 0) {
                if (!currSummon->m_pSummon->IsInWorld()) {
                    vDeActiveSummonList.emplace_back(currSummon->m_pSummon);
                }
                else {
                    auto pos = currSummon->m_pSummon->GetCurrentPosition(ct);
                    if (GetExactDist2d(&pos) <= 525.0f) {
                        vActiveSummonList.emplace_back(currSummon->m_pSummon);
                    }
                }
            }
        }

        int64_t nActiveSummonEXP = GameRule::GetIntValueByRandomInt64(m_fActiveSummonExpAmp + m_fDistEXPMod * (double)exp);
        for (auto &sum1 : vActiveSummonList) {
            if (nActiveSummonEXP != 0 && sum1->GetLevel() < GetLevel()) {
                sum1->AddEXP(nActiveSummonEXP, 0, true);
            }
        }

        int64_t nDeactiveSummonEXP = GameRule::GetIntValueByRandomInt64((m_fDistEXPMod - 1.0f + m_fDeactiveSummonExpAmp) * (double)exp);
        for (auto &sum2 : vDeActiveSummonList) {
            if (nDeactiveSummonEXP != 0 && sum2->GetLevel() < GetLevel()) {
                sum2->AddEXP(nDeactiveSummonEXP, 0, true);
            }
        }
        Unit::AddEXP(exp, jp, true);
    }
}

void Player::applyPassiveSkillEffect(Skill *pSkill)
{
    switch (pSkill->GetSkillBase()->GetSkillEffectType()) {
    case EF_INCREASE_SUMMON_HP_MP_SP:
        m_vApplySummonPassive.emplace_back(pSkill);
        break;

    case EF_AMPLIFY_SUMMON_HP_MP_SP:
        m_vApmlifySummonPassive.emplace_back(pSkill);
        break;

    /*case SKILL_EFFECT_TYPE::EF_CREATURE_ASSIGNMENT_INCREASE:
        m_fDistEXPMod += (skill->m_SkillBase->var[0] + (skill->m_SkillBase->var[1] * (skill->m_nSkillLevel + skill->m_nSkillLevelAdd)));
        return;*/
    case EF_AMPLIFY_EXP_FOR_SUMMON:
        m_fActiveSummonExpAmp += (pSkill->GetVar(0) + pSkill->GetCurrentSkillLevel() * pSkill->GetVar(1));
        m_fDeactiveSummonExpAmp += (pSkill->GetVar(2) + pSkill->GetCurrentSkillLevel() * pSkill->GetVar(3));
        break;
    case EF_MISC:
        if (pSkill->GetSkillId() == SKILL_TECHNICAL_CREATURE_CONTROL) {
            break;
        }
        [[fallthrough]];
    default:
        Unit::applyPassiveSkillEffect(pSkill);
        break;
    }
}

int32_t Player::AddStamina(int32_t nStamina)
{
    int32_t addStamina = nStamina;
    int32_t oldStamina = GetStamina();
    int32_t maxStamina = GetInt32Value(PLAYER_FIELD_MAX_STAMINA);
    if (nStamina > 0) {
        if (oldStamina + nStamina > maxStamina) {
            if (maxStamina <= oldStamina)
                addStamina = nStamina;
            else
                addStamina = maxStamina - oldStamina;
        }
    }
    if (addStamina != 0) {
        SetInt32Value(UNIT_FIELD_STAMINA, GetStamina() + addStamina);
        if (GetStamina() < 0)
            SetInt32Value(UNIT_FIELD_STAMINA, 0);
        if (GetStamina() != oldStamina)
            Messages::SendPropertyMessage(this, this, "stamina", GetStamina());
    }
    return GetStamina();
}

int32_t Player::GetStaminaRegenRate()
{
    int32_t result = 30;

    if (!m_bUsingTent) {
        if (IsInTown())
            result = GetCondition() != 0 ? 100 : 110;
    }
    else {
        result = 120;
    }

    result += GetInt32Value(PLAYER_FIELD_STAMINA_REGEN_BONUS);
    if (GetInt32Value(PLAYER_FIELD_STAMINA_REGEN_RATE) != result) {
        SetInt32Value(PLAYER_FIELD_STAMINA_REGEN_RATE, result);
        Messages::SendPropertyMessage(this, this, "stamina_regen", result);
    }
    return result;
}

CONDITION_INFO Player::GetCondition() const
{
    if (GetInt32Value(UNIT_FIELD_STAMINA) >= 10000)
        return (GetInt32Value(UNIT_FIELD_STAMINA) < 130000) ? CONDITION_INFO::CONDITION_AVERAGE : CONDITION_INFO::CONDITION_GOOD;
    return CONDITION_INFO::CONDITION_BAD;
}

void Player::applyState(State &state)
{
    if (state.GetEffectType() == SEF_RIDING) {
        if (!HasFlag(UNIT_FIELD_STATUS, STATUS_MOVE_SPEED_FIXED))
            m_Attribute.nMoveSpeed = state.GetValue(0);
        SetUInt32Value(PLAYER_FIELD_RIDING_UID, state.m_nUID);
        return;
    }

    switch (state.GetEffectType()) {
    case SEF_CHANGING_FORM: {
        break;
    }
    case SEF_MISC: {
        switch (state.GetCode()) {
        case SC_STAMINA_SAVE:
            m_bStaminaActive = true;
            break;

            // Cracker here

        case SC_PASS_DAMAGE:
            SetFloatValue(PLAYER_FIELD_PASS_DAMAGE_RATIO, state.GetValue(0) + state.GetValue(1) * state.GetLevel());
            break;

        case SC_FUSION_WITH_SUMMON:
            break;
        default:
            Unit::applyState(state);
            break;
        }
        break;
    } break;

    default:
        Unit::applyState(state);
        break;
    }
}

void Player::setBonusMsg(BONUS_TYPE type, int32_t nBonusPerc, int64_t nBonusEXP, int32_t nBonusJP)
{
    m_pBonusInfo[type].type = type;
    m_pBonusInfo[type].rate = nBonusPerc;
    m_pBonusInfo[type].exp = nBonusEXP;
    m_pBonusInfo[type].jp = nBonusJP;
}

void Player::clearPendingBonusMsg()
{
    for (auto &bonus : m_pBonusInfo) {
        bonus.exp = -1;
        bonus.rate = -1;
        bonus.jp = -1;
    }
}

void Player::sendBonusEXPJPMsg()
{
    uint16_t cnt{0};
    for (auto &bonus : m_pBonusInfo) {
        if (bonus.exp != -1)
            cnt++;
    }
    if (cnt == 0)
        return;

    TS_SC_BONUS_EXP_JP bonusPct{};
    bonusPct.handle = GetHandle();
    for (auto &bonus : m_pBonusInfo) {
        if (bonus.exp != -1) {
            TS_BONUS_INFO bonus_info{};
            bonus_info.type = bonus.type;
            bonus_info.rate = bonus.rate;
            bonus_info.exp = bonus.exp;
            bonus_info.jp = bonus.jp;
            bonusPct.bonus.emplace_back(bonus_info);
        }
    }
    SendPacket(bonusPct);
    clearPendingBonusMsg();
}

bool Player::isInLocationType(uint8_t nLocationType)
{
    return m_WorldLocation != nullptr && m_WorldLocation->location_type == nLocationType;
}

bool Player::IsInSiegeDungeon()
{
    return !(m_WorldLocation == nullptr || m_WorldLocation->location_type != 4 || GetLayer() != 1);
}

bool Player::IsInDungeon()
{
    return sDungeonManager.GetDungeonID(GetPositionX(), GetPositionY()) != 0;
}

bool Player::IsInSiegeOrRaidDungeon()
{
    return sDungeonManager.GetDungeonID(GetPositionX(), GetPositionY()) != 0 && GetLayer() > 1;
}

bool Player::IsInEventmap()
{
    return isInLocationType(7);
}

bool Player::IsInBattleField()
{
    return isInLocationType(5);
}

bool Player::IsInTown()
{
    return isInLocationType(1) || isInLocationType(10);
}

void Player::onCompleteCalculateStat()
{
    for (auto &charm : m_vCharmList) {
        if (charm->GetItemTemplate()->eType == ItemType::TYPE_CHARM) {
            applyCharm(charm);
        }
    }
    Unit::onCompleteCalculateStat();
}

void Player::applyCharm(Item *pItem)
{
    for (int32_t i = 0; i < MAX_OPTION_NUMBER; i++) {
        switch (pItem->GetItemTemplate()->opt_type[i]) {
        case 81:
            if (pItem->GetItemTemplate()->opt_var[i][0] > GetInt32Value(PLAYER_FIELD_MAX_STAMINA))
                SetInt32Value(PLAYER_FIELD_MAX_STAMINA, (int32_t)pItem->GetItemTemplate()->opt_var[i][0]);
            break;
        case 82:
            m_bUsingTent = true;
            break;
        case 85:
            SetInt32Value(PLAYER_FIELD_STAMINA_REGEN_BONUS, (int32_t)pItem->GetItemTemplate()->opt_var[i][0]);
            break;
        default:
            break;
        }
    }
}

void Player::onBeforeCalculateStat()
{
    m_bStaminaActive = false;
    SetInt32Value(PLAYER_FIELD_MAX_STAMINA, 500000);
    m_bUsingTent = false;
    SetInt32Value(PLAYER_FIELD_MAX_CHAOS, 0);
    m_fDistEXPMod = 1.0f;
    SetUInt32Value(PLAYER_FIELD_RIDING_UID, 0);

    m_vApplySummonPassive.clear();
    m_vApmlifySummonPassive.clear();
}

void Player::AddSummonToStorage(Summon *pSummon)
{
    pSummon->m_nAccountID = GetUInt32Value(PLAYER_FIELD_ACCOUNT_ID);
    m_vStorageSummonList.emplace_back(pSummon);
    if (pSummon->HasFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE)) {
        Summon::DB_UpdateSummon(this, pSummon);
    }
}

void Player::RemoveSummonFromStorage(Summon *pSummon)
{
    auto pos = std::find(m_vStorageSummonList.begin(), m_vStorageSummonList.end(), pSummon);
    if (pos != m_vStorageSummonList.end())
        m_vStorageSummonList.erase(pos);

    pSummon->m_nAccountID = 0;
    Summon::DB_UpdateSummon(this, pSummon);
}

void Player::OpenStorage()
{
    if (IsUsingSkill())
        return;

    if (!m_bIsStorageRequested) {
        bool bReload = m_bIsStorageLoaded;
        m_bIsStorageRequested = true;
        m_bIsStorageLoaded = false;

        DB_ReadStorage(bReload);
        return;
    }

    if (m_bIsStorageLoaded) {
        Messages::SendItemList(this, true);
        openStorage();
        Messages::SendPropertyMessage(this, this, "storage_gold", GetStorageGold());
    }
}

uint16_t Player::ChangeStorageGold(int64_t gold)
{
    if (GetStorageGold() != gold) {
        if (gold > MAX_GOLD_FOR_STORAGE)
            return TS_RESULT_TOO_MUCH_MONEY;
        if (gold < 0)
            return TS_RESULT_TOO_CHEAP;
        SetUInt64Value(PLAYER_FIELD_STORAGE_GOLD, (uint64_t)gold);
        DB_UpdateStorageGold();
        Messages::SendPropertyMessage(this, this, "storage_gold", GetStorageGold());
    }
    return TS_RESULT_SUCCESS;
}

void Player::DB_UpdateStorageGold()
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_UPD_STORAGE_GOLD);
    stmt->setInt64(0, GetStorageGold());
    stmt->setInt32(1, GetInt32Value(PLAYER_FIELD_ACCOUNT_ID));
    CharacterDatabase.Execute(stmt);
}

void Player::openStorage()
{
    m_bIsUsingStorage = true;
    Messages::SendOpenStorageMessage(this);
}

Item *Player::FindStorageItem(int32_t code)
{
    return m_Storage.FindByCode(code);
}

bool Player::MoveStorageToInventory(Item *pItem, int64_t count)
{
    if (!m_bIsUsingStorage)
        return false;

    if (!pItem->IsInStorage() || !m_Storage.IsValid(pItem))
        return false;

    if (pItem->GetAccountID() != GetAccountID())
        return false;

    if (pItem->GetCount() < count)
        return false;

    if (!pItem->IsJoinable() && pItem->GetCount() != count)
        return false;

    if (!IsTakeable(pItem->GetHandle(), count))
        return false;

    Item *pNewItem{nullptr};
    if (!pItem->IsJoinable()) {
        m_Storage.Pop(pItem, count, false);
        pItem->SetIdx(m_Inventory.IssueNewIndex());
        m_Inventory.Push(pItem, count, false);
        return true;
    }
    else {
        auto nResultCnt = pItem->GetCount() - count;
        Item *pDividedItem = m_Storage.Pop(pItem, count, false);
        pNewItem = m_Inventory.Push(pDividedItem, count, false);

        m_QuestManager.UpdateQuestStatusByItemCount(pItem->GetItemCode(), pNewItem->GetCount());

        if (pItem != pDividedItem)
            pItem->DBUpdate();

        if (pNewItem != pDividedItem)
            pDividedItem->DeleteThis();

        return true;
    }
    return false;
}

bool Player::MoveInventoryToStorage(Item *pItem, int64_t count)
{
    if (!IsErasable(pItem))
        return false;

    if (pItem->GetItemBase()->flaglist[FLAG_STORAGE] != 0)
        return false;

    if ((pItem->GetItemInstance().GetFlag() & ITEM_FLAG_TAMING) != 0)
        return false;

    if (!m_bIsUsingStorage)
        return false;

    if (pItem->GetCount() < count)
        return false;

    if (!pItem->IsJoinable() && pItem->GetCount() != count)
        return false;

    Item *pNewItem{nullptr};
    if (!pItem->IsJoinable()) {
        m_Inventory.Pop(pItem, count, false);
        pItem->SetIdx(m_Storage.IssueNewIndex());
        m_Storage.Push(pItem, count, false);
        return true;
    }
    else {
        auto nResultCnt = pItem->GetCount() - count;
        m_QuestManager.UpdateQuestStatusByItemCount(pItem->GetItemCode(), nResultCnt);

        auto pDividedItem = m_Inventory.Pop(pItem, count, false);
        pDividedItem->SetIdx(m_Storage.IssueNewIndex());
        pNewItem = m_Storage.Push(pDividedItem, count, false);

        if (pItem != pDividedItem)
            pItem->DBUpdate();

        if (pNewItem != pDividedItem)
            pDividedItem->DeleteThis();
        return true;
    }
    return false;
}

bool Player::IsAlly(const Unit *pUnit)
{
    // TODO: Implement real function
    return !pUnit->IsMonster();
}

bool Player::ReadStorageSummonList(std::vector<Summon *> &vList)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_STORAGE_SUMMONLIST);
    stmt->setInt32(0, GetUInt32Value(PLAYER_FIELD_ACCOUNT_ID));
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt)) {
        do {
            Field *fields = result->Fetch();
            int32_t i = 0;

            uint32_t sid = fields[i++].GetUInt32();
            int32_t account_id = fields[i++].GetInt32();
            int32_t code = fields[i++].GetInt32();
            uint32_t card_uid = fields[i++].GetUInt32();
            uint32_t exp = fields[i++].GetUInt32();
            int32_t jp = fields[i++].GetInt32();
            uint32_t last_decreased_exp = fields[i++].GetUInt32();
            std::string name = fields[i++].GetString();
            int32_t transform = fields[i++].GetInt32();
            int32_t lv = fields[i++].GetInt32();
            int32_t jlv = fields[i++].GetInt32();
            int32_t max_level = fields[i++].GetInt32();
            int32_t fp = fields[i++].GetInt32();
            int32_t prev_level_01 = fields[i++].GetInt32();
            int32_t prev_level_02 = fields[i++].GetInt32();
            int32_t prev_id_01 = fields[i++].GetInt32();
            int32_t prev_id_02 = fields[i++].GetInt32();
            int32_t sp = fields[i++].GetInt32();
            int32_t hp = fields[i++].GetInt32();
            int32_t mp = fields[i].GetInt32();

            auto pos = std::find_if(m_vStorageSummonList.begin(), m_vStorageSummonList.end(), [sid](const Summon *s) { return s->GetUInt32Value(UNIT_FIELD_UID) == sid; });
            if (pos != m_vStorageSummonList.end()) {
                vList.emplace_back(*pos);
                continue;
            }

            auto summon = Summon::AllocSummon(nullptr, code);
            summon->SetUInt32Value(UNIT_FIELD_UID, sid);
            summon->m_nSummonInfo = code;
            summon->m_nCardUID = card_uid;
            summon->SetUInt64Value(UNIT_FIELD_EXP, exp);
            summon->SetJP(jp);
            summon->SetName(name);
            summon->SetLevel(static_cast<uint8_t>(lv));
            summon->SetCurrentJLv(jlv);
            summon->SetInt32Value(UNIT_FIELD_PREV_JOB, prev_id_01);
            summon->SetInt32Value(UNIT_FIELD_PREV_JOB + 1, prev_id_02);
            summon->SetInt32Value(UNIT_FIELD_PREV_JLV, prev_level_01);
            summon->SetInt32Value(UNIT_FIELD_PREV_JLV + 1, prev_level_02);
            summon->SetInt32Value(UNIT_FIELD_HEALTH, hp);
            summon->SetInt32Value(UNIT_FIELD_MANA, mp);
            summon->m_nTransform = transform;

            ReadSkillList(summon);
            vList.emplace_back(summon);

        } while (result->NextRow());
    }
    return true;
}

bool Player::IsSitdownable()
{
    if (!IsActable() || IsSitDown() || IsUsingSkill() || IsRiding() || HasRidingState())
        return false;
    return true;
}

bool Player::IsErasable(Item *pItem) const
{
    if (!pItem->IsInInventory())
        return false;
    if (pItem->GetItemInstance().GetOwnerHandle() != GetHandle())
        return false;
    if (pItem->GetItemInstance().GetItemWearType() != ItemWearType::WEAR_NONE)
        return false;
    if (pItem->GetItemGroup() == ItemGroup::GROUP_SKILLCARD && pItem->m_hBindedTarget != 0)
        return false;

    if (pItem->GetItemGroup() == ItemGroup::GROUP_SUMMONCARD) {
        for (int32_t i = 0; i < 6; i++) {
            // TODO: Beltslots
            if (m_aBindSummonCard[i] != nullptr && pItem->GetHandle() == m_aBindSummonCard[i]->GetHandle())
                return false;
        }
    }

    return !pItem->IsInStorage();
}

bool Player::IsSellable(Item *pItem) const
{
    bool result;
    if (!Player::IsErasable(pItem) || pItem->GetItemInstance().GetFlag() & ITEM_FLAG_TAMING)
        result = false;
    else
        result = true; // this is not 100% correct, needs to be reworked
    return result;
}

bool Player::IsMixable(Item *pItem) const
{
    return IsErasable(pItem);
}

bool Player::DropQuest(int32_t code)
{
    auto q = m_QuestManager.FindQuest(code);
    if (q == nullptr)
        return false;

    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_DEL_QUEST);
    stmt->setInt32(0, GetUInt32Value(UNIT_FIELD_UID));
    stmt->setInt32(1, q->m_Instance.nID);
    CharacterDatabase.Execute(stmt);

    onDropQuest(q);
    Messages::SendQuestList(this);
    return true;
}

void Player::onDropQuest(Quest *pQuest)
{
    if (pQuest->m_QuestBase->nType == QuestType::QUEST_PARAMETER) {
        for (int32_t i = 0; i < MAX_RANDOM_QUEST_VALUE; ++i) {
            if (pQuest->GetValue(i) == 99 && GetChaos() != 0 && pQuest->GetValue(i + 1) == 1) {
                AddChaos(-pQuest->GetValue(i + 2));
            }
        }
    }
    m_QuestManager.PopFromActiveQuest(pQuest);
    Messages::SendNPCStatusInVisibleRange(this);
}

void Player::StartTrade(uint32_t pTargetHandle)
{
    if (!m_bTrading && !m_bTradeFreezed) {
        ClearTradeInfo();
        m_bTrading = true;
        SetUInt32Value(PLAYER_FIELD_TRADE_TARGET, pTargetHandle);
    }
}

void Player::CancelTrade(bool bIsNeedBroadcast)
{
    if (bIsNeedBroadcast)
        Messages::SendTradeCancelMessage(this);

    ClearTradeInfo();
}

void Player::FreezeTrade()
{
    m_bTradeFreezed = true;
}

void Player::ClearTradeInfo()
{
    m_bTradeAccepted = false;
    m_bTradeFreezed = false;
    m_bTrading = false;
    SetUInt32Value(PLAYER_FIELD_TRADE_TARGET, 0);
    SetUInt64Value(PLAYER_FIELD_TRADE_GOLD, 0);
    m_vTradeItemList.clear();
}

Player *Player::GetTradeTarget()
{
    if (GetUInt32Value(PLAYER_FIELD_TRADE_TARGET) == 0)
        return nullptr;
    return sMemoryPool.GetObjectInWorld<Player>(GetUInt32Value(PLAYER_FIELD_TRADE_TARGET));
}

void Player::UpdateWeightWithInventory()
{
    SetFloatValue(PLAYER_FIELD_WEIGHT, m_Inventory.m_fWeightModifier + m_Inventory.m_fWeight);
}

bool Player::IsTradableWith(Player *pTarget)
{
    // Todo: Implement this one PKing is implemented. See below.
    return true;
}

/*
bool __thiscall StructPlayer__IsTradableWith(StructPlayer *this, StructPlayer *pTarget)
{
  bool v2; // bl@1
  StructPlayer *v3; // ebp@1
  StructPlayer *v4; // esi@1
  int32_t v5; // edi@11
  int32_t v6; // ebx@12
  int32_t v7; // ST00_4@12
  GuildManager *v8; // eax@12
  int32_t v9; // ST00_4@13
  GuildManager *v10; // eax@13
  bool result; // al@14
  int32_t v12; // eax@15
  char v13; // [sp+13h] [bp-1h]@8
  char v14; // [sp+18h] [bp+4h]@4

  v2 = GameRule__bIsPKServer;
  v3 = pTarget;
  v4 = this;
  if ( !GameRule__bIsPKServer && (StructPlayer__IsDemoniacCharacter(this) || StructPlayer__IsBloodyCharacter(v4)) )
  {
    v14 = 1;
  }
  else
  {
    v14 = 0;
    if ( v2 )
    {
LABEL_9:
      v13 = 0;
      goto LABEL_10;
    }
  }
  if ( !StructPlayer__IsDemoniacCharacter(v3) && !StructPlayer__IsBloodyCharacter(v3) )
    goto LABEL_9;
  v13 = 1;
LABEL_10:
  if ( v4->m_nGuildId
    && (v5 = (int32_t)&v3->m_nGuildId, v3->m_nGuildId)
    && ((v7 = v4->m_nGuildId,
         v8 = GuildManager__GetInstance(),
         v6 = GuildManager__GetAllianceID(v8, v7),
         v4->m_nGuildId == *(_DWORD *)v5)
     || (v9 = *(_DWORD *)v5, v10 = GuildManager__GetInstance(), v6 == GuildManager__GetAllianceID(v10, v9))) )
  {
    result = v4->m_bIsPK == v3->m_bIsPK;
  }
  else
  {
    v12 = v4->m_nPartyId;
    result = (v12 && v12 == v3->m_nPartyId || !v14 && !v13) && !v4->m_bIsPK && !v3->m_bIsPK;
  }
  return result;
}
*/

void Player::AddGoldToTradeWindow(int64_t nGold)
{
    if (!m_bTradeFreezed)
        SetUInt64Value(PLAYER_FIELD_TRADE_GOLD, (uint64_t)nGold);
}

bool Player::IsTradable(Item *pItem)
{
    return IsErasable(pItem) && pItem->IsTradable();
}

bool Player::AddItemToTradeWindow(Item *item, int32_t count)
{
    if (!m_bTradeFreezed && count <= item->GetItemInstance().GetCount()) {
        m_vTradeItemList[item->GetHandle()] = count;
        return true;
    }
    return false;
}

bool Player::RemoveItemFromTradeWindow(Item *item, int32_t count)
{
    if (m_bTradeFreezed)
        return false;

    auto handle = item->GetHandle();
    if (count >= 1 && m_vTradeItemList.count(handle) == 1) {
        m_vTradeItemList.erase(handle);
        return true;
    }

    return false;
}

void Player::ConfirmTrade()
{
    m_bTradeAccepted = true;
}

bool Player::ProcessTrade()
{
    if (m_bTrading && m_bTradeFreezed) {
        auto tradeTarget = GetTradeTarget();
        if (tradeTarget == nullptr)
            return false;

        int64_t nTradeTargetResult = GetGold();
        int64_t nPrevTradeTargetGold = tradeTarget->GetGold();

        uint16_t resultGold = processTradeGold();
        if (resultGold != TS_RESULT_SUCCESS) {
            if (resultGold == TS_RESULT_TOO_MUCH_MONEY) {
                Messages::SendResult(this, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, GetHandle());
                Messages::SendResult(tradeTarget, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, GetHandle());
            }

            if (ChangeGold(nTradeTargetResult) != TS_RESULT_SUCCESS || tradeTarget->ChangeGold(nPrevTradeTargetGold) != TS_RESULT_SUCCESS) {
                NG_LOG_ERROR("server.trade", "ChangeGold/ChangeStorageGold Failed: Case[3], Player[%s}, Info[Owned(%ld), Target(%ld)]", GetName(), GetGold(), nTradeTargetResult);
            }

            return false;
        }

        resultGold = tradeTarget->processTradeGold();
        if (resultGold != TS_RESULT_SUCCESS) {
            if (resultGold == TS_RESULT_TOO_MUCH_MONEY) {
                Messages::SendResult(this, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, GetHandle());
                Messages::SendResult(tradeTarget, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, GetHandle());
            }

            if (ChangeGold(nTradeTargetResult) != TS_RESULT_SUCCESS || tradeTarget->ChangeGold(nPrevTradeTargetGold) != TS_RESULT_SUCCESS) {
                NG_LOG_ERROR("server.trade", "ChangeGold/ChangeStorageGold Failed: Case[3], Player[%s], Info[Owned(%ld), Target(%ld)]", GetName(), GetGold(), nTradeTargetResult);
            }

            return false;
        }

        if (processTradeItem() != TS_RESULT_SUCCESS || tradeTarget->processTradeItem() != TS_RESULT_SUCCESS) {
            NG_LOG_ERROR("server.trade", "Player::ProcessTrade(): Error on trading with %s(%s)", m_szAccount.c_str(), tradeTarget->m_szAccount.c_str());
            return false;
        }

        Save(false);
        tradeTarget->Save(false);
        ClearTradeInfo();
        tradeTarget->ClearTradeInfo();
        return true;
    }
    return false;
}

uint16_t Player::processTradeGold()
{
    if (!m_bTrading || !m_bTradeFreezed)
        return TS_RESULT_NOT_ACTABLE;

    auto tradeTarget = GetTradeTarget();
    if (tradeTarget == nullptr)
        return TS_RESULT_NOT_EXIST;

    if (IsInWorld()) {
        int64_t tradeGold = GetTradeGold();
        int64_t prevGold = GetGold();

        if (tradeGold == 0)
            return TS_RESULT_SUCCESS;

        if (tradeGold < 0) {
            NG_LOG_ERROR("server.trade", "Player::processTradeGold(): Gold cannot be negative value");
            // GameRule::RegisterBlockAccount((const char *)(v1 + 4104));
            return TS_RESULT_ACCESS_DENIED;
        }

        if (ChangeGold(prevGold - tradeGold) == TS_RESULT_SUCCESS && tradeTarget->ChangeGold(tradeTarget->GetGold() + tradeGold) == TS_RESULT_SUCCESS) {
            return TS_RESULT_SUCCESS;
        }

        if (ChangeGold(prevGold) != TS_RESULT_SUCCESS) {
            NG_LOG_ERROR("server.trade", "ChangeGold/ChangeStorageGold Failed: Case[3], Player[%s}, Info[Owned(%ld), Target(%ld)]", GetName(), GetGold(), prevGold);
        }

        return TS_RESULT_TOO_MUCH_MONEY;
    }
    else {
        NG_LOG_ERROR("server.trade", "Player::processTradeGold(): Player not logged in %s", m_szAccount.c_str());
        // GameRule::RegisterBlockAccount((const char *)(v1 + 4104));
        return TS_RESULT_NOT_EXIST;
    }

    return TS_RESULT_SUCCESS;
}

uint16_t Player::processTradeItem()
{
    auto tradeTarget = GetTradeTarget();
    if (tradeTarget == nullptr)
        return TS_RESULT_NOT_EXIST;

    if (!IsInWorld()) {
        NG_LOG_ERROR("server.trade", "Player::processTradeGold(): Player not logged in %s", m_szAccount.c_str());
        return TS_RESULT_NOT_EXIST;
    }

    for (auto &it : m_vTradeItemList) {
        if (!GiveItem(tradeTarget, it.first, it.second)) {
            return TS_RESULT_ACCESS_DENIED;
        }
    }

    return TS_RESULT_SUCCESS;
}

bool Player::CheckTradeWeight()
{
    if (!m_bTrading)
        return false;

    auto tradeTarget = GetTradeTarget();
    if (tradeTarget == nullptr)
        return false;

    float weight = 0;
    float targetWeight = tradeTarget->GetFloatValue(PLAYER_FIELD_WEIGHT);

    for (auto &it : m_vTradeItemList) {
        auto pItem = sMemoryPool.GetObjectInWorld<Item>(it.first);
        if (pItem == nullptr)
            return false;

        weight += pItem->GetItemTemplate()->weight * it.second;
    }

    return (weight <= tradeTarget->m_Attribute.nMaxWeight - targetWeight);
}

bool Player::CheckTradeItem()
{
    for (auto &it : m_vTradeItemList) {
        auto pItem = sMemoryPool.GetObjectInWorld<Item>(it.first);
        if (pItem == nullptr)
            return false;

        if (it.second > pItem->GetItemInstance().GetCount())
            return false;
    }
    return true;
}

bool Player::GiveItem(Player *pTarget, uint32_t ItemHandle, int64_t count)
{
    Item *origItem = FindItemByHandle(ItemHandle);

    if (origItem == nullptr || !IsTradable(origItem))
        return false;

    if (count > origItem->GetItemInstance().GetCount())
        return false;

    Item *item = popItem(origItem, count, false);

    item->GetItemInstance().SetIdx(0);
    item->m_bIsNeedUpdateToDB = true;

    Item *target_item = pTarget->PushItem(item, count, false);

    if (target_item != nullptr && target_item != item)
        Item::PendFreeItem(item);

    return true;
}

Item *Player::DropItem(Player *pTarget, Item *pItem, int64_t count)
{
    if (pTarget == nullptr || pItem == nullptr)
        return nullptr;

    Item *origItem = pItem;
    Item *pNewItem = popItem(origItem, count, false);
    if (pNewItem == nullptr)
        return nullptr;
    pNewItem->Relocate(pTarget->GetPosition());
    sWorld.AddItemToWorld(pNewItem);
    return pNewItem;
}

void Player::onDead(Unit *pFrom, bool decreaseEXPOnDead)
{
    Unit::onDead(pFrom, decreaseEXPOnDead);

    if (m_pMainSummon && m_pMainSummon->IsInWorld()) {
        DoUnSummon(m_pMainSummon);
    }
    // @todo: SubSummon
}

void Player::onEnergyChange()
{
    TS_SC_ENERGY energyPct{};
    energyPct.handle = GetHandle();
    energyPct.energy = static_cast<uint16_t>(GetInt32Value(UNIT_FIELD_ENERGY));
    sWorld.Broadcast((uint32_t)(GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), (uint32_t)(GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), GetLayer(), energyPct);
}


void Player::onAfterRemoveState(State *state, bool bOnDead)
{
    if (!bOnDead)
        state->DeleteThis();

    if (state->GetEffectType() == SEF_CREATURE_PARAMETER_AMP) {
        setSummonUpdate();
    }
    // else if (state->GetEffectType() == SEF_IMMORTALIZE) {
    //     int nHealingHP = state->GetValue(2) > state->GetValue(3) * GetMaxHealth() ? state->GetValue(2) : state->GetValue(3) * GetMaxHealth();
    //     AddHealth(nHealingHP);

    //     Messages::BroadcastHPMPMessage(this, nHealingHP, 0);
    // }
    // else if (state->GetEffectType() == SEF_AUTO_RESURRECTION_AFTER_REMOVE_STATE) {
    //     int nCostMP = (state.GetValue(0) + state.GetValue(1) * state.GetLevel()) * GetMP();
    //     int nIncHP = (state.GetValue(2) + state.GetValue(3) * state.GetLevel()) * nCostMP;
    //     Resurrect(CRT_STATE, nIncHP, -nCostMP, GetLastDecreasedEXP(), true);
    // }

    Unit::onAfterRemoveState(state);
}

Summon *Player::GetRideObject() const
{
    switch (GetInt32Value(PLAYER_FIELD_RIDING_IDX)) {
    case MOUNT_ON_MAIN:
        return m_pMainSummon;
    case MOUNT_ON_SUB:
        return m_pSubSummon;
    case MOUNT_NOTHING:
    default:
        return nullptr;
    }
}

uint32_t Player::GetRideHandle() const
{
    switch (GetInt32Value(PLAYER_FIELD_RIDING_IDX)) {
    case MOUNT_ON_MAIN:
        return m_pMainSummon->GetHandle();
    case MOUNT_ON_SUB:
        return m_pSubSummon->GetHandle();
    case MOUNT_NOTHING:
    default:
        return 0;
    }
}

void Player::UnMount(const uint8_t flag, Unit *pCauser)
{
    if (GetUInt32Value(PLAYER_FIELD_RIDING_UID) == 0) {
        RemoveState(GetUInt32Value(PLAYER_FIELD_RIDING_UID));
    }
    else {
        if (GetInt32Value(PLAYER_FIELD_RIDING_IDX) == 0)
            return;

        auto pSummon = GetRideObject();
        if (bIsMoving) {
            auto t = sWorld.GetArTime();
            auto pos = GetCurrentPosition(t);
            sWorld.SetMove(this, pos, pos, 0, true, t, true);
            sWorld.SetMove(GetRideObject(), pos, pos, 0, true, t, false);
        }

        TS_SC_UNMOUNT_SUMMON unmountMsg{};
        unmountMsg.handle = GetHandle();
        unmountMsg.summon_handle = GetRideHandle();
        unmountMsg.flag = flag;

        sWorld.Broadcast(GetRX(), GetRY(), GetLayer(), unmountMsg);
        SetInt32Value(PLAYER_FIELD_RIDING_IDX, MOUNT_NOTHING);
        if (pSummon != nullptr)
            pSummon->CalculateStat();
    }

    if (flag == UNMOUNT_FALL && !IsDead()) {
        auto t = sWorld.GetArTime();
        AddState(SG_NORMAL, SC_FALL_FROM_SUMMON, 0, 1, t, t + 300);
        if (pCauser != nullptr) {
            int32_t prev_hp = GetHealth();
            damage(pCauser, GetMaxHealth() * GameRule::UNMOUNT_PENALTY);
            Messages::BroadcastHPMPMessage(this, GetHealth() - prev_hp, 0, true);
        }
    }
}

void Player::EnumSummonPassiveSkill(SkillFunctor &fn) const
{
    for (const auto &pSkill : m_vApplySummonPassive)
        fn.onSkill(pSkill);
}

void Player::EnumSummonAmplifySkill(SkillFunctor &fn) const
{
    for (const auto &pSkill : m_vApmlifySummonPassive)
        fn.onSkill(pSkill);
}

void Player::setSummonUpdate()
{
    for (auto &summon : m_vSummonList) {
        if (summon == nullptr)
            continue;

        if (summon->IsInWorld())
            summon->SetFlag(UNIT_FIELD_STATUS, STATUS_NEED_TO_CALCULATE_STAT);
        else
            summon->CalculateStat();
    }
}

bool Player::IsTakeable(uint32_t nItemHandle, int64_t cnt)
{
    auto pItem = sMemoryPool.GetObjectInWorld<Item>(nItemHandle);
    if (pItem == nullptr)
        return false;

    if (pItem->IsQuestItem() && !IsTakeableQuestItem(pItem->GetItemCode()) && !pItem->IsInStorage())
        return false;

    if (pItem->IsJoinable() && cnt != 0) {
        if (pItem->GetWeight() + GetWeight() > GetMaxWeight())
            return false;
    }
    else {
        if (pItem->GetWeight() + GetWeight() > GetMaxWeight())
            return false;
    }
    return true;
}

int32_t Player::onDamage(Unit *pFrom, ElementalType elementalType, DamageType damageType, int32_t nDamage, bool bCritical)
{
    // @Todo: Remove Riding & Sitdown
    if (nDamage > 0 &&
        (damageType == DT_NORMAL_PHYSICAL_DAMAGE || damageType == DT_NORMAL_MAGICAL_DAMAGE || damageType == DT_NORMAL_PHYSICAL_LEFT_HAND_DAMAGE || damageType == DT_NORMAL_PHYSICAL_SKILL_DAMAGE)) {
        if (IsRiding() || HasRidingState()) {
            // Remove Riding
        }
    }
    else if (IsSitDown()) {
        Standup();

        Messages::BroadcastStatusMessage(this);
        auto t = sWorld.GetArTime();
        AddState(StateType::SG_NORMAL, SC_CARELESSNESS, 0, 1, t, t + 300);
    }

    auto pMainSummon = GetMainSummon();
    if (GetFloatValue(PLAYER_FIELD_PASS_DAMAGE_RATIO) != 0 && pMainSummon != nullptr) {
        auto nPassDamage = std::min(static_cast<int32_t>(nDamage * GetFloatValue(PLAYER_FIELD_PASS_DAMAGE_RATIO)), pMainSummon->GetHealth());
        if (nPassDamage != 0) {
            if (sRegion.IsVisibleRegion(GetRX(), GetRY(), pMainSummon->GetRX(), pMainSummon->GetRY()) != 0) {
                pMainSummon->damage(pFrom, nPassDamage);
                nDamage -= nPassDamage;

                Messages::BroadcastHPMPMessage(pMainSummon, -nPassDamage, 0, false);
            }
        }
    }
    return Unit::onDamage(pFrom, elementalType, damageType, nDamage, bCritical);
}

void Player::onStandUp()
{
    RemoveStateIf(StateFlagChecker(AF_ERASE_ON_STAND_UP));
}