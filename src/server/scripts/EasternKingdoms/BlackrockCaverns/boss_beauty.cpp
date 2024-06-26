/*
 * Copyright (C) 2013-2014 trinity core og
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
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

/*
 * TODO:
 * Script: 95% Complete
 * - Check timers
 */

#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "blackrock_caverns.h"

enum spells
{
    SPELL_BERSERKER_CHARGE        = 56106,
    SPELL_FLAME_BREAK             = 76032,
    SPELL_TERRIFYING_ROAR         = 14100,
    SPELL_MAGMA_SPIT              = 76031,
    SPELL_BERSERK                 = 26662,

    SPELL_LAVA_DROOL              = 76628,
    SPELL_LITTLE_BIG_FLAME_BREATH = 76665,
    SPELL_ALMOST_FEROCIOUS        = 77783
};

enum events
{
    EVENT_BERSERKER_CHARGE        = 1,
    EVENT_FLAME_BREAK             = 2,
    EVENT_TERRIFYING_ROAR         = 3,
    EVENT_MAGMA_SPIT              = 4,
    EVENT_LAVA_DROOL              = 5,
    EVENT_LITTLE_BIG_FLAME_BREATH = 6
};

uint32 const Entry[3] =
{
    NPC_LUCKY,
    NPC_BUSTER,
    NPC_RUNTY
};

Position const SummonPositions[3] =
{
    { 115.989914f, 570.281799f, 76.449539f, 0.136303f }, // Lucky
    { 117.035263f, 590.365723f, 76.343124f, 0.059338f }, // Buster
    { 81.2632f,615.635f, 77.163f, 2.051f  }  // Runty  
}; 

class boss_beauty : public CreatureScript
{
    public:
        boss_beauty() : CreatureScript("boss_beauty") { }
            
        struct boss_beautyAI : public BossAI
        {
            boss_beautyAI(Creature* creature) : BossAI(creature, BOSS_BEAUTY) {}

            void Reset()
            {
                _Reset();
                instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
                killCounter = 0;

                for (uint8 i = 0; i < 3; ++i)
                    me->SummonCreature(Entry[i], SummonPositions[i], TEMPSUMMON_MANUAL_DESPAWN);
            }

            void DamageTaken(Unit* attacker, uint32& damage)
            {
                if (attacker->GetEntry() == NPC_RAZ_THE_CRAZED)
                {
                    damage = 0;
                    instance->DoOnPlayers([this](Player* player)
                        {
                            me->Kill(player, true, false);
                        });

                    me->Kill(attacker, false, false);
                }
            }

            void EnterCombat(Unit* who)
            {
                _EnterCombat();
                instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me);
                events.ScheduleEvent(EVENT_BERSERKER_CHARGE, 5000);
                events.ScheduleEvent(EVENT_FLAME_BREAK, 7000);
                events.ScheduleEvent(EVENT_TERRIFYING_ROAR, urand(10000, 15000));
                events.ScheduleEvent(EVENT_MAGMA_SPIT, 12000);

                if(IsHeroic())
                    for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
						    if (Creature* beautyKids = ObjectAccessor::GetCreature(*me, *itr))
							    if (beautyKids->isAlive() && beautyKids->GetEntry() != NPC_RUNTY)
								    beautyKids->AI()->AttackStart(who);		
            }

            void SummonedCreatureDies(Creature* summon, Unit* /*killer*/)
            {
                switch (summon->GetEntry())
                {
                    case NPC_RUNTY:
                        if(IsHeroic())
                            DoCast(me, SPELL_BERSERK);
                    default:
                        break;
                }
            }

            void JustDied(Unit* /*killer*/)
            {
                instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
                _JustDied();
            }

            void JustSummoned(Creature* summon)
            {
                summons.Summon(summon);
                summon->AddAura(SPELL_ALMOST_FEROCIOUS, summon);
            }

            void UpdateAI(uint32 const diff)
            {
                if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                    return;

				if (IsHeroic())
				{
					if (Creature * buster = GetClosestCreatureWithEntry(me, NPC_BUSTER, 100.0f))
						DoZoneInCombat(buster);
					
					if (Creature * lucky = GetClosestCreatureWithEntry(me, NPC_LUCKY, 100.0f))
						DoZoneInCombat(lucky);
				}
				
                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_BERSERKER_CHARGE:
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 100.0f, true))
                                DoCast(target, SPELL_BERSERKER_CHARGE);
                            events.ScheduleEvent(EVENT_BERSERKER_CHARGE, urand(7000, 20000));
                            break;
                        case EVENT_FLAME_BREAK:
                            DoCastAOE(SPELL_FLAME_BREAK);
                            events.ScheduleEvent(EVENT_FLAME_BREAK, urand(7000, 10000));
                            break;
                        case EVENT_TERRIFYING_ROAR:
                            DoCastAOE(SPELL_TERRIFYING_ROAR);
                            events.ScheduleEvent(EVENT_TERRIFYING_ROAR, urand(10000, 15000));
                            break;
                        case EVENT_MAGMA_SPIT:
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0 , 100.0f, true))
                                DoCast(target, SPELL_MAGMA_SPIT);
                            events.ScheduleEvent(EVENT_MAGMA_SPIT, 12000);
                            break;
                        default:
                            break;
                    }
                }
                    
                DoMeleeAttackIfReady();
            }
        private:
            uint8 killCounter;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_beautyAI(creature);
        }
};

void AddSC_boss_beauty()
{
    new boss_beauty();
}
