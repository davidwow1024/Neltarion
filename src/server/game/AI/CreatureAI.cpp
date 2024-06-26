/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
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

#include "CreatureAI.h"
#include "CreatureAIImpl.h"
#include "Creature.h"
#include "World.h"
#include "SpellMgr.h"
#include "Vehicle.h"
#include "Log.h"
#include "MapReference.h"
#include "Player.h"
#include "CreatureTextMgr.h"
#include "InstanceScript.h"

//Disable CreatureAI when charmed
void CreatureAI::OnCharmed(bool apply)
{
        me->NeedChangeAI = true;
        me->IsAIEnabled = !(me->GetCharmer());
}

AISpellInfoType* UnitAI::AISpellInfo;
AISpellInfoType* GetAISpellInfo(uint32 i) { return &CreatureAI::AISpellInfo[i]; }

CreatureAI::CreatureAI(Creature* creature) : UnitAI(creature), me(creature),
    summons(creature),
    instance(creature->GetInstanceScript()),
    m_MoveInLineOfSight_locked(false)
{
}

// if id < 0 uses broadcast id otherwise creature text id
void CreatureAI::Talk(int32 id, uint64 WhisperGuid, ChatMsg msgType, CreatureTextRange range)
{
    if (id < 0)
    {
        sCreatureTextMgr->SendBroadcast(me, id *= -1, WhisperGuid, msgType, range);
    }
    else
        sCreatureTextMgr->SendChat(me, id, WhisperGuid);
}

void CreatureAI::TalkBroadcastGroup(int32 id, uint64 WhisperGuid, ChatMsg msgType, CreatureTextRange range)
{
    sCreatureTextMgr->SendBroadcastGroup(me, id, WhisperGuid, msgType, range);
}

void CreatureAI::TalkWithDelay(uint32 Delay, int32 Id, uint64 WhisperGuid, ChatMsg msgType)
{
    me->TalkWithDelay(Delay, Id, WhisperGuid, msgType);
}

void CreatureAI::TalkToFar(uint8 id, CreatureTextRange range, uint64 WhisperGuid)
{
    sCreatureTextMgr->SendChat(me, id, WhisperGuid, CHAT_MSG_ADDON, LANG_ADDON, range);
}

void CreatureAI::TalkToMap(uint8 id)
{
    sCreatureTextMgr->SendChat(me, id, 0, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_MAP);
}

void CreatureAI::DoZoneInCombat(Creature* creature /*= NULL*/, float maxRangeToNearestTarget /* = 50.0f*/)
{
    if (!creature)
        creature = me;

    if (!creature->CanHaveThreatList())
        return;

    Map* map = creature->GetMap();
    if (!map->IsDungeon())                                  //use IsDungeon instead of Instanceable, in case battlegrounds will be instantiated
    {
        TC_LOG_DEBUG("misc", "DoZoneInCombat call for map that isn't an instance (creature entry = %d)", creature->GetTypeId() == TYPEID_UNIT ? creature->ToCreature()->GetEntry() : 0);
        return;
    }

    if (!creature->HasReactState(REACT_PASSIVE) && !creature->getVictim())
    {
        if (Unit* nearTarget = creature->SelectNearestTarget(maxRangeToNearestTarget))
            creature->AI()->AttackStart(nearTarget);
        else if (creature->isSummon())
        {
            if (Unit* summoner = creature->ToTempSummon()->GetSummoner())
            {
                Unit* target = summoner->getAttackerForHelper();
                if (!target && summoner->CanHaveThreatList() && !summoner->getThreatManager().isThreatListEmpty())
                    target = summoner->getThreatManager().getHostilTarget();
                if (target && (creature->IsFriendlyTo(summoner) || creature->IsHostileTo(target)))
                    creature->AI()->AttackStart(target);
            }
        }
    }

    if (!creature->HasReactState(REACT_PASSIVE) && !creature->getVictim())
    {
        TC_LOG_DEBUG("misc", "DoZoneInCombat called for creature that has empty threat list (creature entry = %u)", creature->GetEntry());
        return;
    }

    Map::PlayerList const& playerList = map->GetPlayers();

    if (playerList.isEmpty())
        return;

    for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
    {
        if (Player* player = itr->getSource())
        {
            if (player->isGameMaster())
                continue;

            if (player->isAlive())
            {
                creature->SetInCombatWith(player);
                player->SetInCombatWith(creature);
                creature->AddThreat(player, 0.0f);
            }

            /* Causes certain things to never leave the threat list (Priest Lightwell, etc):
            for (Unit::ControlList::const_iterator itr = player->m_Controlled.begin(); itr != player->m_Controlled.end(); ++itr)
            {
                creature->SetInCombatWith(*itr);
                (*itr)->SetInCombatWith(creature);
                creature->AddThreat(*itr, 0.0f);
            }*/
        }
    }
}

// scripts does not take care about MoveInLineOfSight loops
// MoveInLineOfSight can be called inside another MoveInLineOfSight and cause stack overflow
void CreatureAI::MoveInLineOfSight_Safe(Unit* who)
{
    if (Player* player = who->ToPlayer())
        if (player->isGameMaster())
            return;

    if (m_MoveInLineOfSight_locked == true)
        return;
    m_MoveInLineOfSight_locked = true;
    MoveInLineOfSight(who);
    m_MoveInLineOfSight_locked = false;
}

void CreatureAI::MoveInLineOfSight(Unit* who)
{
    if (!me->HasReactState(REACT_AGGRESSIVE))
        return;

    if (me->getVictim())
        return;

    if (me->GetCreatureType() == CREATURE_TYPE_NON_COMBAT_PET) // non-combat pets should just stand there and look good;)
        return;

    if (who->HasAura(51755) && me->GetExactDist2d(who) >= 5.0f) // Camouflage
        return;

    if (me->canStartAttack(who, false))
        AttackStart(who);
    //else if (who->getVictim() && me->IsFriendlyTo(who)
    //    && me->IsWithinDistInMap(who, sWorld->getIntConfig(CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS))
    //    && me->canStartAttack(who->getVictim(), true)) // TODO: if we use true, it will not attack it when it arrives
    //    me->GetMotionMaster()->MoveChase(who->getVictim());
}

void CreatureAI::EnterEvadeMode()
{
    if (!_EnterEvadeMode())
        return;

    TC_LOG_DEBUG("entities.unit", "Creature %u enters evade mode.", me->GetEntry());

    if (!me->GetVehicle()) // otherwise me will be in evade mode forever
    {
        if (Unit* owner = me->GetCharmerOrOwner())
        {
            me->GetMotionMaster()->Clear(false);
            me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, me->GetFollowAngle(), MOTION_SLOT_ACTIVE);
            Reset();
            if (me->IsVehicle()) // use the same sequence of addtoworld, aireset may remove all summons!
                me->GetVehicleKit()->Reset(false);
        }
        else
        {
            // Required to prevent attacking creatures that are evading and cause them to reenter combat
            // Does not apply to MoveFollow
            me->AddUnitState(UNIT_STATE_EVADE);
            
            // Hackfix for Nefarian - TODO: Find better way to handle this
            if (me->GetEntry() == 41376 || me->GetEntry() == 51104 || me->GetEntry() == 51105 || me->GetEntry() == 51106)
            {
                me->SetHover(true);
                me->SetCanFly(true);
            }

            me->GetMotionMaster()->MoveTargetedHome();
        }
    }
    else
    {
        Reset();
        if (me->IsVehicle()) // use the same sequence of addtoworld, aireset may remove all summons!
            me->GetVehicleKit()->Reset(false);
    }


    me->SetLastDamagedTime(0);
}

/*void CreatureAI::AttackedBy(Unit* attacker)
{
    if (!me->getVictim())
        AttackStart(attacker);
}*/

void CreatureAI::SetGazeOn(Unit* target)
{
    if (me->IsValidAttackTarget(target))
    {
        AttackStart(target);
        me->SetReactState(REACT_PASSIVE);
    }
}

bool CreatureAI::UpdateVictimWithGaze()
{
    if (!me->isInCombat())
        return false;

    if (me->HasReactState(REACT_PASSIVE))
    {
        if (me->getVictim())
            return true;
        else
            me->SetReactState(REACT_AGGRESSIVE);
    }

    if (auto victim =  me->SelectVictim())
        AttackStart(victim);
    return me->getVictim();
}

bool CreatureAI::UpdateVictim()
{
    if (!me)
        return false;

    if (!me->isInCombat())
        if (!me->getThreatManager().getThreatList().size())
            return false;

    if (!me->ToPet())
    if (!me->isInCombat())
    if (!me->HasUnitState(UNIT_STATE_CASTING))
    if (me->getThreatManager().getThreatList().size())
    if (me->CanHaveThreatList())
    {
        me->CombatStop(true);
        return false;
    }

    if (!me->HasReactState(REACT_PASSIVE) && !me->HasUnitMovementFlag(MOVEMENTFLAG_TRACKING_TARGET))
    {
        
        if (!me->HasUnitState(UNIT_STATE_CASTING))
            if (auto victim = me->SelectVictim())
            {
                if (victim != me->getVictim())
                    AttackStart(victim);
                return me->getVictim();
            }
            else
            {
                if (me->CanHaveThreatList() && !me->ToPet())
                    if (!me->HasUnitState(UNIT_STATE_CASTING))
                        me->CombatStop(true);
                EnterEvadeMode();
                return false;
            }
    }
    else
    if (me->getThreatManager().isThreatListEmpty())
    {
        if (me->CanHaveThreatList())
            if (!me->HasUnitState(UNIT_STATE_CASTING))
                me->CombatStop(true);
        EnterEvadeMode();
        return false;
    }

    if (me->GetCreatureType() == CREATURE_TYPE_CRITTER)
        {
            ThreatContainer::StorageType const& threatlist = me->getThreatManager().getThreatList();
            if (threatlist.size())
            {
                bool reset{ true };
                bool playerReset{ false };

                for (auto itr = threatlist.begin(); itr != threatlist.end(); ++itr)
                    if (auto ref = (*itr))
                        if (auto unit = ref->getTarget())
                        {
                            if (me->GetExactDist(unit) < 15.f)
                                reset = false;
                            else
                                if (unit->ToPlayer())
                                    playerReset = true;
                        }
                        /*
                                If no targets are within 15 yards, enter evade mode.
                                If a player target is more than 15 yards away, enter evade mode. (even if theres multiple players. This is a critter)
                        */

                if (reset || playerReset)
                {
                    me->CombatStop(true);
                    EnterEvadeMode();
                    return false;
                }
            }
        }


    return true;
}

bool CreatureAI::_EnterEvadeMode()
{
    if (!me->isAlive())
        return false;

    // dont remove vehicle auras, passengers arent supposed to drop off the vehicle
    if (me->isGuardian())
        me->RemoveAllNonPassiveAurasExceptType(SPELL_AURA_CONTROL_VEHICLE);
    else
        me->RemoveAllAurasExceptType(SPELL_AURA_CONTROL_VEHICLE);

    // sometimes bosses stuck in combat?
    me->DeleteThreatList();
    me->CombatStop(true);
    me->LoadCreaturesAddon();
    me->SetLootRecipient(NULL);
    me->ResetPlayerDamageReq();

    if (me->IsInEvadeMode())
        return false;

    return true;
}

Creature* CreatureAI::DoSummon(uint32 entry, const Position& pos, uint32 despawnTime, TempSummonType summonType)
{
    return me->SummonCreature(entry, pos, summonType, despawnTime);
}

Creature* CreatureAI::DoSummon(uint32 entry, WorldObject* obj, float radius, uint32 despawnTime, TempSummonType summonType)
{
    Position pos;
    obj->GetRandomNearPosition(pos, radius);
    return me->SummonCreature(entry, pos, summonType, despawnTime);
}

Creature* CreatureAI::DoSummonFlyer(uint32 entry, WorldObject* obj, float flightZ, float radius, uint32 despawnTime, TempSummonType summonType)
{
    Position pos;
    obj->GetRandomNearPosition(pos, radius);
    pos.m_positionZ += flightZ;
    return me->SummonCreature(entry, pos, summonType, despawnTime);
}

void CreatureAI::AddEncounterFrame()
{
    if (InstanceScript* instance = me->GetInstanceScript())
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me);
}

void CreatureAI::RemoveEncounterFrame()
{
    if (InstanceScript* instance = me->GetInstanceScript())
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
}
