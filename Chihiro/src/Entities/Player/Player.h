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
#include "Inventory.h"
#include "Packets/MessageSerializerBuffer.h"
#include "QuestManager.h"
#include "TimeSync.h"
#include "Unit.h"
#include "GameRule.h"
#include "WorldSession.h"

class Item;
class Summon;
class WorldLocation;

constexpr int32_t MAX_ITEM_COOLTIME_GROUP = 20;

enum BONUS_TYPE : int {
    BONUS_PCBANG = 0x0,
    BONUS_STAMINA = 0x1,
    BONUS_PREMIUM_PCBANG = 0x2,
    MAX_BONUS_TYPE = 0x3,
};

enum MOUNT_MODE : uint8_t {
    MOUNT_NOTHING = 0,
    MOUNT_ON_MAIN = 1,
    MOUNT_ON_SUB = 2,
};

enum UNMOUNT_TYPE : uint8_t {
    UNMOUNT_NORMAL = 0,
    UNMOUNT_FALL = 1,
    UNMOUNT_UNSUMMON = 2,
};

enum TRADE_MODE : int {
    TM_REQUEST_TRADE = 0,
    TM_ACCEPT_TRADE,
    TM_BEGIN_TRADE,
    TM_CANCEL_TRADE,
    TM_REJECT_TRADE,
    TM_ADD_ITEM,
    TM_ADD_GOLD,
    TM_FREEZE_TRADE,
    TM_CONFIRM_TRADE,
    TM_PROCESS_TRADE,
    TM_REMOVE_ITEM,
    TM_MODIFY_COUNT
};

enum struct JOB_RACE : int32_t {
    GAIA = 3,
    DEVA = 4,
    ASURA = 5,
};

struct BonusInfo {
    int32_t type;
    int32_t rate;
    int64_t exp;
    int32_t jp;
};

enum CONDITION_INFO : int { CONDITION_GOOD = 0, CONDITION_AVERAGE = 1, CONDITION_BAD = 2 };

class Player : public Unit, public QuestEventHandler, public InventoryEventReceiver {
public:
    friend class Messages;
    friend class Summon;
    friend class WorldSession;

    explicit Player(uint32_t);
    ~Player() override;
    // Deleting the copy & assignment operators
    // Better safe than sorry
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    void CleanupsBeforeDelete();

    /* ****************** STATIC FUNCTIONS *******************/
    static void EnterPacket(TS_SC_ENTER &, Player *, Player *);
    static void DoEachPlayer(const std::function<void(Player *)> &fn);
    static Player *FindPlayer(const std::string &szName);
    static void DB_ItemCoolTime(Player *);
    /* ****************** STATIC END *******************/

    /* ****************** QUEST *******************/
    void DoEachActiveQuest(const std::function<void(Quest *)> &fn) { m_QuestManager.DoEachActiveQuest(fn); }

    int32_t GetAccountID() const { return GetInt32Value(PLAYER_FIELD_ACCOUNT_ID); }

    void UpdateQuestStatusByMonsterKill(int32_t monster_id);
    void GetQuestByMonster(int32_t monster_id, std::vector<Quest *> &vQuest, int32_t type);
    void StartQuest(int32_t code, int32_t nStartQuestID, bool bForce);
    void EndQuest(int32_t code, int32_t nRewardID, bool bForce);
    void UpdateQuestStatusByItemUpgrade();
    void onStatusChanged(Quest *quest, int32_t nOldStatus, int32_t nNewStatus) override;
    void onProgressChanged(Quest *quest, QuestProgress oldProgress, QuestProgress newProgress) override;
    int32_t GetQuestProgress(int32_t nQuestID);

    int32_t GetActiveQuestCount() const { return (int32_t)m_QuestManager.m_vActiveQuest.size(); }

    bool IsTakeableQuestItem(int32_t code) { return m_QuestManager.IsTakeableQuestItem(code); }
    bool IsTakeable(uint32_t nItemHandle, int64_t cnt);

    bool CheckFinishableQuestAndGetQuestStruct(int32_t code, Quest *&pQuest, bool bForce);
    bool IsInProgressQuest(int32_t code);
    bool IsStartableQuest(int32_t code, bool bForQuestMark);
    bool IsFinishableQuest(int32_t code);
    bool CheckFinishableQuestAndGetQuestStruct(int32_t code);
    Quest *FindQuest(int32_t code);
    bool DropQuest(int32_t code);

    Summon *GetRideObject() const;
    uint32_t GetRideHandle() const;

    /* ****************** QUEST END *******************/

    int32_t GetPermission() const { return GetInt32Value(PLAYER_FIELD_PERMISSION); }
    int64_t GetGold() const { return GetUInt64Value(PLAYER_FIELD_GOLD); }
    uint32_t GetTamingTarget() const { return m_hTamingTarget; }
    Summon *GetMainSummon() const { return m_pMainSummon; }
    Summon *GetSubSummon() const { return m_pSubSummon; }
    int64_t GetTradeGold() const { return GetUInt64Value(PLAYER_FIELD_TRADE_GOLD); }
    int64_t GetStorageGold() const { return GetUInt64Value(PLAYER_FIELD_STORAGE_GOLD); }
    int32_t GetPartyID() const { return GetInt32Value(PLAYER_FIELD_PARTY_ID); }
    int32_t GetGuildID() const { return GetInt32Value(PLAYER_FIELD_GUILD_ID); }
    bool HasRidingState() { return GetUInt32Value(PLAYER_FIELD_RIDING_UID) != 0; }
    bool IsRiding() { return GetInt32Value(PLAYER_FIELD_RIDING_IDX) != MOUNT_NOTHING; }
    float GetWeight() const { return GetFloatValue(PLAYER_FIELD_WEIGHT); }

    int32_t AddStamina(int32_t nStamina);
    int32_t GetStaminaRegenRate();
    CONDITION_INFO GetCondition() const;

    bool IsSitdownable();
    bool IsSitDown() const override { return m_bSitdown; }
    void SitDown() { m_bSitdown = true; }
    void Standup()
    {
        m_bSitdown = false;
        onStandUp();
    }

    void SetWalk(bool bOn) { m_bWalk = bOn; }
    bool IsWalking() const { return m_bWalk; }

    std::string GetCharacterFlag(const std::string &flag) { return m_lFlagList[flag]; }

    /* ****************** DATABASE *******************/
    bool ReadCharacter(const std::string &, int32_t);
    bool ReadItemList(int32_t);
    bool ReadItemCoolTimeList(int32_t);
    bool ReadSummonList(int32_t);
    bool ReadEquipItem();
    bool ReadSkillList(Unit *);
    bool ReadQuestList();
    bool ReadStateList(Unit *);
    bool ReadStorageSummonList(std::vector<Summon *> &);
    void DB_ReadStorage(bool bReload);
    void DB_UpdateStorageGold();
    /* ****************** DATABASE END *******************/

    // Warping
    void PendWarp(int32_t x, int32_t y, uint8_t layer);

    void SetCharacterFlag(const std::string &key, const std::string &value);

    // Summon
    void LogoutNow(int32_t callerIdx);
    void RemoveAllSummonFromWorld();
    void EnumSummonPassiveSkill(struct SkillFunctor &fn) const;
    void EnumSummonAmplifySkill(struct SkillFunctor &fn) const;

    void SendLoginProperties();
    void SendGoldChaosMessage();
    void SendJobInfo();
    void SendWearInfo();

    Summon *GetSummon(int32_t);
    Summon *GetSummonByHandle(uint32_t);
    void AddSummon(Summon *, bool);
    void UnMount(const uint8_t flag = UNMOUNT_NORMAL, Unit *pCauser = nullptr);

    // Database
    void Update(uint32_t diff) override;
    void OnUpdate() override;
    void Save(bool);

    // Item relevant
    Item *FindItemByCode(int32_t);
    Item *FindItemBySID(int64_t);
    Item *FindItemByHandle(uint32_t);
    Item *FindItem(uint32_t code, uint32_t flag, bool bFlag);
    Item *FindStorageItem(int32_t code);
    /* InventoryEventReceiver Start*/
    void onAdd(Inventory *pInventory, Item *pItem, bool bSkipUpdateItemToDB) override;
    void onRemove(Inventory *pInventory, Item *pItem, bool bSkipUpdateItemToDB) override;
    void onChangeCount(Inventory *pInventory, Item *pItem, bool bSkipUpdateItemToDB) override;
    /* InventoryEventReceiver End*/
    Item *PushItem(Item *, int64_t, bool = false);
    Item *PopItem(Item *, int64_t, bool = false);
    Item *GetItem(uint32_t idx);
    Item *GetStorageItem(uint32_t idx);
    uint32_t GetItemCount() const;
    uint32_t GetStorageItemCount() const;
    bool GiveItem(Player *pTarget, uint32_t ItemHandle, int64_t count);
    Item *DropItem(Player *pTarget, Item *pItem, int64_t count);
    bool MoveStorageToInventory(Item *pItem, int64_t count);
    bool MoveInventoryToStorage(Item *pItem, int64_t count);
    bool IsUsingStorage() const { return m_bIsUsingStorage; }
    bool EraseItem(Item *pItem, int64_t count);
    bool EraseBullet(int64_t count);
    bool IsMixable(Item *pItem) const;
    bool IsErasable(Item *pItem) const;
    bool IsSellable(Item *pItem) const;

    // Storage Relevant
    bool RemoveSummon(Summon *pSummon);
    void AddSummonToStorage(Summon *pSummon);
    void RemoveSummonFromStorage(Summon *pSummon);
    void OpenStorage();

    void StartTrade(uint32_t pTargetHandle);
    void CancelTrade(bool bIsNeedBroadcast);
    void FreezeTrade();
    void ConfirmTrade();
    bool ProcessTrade();
    void ClearTradeInfo();
    Player *GetTradeTarget();
    bool IsTradableWith(Player *pTarget);
    void AddGoldToTradeWindow(int64_t nGold);
    bool AddItemToTradeWindow(Item *item, int32_t count);
    bool RemoveItemFromTradeWindow(Item *item, int32_t count);
    bool IsTradable(Item *pItem);
    bool CheckTradeWeight();
    bool CheckTradeItem();

    void AddEXP(int64_t exp, uint32_t jp, bool bApplyStamina) override;
    uint16_t putonItem(ItemWearType, Item *) override;
    uint16_t putoffItem(ItemWearType) override;
    void SendItemWearInfoMessage(Item *item, Unit *u);
    void ChangeLocation(float x, float y, bool bByRequest, bool bBroadcast);

    bool IsAlly(const Unit *pUnit) override;

    /* ****************** PK *******************/
    inline bool IsPKOn() const { return m_bIsPK; }
    inline void SetPKOn() { m_bIsPK = true; }
    inline void SetPKOff() { m_bIsPK = false; }
    bool IsBloodyCharacter() const { return (GetFloatValue(PLAYER_FIELD_IP) >= GameRule::MORAL_LIMIT); }
    bool IsDemoniacCharacter() const { return (GetFloatValue(PLAYER_FIELD_IP) >= GameRule::CRIME_LIMIT); }

    /* ****************** DIALOG *******************/
    void SetLastContact(const std::string &, uint32_t);
    void SetLastContact(const std::string &, const std::string &);
    std::string GetLastContactStr(const std::string &);
    uint32_t GetLastContactLong(const std::string &);
    void SetDialogTitle(const std::string &, int32_t);
    void SetDialogText(const std::string &);
    void AddDialogMenu(const std::string &, const std::string &);
    void ClearDialogMenu();

    bool IsFixedDialogTrigger(const std::string &szTrigger) { return false; }

    bool HasDialog() { return !m_szDialogTitle.empty(); }

    void ShowDialog();
    bool IsValidTrigger(const std::string &);
    /* ****************** DIALOG END *******************/

    /* ****************** WORLDLOCATION BEGIN *******************/
    bool IsInTown();
    // Function       :   public bool StructPlayer::IsInField()
    bool IsInBattleField();
    bool IsInEventmap();
    // Function       :   public bool IsUsingTent()
    bool IsInSiegeOrRaidDungeon();
    bool IsInSiegeDungeon();
    bool IsInDungeon();
    /* ****************** WORLDLOCATION END *******************/

    void onChangeProperty(std::string, int32_t);
    Position GetLastTownPosition();

    void DoSummon(Summon *pSummon, Position pPosition);
    void DoUnSummon(Summon *pSummon);

    bool IsHunter();
    bool IsFighter();
    bool IsMagician();
    bool IsSummoner();
    bool IsUsingBow() const override;
    bool IsUsingCrossBow() const override;

    bool IsBattleMode() const override { return m_bIsBattleMode; }
    void SetBattleModeOn() { m_bIsBattleMode = true; }
    void SetBattleModeOff() { m_bIsBattleMode = false; }

    bool IsPlayer() const override { return true; }

    int32_t GetMoveSpeed() override;

    bool TranslateWearPosition(ItemWearType &pos, Item *item, std::vector<int32_t> *ItemList) override;

    CreatureStat *GetBaseStat() const override;
    Inventory *GetInventory() { return &m_Inventory; }

    uint32_t GetCreatureGroup() const override { return 9; }

    uint16_t ChangeGold(int64_t);
    uint16_t ChangeStorageGold(int64_t);

    uint16_t IsUseableItem(Item *pItem, Unit *pTarget);
    uint16_t UseItem(Item *pItem, Unit *pTarget, const std::string &szParameter);

    void SendPacket(const XPacket &pPacket);

    template<class TS_SERIALIZABLE_PACKET>
    void SendPacket(TS_SERIALIZABLE_PACKET const &packet)
    {
        if (m_session == nullptr)
            return;
        m_session->SendPacket(packet);
    }

    WorldSession &GetSession() const { return *m_session; }

    void SetClientInfo(const std::string &value) { m_szClientInfo = value; }

    void SetSession(WorldSession *session) { m_session = session; }

    void applyJobLevelBonus() override;
    void UpdateWeightWithInventory();

    Item *m_aBindSummonCard[6]{nullptr};
    WorldLocation *m_WorldLocation{nullptr};
    int32_t m_nWorldLocationId{0};
    TimeSynch m_TS{200, 2, 10};

    Summon *m_pMainSummon{nullptr};
    Summon *m_pSubSummon{nullptr};
    uint32_t m_hTamingTarget{};
    int32_t GetChaos() const;
    int32_t GetMaxChaos() const;
    inline bool IsInParty() const { return GetPartyID() != 0; }
    void AddChaos(int32_t chaos);
    bool m_bSitdown{false};
    bool m_bTrading{false};
    bool m_bWalk{false};
    bool m_bTradeFreezed{false};
    bool m_bTradeAccepted{false};

protected:
    bool isInLocationType(uint8_t nLocationType);
    void onCompleteCalculateStat() override;
    void onBeforeCalculateStat() override;
    void onRegisterSkill(int64_t skillUID, int32_t skill_id, int32_t prev_level, int32_t skill_level) override;
    void onItemWearEffect(Item *pItem, bool bIsBaseVar, int32_t type, float var1, float var2, float fRatio) override;
    void onExpChange() override;
    void onEnergyChange() override;
    void onJobLevelUp() override;
    void onCantAttack(uint32_t target, uint32_t t) override;
    void onModifyStatAndAttribute() override;
    void applyPassiveSkillEffect(Skill *skill) override;
    void applyState(State &state) override;
    void onDead(Unit *pFrom, bool decreaseEXPOnDead) override;
    int32_t onDamage(Unit *pFrom, ElementalType elementalType, DamageType damageType, int32_t nDamage, bool bCritical) override;
    void onAfterRemoveState(State *state, bool bByDead = false) override;

    void onStartQuest(Quest *pQuest);
    void updateQuestStatus(Quest *pQuest);
    void onEndQuest(Quest *pQuest);

private:
    void onDropQuest(Quest *pQuest);
    void openStorage();
    void onStandUp();
    void setSummonUpdate();
    Item *popItem(Item *pItem, int64_t cnt, bool bSkipUpdateToDB);

    void applyCharm(Item *pItem);
    void setBonusMsg(BONUS_TYPE type, int32_t nBonusPerc, int64_t nBonusEXP, int32_t nBonusJP);
    void clearPendingBonusMsg();
    void sendBonusEXPJPMsg();

    uint16_t processTradeGold();
    uint16_t processTradeItem();

    WorldSession *m_session{nullptr};
    std::string m_szAccount;
    QuestManager m_QuestManager{};
    bool m_bStaminaActive{false};
    bool m_bUsingTent{false};
    float m_fDistEXPMod{1.0f};
    float m_fActiveSummonExpAmp{0.0f};
    float m_fDeactiveSummonExpAmp{0.0f};

    uint32_t m_nItemCooltime[MAX_ITEM_COOLTIME_GROUP]{0};

    std::unordered_map<std::string, std::string> m_lFlagList{};
    std::unordered_map<std::string, std::string> m_hsContact{};

    std::vector<Skill *> m_vApplySummonPassive;
    std::vector<Skill *> m_vApmlifySummonPassive;

    std::vector<Summon *> m_vSummonList{};
    std::vector<Summon *> m_vStorageSummonList{};
    std::vector<Item *> m_vCharmList{};

    std::unordered_map<uint32_t, int32_t> m_vTradeItemList{};

    BonusInfo m_pBonusInfo[BONUS_TYPE::MAX_BONUS_TYPE]{};

    Inventory m_Inventory;
    Inventory m_Storage;
    bool m_bIsStorageRequested{false};
    bool m_bIsStorageLoaded{false};
    bool m_bIsUsingStorage{false};
    bool m_bIsPK{false};
    bool m_bIsBattleMode{false};

    // Dialog stuff
    int32_t m_nDialogType{};
    bool m_bNonNPCDialog{false};
    std::string m_szDialogTitle{};
    std::string m_szDialogText{};
    std::string m_szDialogMenu{};
    std::string m_szSpecialDialogMenu{};
    std::string m_szClientInfo{};
};
