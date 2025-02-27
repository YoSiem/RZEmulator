
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

#include "SkillBase.h"

#include "World.h"

bool SkillBase::IsUseableWeapon(ItemClass cl)
{
    auto c = (int32_t)cl;

    if (c <= 210) {
        switch (c) {
        case 101:
            return vf_one_hand_sword != 0;
        case 102:
            return vf_two_hand_sword != 0;
        case 98:
            return vf_double_dagger != 0;
        case 96:
            return vf_double_sword != 0;
        case 103:
            return vf_dagger != 0;
        case 104:
            return vf_spear != 0;
        case 105:
            return vf_axe != 0;
        case 113:
            return vf_one_hand_axe != 0;
        case 95:
            return vf_double_axe != 0;
        case 106:
            return vf_one_hand_mace != 0;
        case 107:
            return vf_two_hand_mace != 0;
        case 109:
            return vf_lightbow != 0;
        case 108:
            return vf_heavybow != 0;
        case 110:
            return vf_crossbow != 0;
        case 111:
            return vf_one_hand_staff != 0;
        case 112:
            return vf_two_hand_staff != 0;
        case 210:
            return vf_shield_only != 0;
        default:
            return false;
        }
    }

    return false;
}

int32_t SkillBase::GetStateSecond(int32_t skill_lv, int32_t enhance_lv)
{
    return ((int32_t)state_second + skill_lv * (int32_t)state_second_per_level) + ((int32_t)enhance_lv * (int32_t)state_second_per_enhance);
}

int32_t SkillBase::GetHitBonus(int32_t enhance, int32_t level_diff) const
{
    return hit_bonus + level_diff * percentage + enhance * hit_bonus_per_enhance;
}

int32_t SkillBase::GetStateLevel(int32_t skill_lv, int32_t enhance_lv)
{
    return (int32_t)(state_level_base + (state_level_per_enhance * enhance_lv) + (state_level_per_skl * skill_lv));
}

uint32_t SkillBase::GetCastDelay(int32_t skill_lv, int32_t enhance)
{
    return static_cast<uint32_t>((delay_cast + delay_cast_per_skl * (skill_lv)) * (delay_cast_mode_per * enhance + 1.0f));
}

uint32_t SkillBase::GetCoolTime(int32_t enhance) const
{
    return sWorld.getBoolConfig(CONFIG_NO_SKILL_COOLTIME) ? 0 : static_cast<uint32_t>(delay_cooltime * (delay_cooltime_mode * enhance + 1.0f));
}

int32_t SkillBase::GetNeedJobPoint(int32_t skill_lv)
{
    int32_t result;

    if (skill_lv <= 50)
        result = this->m_need_jp[skill_lv - 1];
    else
        result = this->m_need_jp[49];
    return result;
}

bool SkillBase::IsUsable(uint8_t nUseIndex) const
{
    switch (nUseIndex) {
    case 0:
        return uf_self != 0;
    case 1:
        return uf_party != 0;
    case 2:
        return uf_guild != 0;
    case 3:
        return uf_neutral != 0;
    case 4:
        return uf_purple != 0;
    case 5:
        return uf_enemy != 0;
    default:
        return false;
    }
}

int32_t SkillBase::GetCostEnergy(uint8_t skill_lv) const
{
    return static_cast<int32_t>((cost_energy_per_skl * skill_lv) + cost_energy);
}

int32_t SkillBase::GetID() const
{
    return id;
}

int32_t SkillBase::GetNameID() const
{
    return text_id;
}

bool SkillBase::IsPassive() const
{
    // Yes, the DB Value is actually is_active, not is_passive.
    // No questions asked.
    return is_passive == 0;
}

bool SkillBase::IsPhysicalSkill() const
{
    return is_physical_act == 0;
}

bool SkillBase::IsMagicalSkill() const
{
    return is_physical_act;
}

bool SkillBase::IsHarmful() const
{
    return is_harmful;
}

bool SkillBase::IsNeedTarget() const
{
    return is_need_target;
}

bool SkillBase::IsValidToCorpse() const
{
    return is_corpse;
}

bool SkillBase::IsToggle() const
{
    return is_toggle;
}

int32_t SkillBase::GetCastRange() const
{
    return cast_range;
}

int32_t SkillBase::GetValidRange() const
{
    return valid_range;
}

int32_t SkillBase::GetToggleGroup() const
{
    return toggle_group;
}

int32_t SkillBase::GetSkillTargetType() const
{
    return target;
}

int32_t SkillBase::GetSkillEffectType() const
{
    return effect_type;
}

int32_t SkillBase::GetElementalType() const
{
    return elemental;
}

bool SkillBase::IsNeedShield() const
{
    return vf_shield_only;
}

bool SkillBase::IsNeedWeapon() const
{
    return !vf_is_not_need_weapon;
}

bool SkillBase::IsValid() const
{
    return is_valid != ST_INVALID;
}

bool SkillBase::IsSystemSkill() const
{
    return is_valid == ST_SYSTEM;
}

int32_t SkillBase::GetCriticalBonus(int32_t skill_lv) const
{
    return critical_bonus + critical_bonus_per_skl * skill_lv;
}

int32_t SkillBase::GetCostEXP(int32_t skill_lv, int32_t enhance) const
{
    return cost_exp + cost_exp_per_enhance * enhance;
}

int32_t SkillBase::GetCostJP(int32_t skill_lv, int32_t enhance) const
{
    return cost_jp + cost_jp_per_enhance * enhance;
}

int32_t SkillBase::GetCostItemCode() const
{
    return cost_item;
}

int64_t SkillBase::GetCostItemCount(int32_t skill_lv) const
{
    return cost_item_count + (skill_lv * cost_item_count_per);
}

int32_t SkillBase::GetCostHP(int32_t skill_lv) const
{
    return cost_hp + cost_hp_per_skl * (skill_lv);
}

float SkillBase::GetCostHPPercent(int32_t skill_lv) const
{
    return cost_hp_per + cost_hp_per_skl_per * skill_lv;
}

int32_t SkillBase::GetCostMP(int32_t skill_lv, int32_t enhance) const
{
    return cost_mp + cost_mp_per_skl * (skill_lv) + cost_mp_per_enhance * (enhance);
}

float SkillBase::GetCostMPPercent(int32_t skill_lv) const
{
    return cost_mp_per + cost_mp_per_skl_per * skill_lv;
}

int32_t SkillBase::GetCostHavoc(int32_t skill_lv) const
{
    return cost_havoc + cost_havoc_per_skl * (skill_lv);
}

int32_t SkillBase::GetCostEnergy(int32_t skill_lv) const
{
    return cost_energy + cost_energy_per_skl * (skill_lv);
}

int32_t SkillBase::GetCostItem() const
{
    return cost_item;
}

int32_t SkillBase::GetNeedLevel() const
{
    return need_level;
}

int32_t SkillBase::GetNeedHP() const
{
    return need_hp;
}

int32_t SkillBase::GetNeedMP() const
{
    return need_mp;
}

int32_t SkillBase::GetNeedHavoc() const
{
    return need_havoc;
}

int32_t SkillBase::GetNeedHavocBurst() const
{
    return need_havoc_burst;
}

int32_t SkillBase::GetNeedStateId() const
{
    return need_state_id;
}

uint8_t SkillBase::GetNeedStateLevel() const
{
    return need_state_level;
}

bool SkillBase::NeedStateExhaust() const
{
    return need_state_exhaust;
}

uint32_t SkillBase::GetFireRange() const
{
    return valid_range * 12;
}

int32_t SkillBase::GetStateId() const
{
    return state_id;
}

int32_t SkillBase::GetProbabilityOnHit(int32_t slv) const
{
    return probability_on_hit + slv * probability_inc_by_slv;
}

int32_t SkillBase::GetStateType() const
{
    return state_type;
}

bool SkillBase::IsUseableOnSummon() const
{
    return tf_summon;
}

bool SkillBase::IsUseableOnMonster() const
{
    return tf_monster;
}

bool SkillBase::IsUseableOnAvatar() const
{
    return tf_avatar;
}

int32_t SkillBase::GetHatePoint(int32_t lv, int32_t point, int32_t enhance) const
{
    if (hate_mod == 0)
        return 0;

    if (hate_mod < 0)
        return (int32_t)(hate_basic + lv * hate_per_skl + enhance * hate_per_enhance);

    return (int32_t)(hate_basic + point * hate_mod);
}

uint32_t SkillBase::GetCommonDelay() const
{
    return static_cast<uint32_t>(delay_common);
}
