/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

/* ScriptData
SDName: Boss_Janalai
SD%Complete: 100
SDComment:
SDCategory: Zul'Aman
EndScriptData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "zulaman.h"
#include "GridNotifiers.h"
#include "CellImpl.h"

enum eEnums
{
    SAY_AGGRO                   = 0,
    SAY_FIRE_BOMBS              = 1,
    SAY_SUMMON_HATCHER          = 2,
    SAY_ALL_EGGS                = 3,
    SAY_BERSERK                 = 4,
    SAY_SLAY                    = 5,
    SAY_DEATH                   = 6,

    // enter combat 22:16:30.299
    // Jan'alai
    SPELL_FLAME_BREATH          = 97855, // 22:16:37.553 on player SPELL_AURA_PERIODIC_DUMMY => SPELL_FLAME_BREATH_2 22:16:45.571 22:16:53.636 22:17:01.655
    SPELL_FLAME_BREATH_GROUND   = 97497, // 22:16:37.553 22:16:38.754 22:16:38.847
    SPELL_SUMON_AMANI_HATCHER_1 = 43962, // 22:16:42.373 -88.17762 Y: 1145.364 Z: 5.594031
    SPELL_SUMON_AMANI_HATCHER_2 = 45340, // 22:16:42.373  -77.75909 Y: 1156.538 Z: 5.270153
    SPELL_TELEPORT_CENTER = 43098, // -34.6677 Y: 1149.56 Z: 19.1438 22:17:25.726 22:17:55.850
    SPELL_SUMMON_ALL_PLAYERS = 43096, // 22:17:26.943
    SPELL_FIREBOMB_AURA = 42621, // 22:17:28.144
    SPELL_FIREBOMB_THROW = 42628, // 22:17:28.549 on NPC_FIRE_BOMB
    SPELL_FIREBOMB_SUMMON_5_YARDS = 42622, //  22:17:28.237 22:17:28.347  22:17:28.440 22:17:28.549
    SPELL_FIREBOMB_SUMMON_10_YARDS = 42623, // 22:17:28.643 22:17:28.752 22:17:28.830 22:17:28.955 22:17:29.033
    SPELL_FIREBOMB_SUMMON_15_YARDS = 42624, // 22:17:29.142  22:17:29.236 22:17:29.345 22:17:29.439 22:17:29.548 22:17:29.641 22:17:29.735 22:17:29.844 22:17:29.938  22:17:30.047
    SPELL_FIREBOMB_SUMMON_20_YARDS = 42625, // 22:17:30.141 22:17:30.234 22:17:30.343 22:17:30.437 30.546 30.640 30.749 30.843 30.936 31.030
    SPELL_FIREBOMB_SUMMON_25_YARDS = 42626, // 31.139 31.233 31.357 31.451  31.545 31.638 31.747 31.841 31.935 32.044
    SPELL_FIREBOMB_SUMMON_30_YARDS = 42627, // 32.137 32.231 32.340 32.449 32.543 32.637 32.746 32.839 32.933 33.042
    SPELL_HATCH_ALL             = 43144, // 22:17:37.785
    SPELL_FIRE_WALL             = 43113,
    SPELL_FRENZY                = 44779,
    SPELL_BERSERK               = 45078,

    // -- Fire Bob Spells
    SPELL_FIRE_BOMB_CHANNEL     = 42621, // last forever
    SPELL_FIRE_BOMB_THROW       = 42628, // throw visual
    SPELL_FIRE_BOMB_DUMMY       = 42629, // bomb visual
    SPELL_FIRE_BOMB_DAMAGE      = 42630,

    // -- Hatcher Spells
    SPELL_HATCH_EGG             = 42471,   // 43734
    SPELL_SUMMON_HATCHLING      = 42493,

    // -- Hatchling Spells
    SPELL_FLAME_BUFFET           = 43299
};

enum eNpcs
{
    NPC_AMANI_HATCHER           = 23818,
    NPC_HATCHLING               = 23598,
    NPC_EGG                     = 23817,
    NPC_FIRE_BOMB               = 23920,
    NPC_UNIVERSAL_TRIGGER       = 21252,
};

enum Events
{
    EVENT_FLAME_BREATH = 1,
    EVENT_SUMMON_FLAME_BREATH,
    EVENT_HATCHER,
    EVENT_FIRE_BOMB,
    EVENT_FIRE_BOMB_SUMMON,
    EVENT_FIRE_BOMB_SUMMON_STOP,
    EVENT_FIRE_BOMB_EXPLOSION,
    EVENT_BERSERK,

    EVENT_HATCHER_EGGS,
    EVENT_HATCHER_MOVE,

    EVENT_FLAME_BUFFET,
    EVENT_CHECK_WIPE,
};

float FireWallCoords[4][4] =
{
    {-10.13f, 1149.27f, 19, 3.1415f},
    {-33.93f, 1123.90f, 19, 0.5f*3.1415f},
    {-54.80f, 1150.08f, 19, 0},
    {-33.93f, 1175.68f, 19, 1.5f*3.1415f}
};

float hatcherway[2][5][3] =
{
    {
        {-87.46f, 1170.09f, 6},
        {-74.41f, 1154.75f, 6},
        {-52.74f, 1153.32f, 19},
        {-33.37f, 1172.46f, 19},
        {-33.09f, 1203.87f, 19}
    },
    {
        {-86.57f, 1132.85f, 6},
        {-73.94f, 1146.00f, 6},
        {-52.29f, 1146.51f, 19},
        {-33.57f, 1125.72f, 19},
        {-34.29f, 1095.22f, 19}
    }
};
class boss_janalai : public CreatureScript
{
    public:
        boss_janalai() : CreatureScript("boss_janalai") { }

        struct boss_janalaiAI : public BossAI
        {
            boss_janalaiAI(Creature* creature) : BossAI(creature, DATA_JANALAIEVENT) {}

            void Reset()
            {
                events.Reset();
                _Reset();

                summons.DespawnAll();

                std::list<Creature*> egg;
                me->GetCreatureListWithEntryInGrid(egg, NPC_EGG, 200.0f);
                for (std::list<Creature*>::iterator itr = egg.begin(); itr != egg.end(); ++itr)
                    (*itr)->Respawn();

                std::list<Creature*> hatchling;
                me->GetCreatureListWithEntryInGrid(hatchling, NPC_HATCHLING, 200.0f);
                for (std::list<Creature*>::iterator itr = hatchling.begin(); itr != hatchling.end(); ++itr)
                    (*itr)->DespawnOrUnsummon();
                hatchAll = false;
                frenzy = false;
            }

            void JustDied(Unit* /*killer*/)
            {
                _JustDied();
                Talk(SAY_DEATH);


                if (Creature* norkani = me->FindNearestCreature(NPC_HOSTAGE_3, 200.f, true))
                    norkani->AI()->DoAction(2);

                summons.DespawnAll();
                std::list<Creature*> units;
                GetCreatureListWithEntryInGrid(units, me, 23598, 100.0f);
                GetCreatureListWithEntryInGrid(units, me, 23818, 100.0f);
                for (auto itr = units.begin(); itr != units.end(); ++itr)
                    if (auto c = (*itr))
                    {
                        c->DeleteThreatList();
                        c->DespawnOrUnsummon();
                    }
            }

            void KilledUnit(Unit* /*victim*/)
            {
                Talk(SAY_SLAY);
            }

            void EnterCombat(Unit* /*who*/)
            {
                _EnterCombat();
                Talk(SAY_AGGRO);

                events.ScheduleEvent(EVENT_FLAME_BREATH, 7000);
                events.ScheduleEvent(EVENT_HATCHER, 12000);
                events.ScheduleEvent(EVENT_FIRE_BOMB, 55000);
                events.ScheduleEvent(EVENT_BERSERK, 600000);
                events.ScheduleEvent(EVENT_CHECK_WIPE, 5000);
            }

            void DamageTaken(Unit* /*done_by*/, uint32 &damage)
            {
                if (me->HealthBelowPct(35) && !hatchAll)
                {
                    me->CastSpell(me, SPELL_HATCH_ALL, true);
                    hatchAll = true;
                }
                else if (me->HealthBelowPct(20) && !frenzy)
                {
                    me->CastSpell(me, SPELL_FRENZY, true);
                    frenzy = true;
                }
            }

            bool checkWipe()
            {
                auto players = me->GetPlayersInRange(200.f, true);
                for (Player* player : players)
                    if (!player->HasAuraType(SPELL_AURA_FEIGN_DEATH) && !player->HasAuraType(SPELL_AURA_MOD_STEALTH) && !player->HasAuraType(SPELL_AURA_MOD_INVISIBILITY))
                        return false;
                return true;
            }

            void JustSummoned(Creature *summon)
            {
                summons.Summon(summon);
                switch (summon->GetEntry())
                {
                    case NPC_FIRE_BOMB:
                        me->CastSpell(summon, SPELL_FIREBOMB_THROW, true);
                        break;
                    case NPC_UNIVERSAL_TRIGGER:
                        summon->CastSpell(summon, SPELL_FIRE_WALL, true);
                        break;
                }

                BossAI::JustSummoned(summon);
            }


            void UpdateAI(uint32 const diff)
            {
                events.Update(diff);
                while (uint32 eventId = events.ExecuteEvent())
                    switch (eventId)
                    {

                    case EVENT_CHECK_WIPE:
                        //TC_LOG_ERROR("sql.sql", "Executing check wipe");
                        if (checkWipe())
                        {
                            //TC_LOG_ERROR("sql.sql", "Wipe recognized at glubtok.");
                            me->DeleteThreatList();
                            me->CombatStop(true);
                            Reset();
                            me->Respawn(true);
                            me->DeleteThreatList();
                        }
                        else
                            events.ScheduleEvent(EVENT_CHECK_WIPE, 5000);
                        break;
                        case EVENT_FLAME_BREATH:
                        {
                            if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0))
                                me->CastSpell(target, SPELL_FLAME_BREATH, true);
                            events.ScheduleEvent(EVENT_FLAME_BREATH, 8000);
                            break;
                        }
                        case EVENT_HATCHER:
                            Talk(SAY_SUMMON_HATCHER);
                            me->SummonCreature(NPC_AMANI_HATCHER, hatcherway[0][0][0], hatcherway[0][0][1], hatcherway[0][0][2], 0, TEMPSUMMON_CORPSE_DESPAWN, 0);
                            me->SummonCreature(NPC_AMANI_HATCHER, hatcherway[1][0][0], hatcherway[1][0][1], hatcherway[1][0][2], 0, TEMPSUMMON_CORPSE_DESPAWN, 0);
                            events.ScheduleEvent(EVENT_HATCHER, 60000);
                            break;
                        case EVENT_FIRE_BOMB:
                        {
                            for (uint8 j = 0; j < 4; j++)
                                me->SummonCreature(NPC_UNIVERSAL_TRIGGER, FireWallCoords[j][0], FireWallCoords[j][1], FireWallCoords[j][2], FireWallCoords[j][3], TEMPSUMMON_TIMED_DESPAWN, 15000);
                            Talk(SAY_FIRE_BOMBS);
                            me->AddUnitState(UNIT_STATE_ROOT);
                            me->SetReactState(REACT_PASSIVE);
                            DoCast(SPELL_TELEPORT_CENTER);
                            events.CancelEvent(EVENT_FLAME_BREATH);
                            events.ScheduleEvent(EVENT_FIRE_BOMB, 30000);
                            events.ScheduleEvent(EVENT_FIRE_BOMB_SUMMON, 1000);
                            break;
                        }
                        case EVENT_FIRE_BOMB_SUMMON:
                            me->CastSpell(me, SPELL_SUMMON_ALL_PLAYERS, true); // 5sec
                            me->CastSpell(me, SPELL_FIREBOMB_AURA, true);
                            events.ScheduleEvent(EVENT_FIRE_BOMB_SUMMON_STOP, 6000);
                            break;
                        case EVENT_FIRE_BOMB_SUMMON_STOP:
                            me->RemoveAurasDueToSpell(SPELL_FIREBOMB_AURA);
                            events.ScheduleEvent(EVENT_FIRE_BOMB_EXPLOSION, 5000);
                            me->ClearUnitState(UNIT_STATE_ROOT);
                            me->SetReactState(REACT_AGGRESSIVE);
                            UpdateVictim();
                            break;
                        case EVENT_FIRE_BOMB_EXPLOSION:
                        {
                            // Select Fire Bombs Triggers
                            std::list<Creature*> fireBomb;
                            me->GetCreatureListWithEntryInGrid(fireBomb, NPC_FIRE_BOMB, 200.0f);
                            for (std::list<Creature*>::iterator itr = fireBomb.begin(); itr != fireBomb.end(); ++itr)
                                if (Creature *firebomb = *itr)
                                {
                                    firebomb->CastSpell(firebomb, SPELL_FIRE_BOMB_DAMAGE, false);
                                    firebomb->RemoveAura(SPELL_FIRE_BOMB_DUMMY);
                                    firebomb->DespawnOrUnsummon(1000);
                                }
                            events.RescheduleEvent(EVENT_FLAME_BREATH, 8000);
                            break;
                        }
                        case EVENT_BERSERK:
                            Talk(SAY_BERSERK);
                            me->CastSpell(me, SPELL_BERSERK, true);
                            break;
                    }

                if (!me->HasUnitState(UNIT_STATE_CASTING))
                    if (UpdateVictim())
                        DoMeleeAttackIfReady();
            }

        private:
            float ownerOrientation;
            float dx, dy;
            int8 i;
            bool hatchAll, frenzy;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new boss_janalaiAI(creature);
        }
};

class mob_janalai_firebomb : public CreatureScript
{
    public:
        mob_janalai_firebomb() : CreatureScript("mob_janalai_firebomb") { }

        struct mob_janalai_firebombAI : public ScriptedAI
        {
            mob_janalai_firebombAI(Creature* creature) : ScriptedAI(creature){}

            void Reset() {}

            void SpellHit(Unit* /*caster*/, const SpellInfo* spell)
            {
                if (spell->Id == SPELL_FIRE_BOMB_THROW)
                    DoCast(me, SPELL_FIRE_BOMB_DUMMY, true);
            }

            void EnterCombat(Unit* /*who*/) {}

            void AttackStart(Unit* /*who*/) {}

            void MoveInLineOfSight(Unit* /*who*/) {}

            void UpdateAI(uint32 const /*diff*/) {}
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_janalai_firebombAI(creature);
        }
};

class mob_janalai_hatcher : public CreatureScript
{
    public:
        mob_janalai_hatcher() : CreatureScript("mob_janalai_hatcher") { }

        struct mob_janalai_hatcherAI : public ScriptedAI
        {
            mob_janalai_hatcherAI(Creature* creature) : ScriptedAI(creature)
            {
                instance = creature->GetInstanceScript();
            }

            void Reset()
            {
                side = me->GetPositionY() < 1150;
                waypoint = 0;
                isHatching = false;
                me->SetReactState(REACT_PASSIVE);
                events.Reset();
                events.ScheduleEvent(EVENT_HATCHER_MOVE, 1);
            }

            void MovementInform(uint32 type, uint32 )
            {
                if (type != POINT_MOTION_TYPE || !me->HasReactState(REACT_PASSIVE))
                    return;

                if (waypoint == 5)
                {
                    isHatching = true;
                    events.ScheduleEvent(EVENT_HATCHER_EGGS, 1);
                }
                else if (waypoint == 2)
                    events.ScheduleEvent(EVENT_HATCHER_MOVE, 3000);
                else
                    events.ScheduleEvent(EVENT_HATCHER_MOVE, 1);
            }

            void UpdateAI(uint32 const diff)
            {
                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_HATCHER_EGGS:
                        {
                            if (Creature *c = me->FindNearestCreature(NPC_EGG, 50.0f))
                                me->CastSpell(c, SPELL_HATCH_EGG, false);
                            else
                                me->SetReactState(REACT_AGGRESSIVE);
                            events.ScheduleEvent(EVENT_HATCHER_EGGS, 5000);
                            break;
                        }
                        case EVENT_HATCHER_MOVE:
                        {
                            me->GetMotionMaster()->Clear();
                            me->GetMotionMaster()->MovePoint(0, hatcherway[side][waypoint][0], hatcherway[side][waypoint][1], hatcherway[side][waypoint][2]);
                            ++waypoint;
                            break;
                        }
                    }
                }
            }

        private:
            InstanceScript* instance;
            EventMap events;
            uint32 waypoint;
            bool side;
            bool isHatching;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_janalai_hatcherAI(creature);
        }
};

class mob_janalai_hatchling : public CreatureScript
{
    public:
        mob_janalai_hatchling() : CreatureScript("mob_janalai_hatchling") { }

        struct mob_janalai_hatchlingAI : public ScriptedAI
        {
            mob_janalai_hatchlingAI(Creature* creature) : ScriptedAI(creature)
            {
                instance = creature->GetInstanceScript();
            }

            void Reset()
            {
                me->SetHover(true);
            }

            void IsSummonedBy(Unit* summoner)
            {
                if (me->GetEntry() == NPC_HATCHLING)
                {
                    if (me->GetPositionY() > 1150)
                        me->GetMotionMaster()->MovePoint(0, hatcherway[0][3][0]+rand()%4-2, 1150.0f+rand()%4-2, hatcherway[0][3][2]);
                    else
                        me->GetMotionMaster()->MovePoint(0, hatcherway[1][3][0]+rand()%4-2, 1150.0f+rand()%4-2, hatcherway[1][3][2]);
                }
            }

            void EnterCombat(Unit* /*who*/)
            {
                events.ScheduleEvent(EVENT_FLAME_BUFFET, 10000);
            }

            void UpdateAI(uint32 const diff)
            {

                if (!UpdateVictim() || me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_FLAME_BUFFET:
                            DoCastVictim(SPELL_FLAME_BUFFET);
                            events.ScheduleEvent(EVENT_FLAME_BUFFET, urand (10000, 15000));
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

        private:
            InstanceScript* instance;
            EventMap events;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_janalai_hatchlingAI(creature);
        }
};

class mob_janalai_egg : public CreatureScript
{
    public:
        mob_janalai_egg(): CreatureScript("mob_janalai_egg") {}

        struct mob_janalai_eggAI : public ScriptedAI
        {
            mob_janalai_eggAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() {}

            void UpdateAI(uint32 const /*diff*/) {}

            void SpellHit(Unit* /*caster*/, const SpellInfo* spell)
            {
                if (spell->Id == SPELL_HATCH_EGG || spell->Id == SPELL_HATCH_ALL)
                {
                    DoCast(SPELL_SUMMON_HATCHLING);
                    me->DespawnOrUnsummon();
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new mob_janalai_eggAI(creature);
        }
};

// 42621
class spell_janalai_firebomb_periodic : public SpellScriptLoader
{
public:
    spell_janalai_firebomb_periodic() : SpellScriptLoader("spell_janalai_firebomb_periodic") { }

    class spell_janalai_firebomb_periodic_AuraScript : public AuraScript
    {
        PrepareAuraScript(spell_janalai_firebomb_periodic_AuraScript);

        bool Validate(SpellInfo const* spellInfo)
        {
            return true;
        }

        void onPeriodicTick(AuraEffect const* aurEff)
        {
            if (auto caster = GetCaster())
            {
                if (!caster->isAlive())
                    return;
                if (aurEff->GetTickNumber() > 60)
                    return;

                Position flare_dest(*caster);

                if (aurEff->GetTickNumber() <= 9)
                {
                    
                    float min = float(0);
                    float current = float(aurEff->GetTickNumber()) - min;
                    float max = float(9);
                    float angle{ ((M_PI * 2.f) * ((current - min) / (max - min))) };

                    caster->MoveBlink(flare_dest, frand(2.5f, 7.5f), angle);
                }
                else if (aurEff->GetTickNumber() <= 25)
                {
                    float min = float(9);
                    float current = float(aurEff->GetTickNumber()) - min;
                    float max = float(25);
                    float angle{ ((M_PI * 2.f) * ((current - min) / (max - min))) };

                    caster->MoveBlink(flare_dest, frand(7.5f, 12.5f), angle);
                }
                else if (aurEff->GetTickNumber() <= 35)
                {
                    float min = float(25);
                    float current = float(aurEff->GetTickNumber()) - min;
                    float max = float(35);
                    float angle{ ((M_PI * 2.f) * ((current - min) / (max - min))) };

                    caster->MoveBlink(flare_dest, frand(12.f, 17.5f), angle);
                }
                else if (aurEff->GetTickNumber() <= 50)
                {
                    float min = float(35);
                    float current = float(aurEff->GetTickNumber()) - min;
                    float max = float(50);
                    float angle{ ((M_PI * 2.f) * ((current - min) / (max - min))) };

                    caster->MoveBlink(flare_dest, frand(17.5f, 23.5f), angle);
                }
                else
                {
                    flare_dest = try_tier_safespots[aurEff->GetTickNumber() - 51];
                }


                caster->CastSpell(
                    flare_dest.GetPositionX(),
                    flare_dest.GetPositionY(),
                    flare_dest.GetPositionZ(), SPELL_FIREBOMB_SUMMON_20_YARDS, true, NULL, NULL, NULL);
            }

        }

        Position const try_tier_safespots[10]
        {
            {-24.732698f, 1128.368408f, 18.711559f, 5.089436f},
            {-40.259422f, 1127.228149f, 18.711426f, 3.059191f},
            {-28.532600f, 1130.102173f, 18.711426f, 5.407526f},
            {-14.286216f, 1133.804077f, 18.711426f, 4.724236f},
            {-14.230176f, 1166.226196f, 18.736940f, 0.011814f},
            {-27.760990f, 1172.759399f, 18.709520f, 0.027522f},
            {-40.828060f, 1172.975220f, 18.709486f, 3.019896f},
            {-46.043411f, 1166.369263f, 18.708839f, 2.709663f},
            {-50.038277f, 1159.324219f, 18.707582f, 1.877141f},
            {-50.281452f, 1133.331543f, 18.707397f, 3.078801f},
        };

        void Register()
        {
            OnEffectPeriodic += AuraEffectPeriodicFn(spell_janalai_firebomb_periodic_AuraScript::onPeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
        }
    };

    AuraScript* GetAuraScript() const
    {
        return new spell_janalai_firebomb_periodic_AuraScript();
    }
};

class spell_janalai_flamebreath_periodic : public SpellScriptLoader
{
public:
    spell_janalai_flamebreath_periodic() : SpellScriptLoader("spell_janalai_flamebreath_periodic") { }

    class spell_janalai_flamebreath_periodic_AuraScript : public AuraScript
    {
        PrepareAuraScript(spell_janalai_flamebreath_periodic_AuraScript);

        bool Validate(SpellInfo const* spellInfo)
        {
            return true;
        }

        bool Load()
        {
            orientation = GetCaster()->GetOrientation();
            return true;
        }

        void onPeriodicTick(AuraEffect const* aurEff)
        {
            uint32 tick = aurEff->GetTickNumber() * 4;
            Position pos;
            GetCaster()->SetOrientation(orientation);
            GetCaster()->GetNearPosition(pos, tick, 0.0f);
            GetCaster()->CastSpell(pos.m_positionX, pos.m_positionY, pos.m_positionZ, SPELL_FLAME_BREATH_GROUND, true);
        }

        void Register()
        {
            OnEffectPeriodic += AuraEffectPeriodicFn(spell_janalai_flamebreath_periodic_AuraScript::onPeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
        }

    private:
        float orientation;
    };

    AuraScript* GetAuraScript() const
    {
        return new spell_janalai_flamebreath_periodic_AuraScript();
    }
};

class spell_janalai_summon_players : public SpellScriptLoader
{
public:
    spell_janalai_summon_players() : SpellScriptLoader("spell_janalai_summon_players") { }

    class spell_janalai_summon_players_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_janalai_summon_players_SpellScript);

        bool Validate(SpellInfo const* /*spellInfo*/)
        {
            return true;
        }

        void HandleDummy(SpellEffIndex /*effIndex*/)
        {
            if (GetCaster() && GetHitUnit())
                if (GetCaster()->GetDistance(GetHitUnit()) >= 17.0f)
                    GetHitUnit()->NearTeleportTo(GetCaster()->GetPositionX(), GetCaster()->GetPositionY(), GetCaster()->GetPositionZ(), 0.0f);
        }

        void Register()
        {
            OnEffectHitTarget += SpellEffectFn(spell_janalai_summon_players_SpellScript::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
        }
    };

    SpellScript* GetSpellScript() const
    {
        return new spell_janalai_summon_players_SpellScript();
    }
};

class npc_Amanashi_scout : public CreatureScript
{
public:
    npc_Amanashi_scout() : CreatureScript("npc_Amanashi_scout") { }

    struct npc_Amanashi_scoutAI : public ScriptedAI
    {
        npc_Amanashi_scoutAI(Creature* creature) : ScriptedAI(creature)
        {
        }

        bool CanSeeAlways(WorldObject const* obj)
        {
            if (auto p = obj->ToPlayer())
                return !(p->isGameMaster() || !p->isGMVisible());

            return false;
        }

        void InitializeAI()
        {
            yelled = false;
            yellTarget = nullptr;
            nearest_warbringer = nullptr;
            me->SetReactState(REACT_PASSIVE);
        }
        void Reset()
        {
            yelled = false;
            yellTarget = nullptr;
            nearest_warbringer = nullptr;
            me->SetReactState(REACT_PASSIVE);
        }

        void MoveInLineOfSight(Unit* who)
        {
            if (!yelled)
                if (!yellTarget)
                    if (!me->isInCombat())
                        if (me->GetDistance(who) < 20.f)
                            if (who->ToPlayer())
                                FleeForHelp(who);
        }
        void UpdateAI(const uint32 diff)
        {
            if (!yelled && nearest_warbringer)
                if (me->GetDistance(nearest_warbringer) < 5.f)
                {
                    yelled = true;
                    me->CallForHelp(20.f);
                    if (yellTarget)
                    {
                        nearest_warbringer->SetReactState(REACT_AGGRESSIVE);
                    }
                    me->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                    me->SetReactState(REACT_AGGRESSIVE);
                    if (UpdateVictim())
                    {
                        me->AI()->AttackStart(me->getVictim());
                        me->GetMotionMaster()->Clear();
                        me->StopMoving();
                        me->GetMotionMaster()->MoveChase(me->getVictim(), 0.5f);
                    }
                }

            if (yelled)
                if (me->HasReactState(REACT_AGGRESSIVE))
                    if (UpdateVictim())
                        DoMeleeAttackIfReady();
        }

        void FleeForHelp(Unit* who)
        {
            Talk(0);
            yellTarget = who;
            if (!me->isInCombat())
            {
                AttackStart(who);
                me->AddThreat(who, 10000.f);
            }

            if (nearest_warbringer = me->FindNearestCreature(23597, 150.0f))    //guardian
            {
                me->SetReactState(REACT_PASSIVE);
                if (auto m = me->GetMotionMaster())
                {
                    m->Clear();
                    me->GetMotionMaster()->MovePoint(1, nearest_warbringer->GetPositionX(), nearest_warbringer->GetPositionY(), nearest_warbringer->GetPositionZ(), true, 8.f, false);
                }
            }
            else
                if (nearest_warbringer = me->FindNearestCreature(23774, 150.0f))    //trainer
                {
                    me->SetReactState(REACT_PASSIVE);
                    if (auto m = me->GetMotionMaster())
                    {
                        m->Clear();
                        me->GetMotionMaster()->MovePoint(1, nearest_warbringer->GetPositionX(), nearest_warbringer->GetPositionY(), nearest_warbringer->GetPositionZ(), true, 8.f, false);
                    }
                }

        }
        void EnterCombat(Unit* who)
        {
            if (!yelled)
            {
                if (!yellTarget)
                {
                    FleeForHelp(who);
                }
            }
        }

    private:
        Unit* yellTarget;
        bool yelled = false;
        Creature* nearest_warbringer;
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_Amanashi_scoutAI(creature);
    }
};

void AddSC_boss_janalai()
{
    new boss_janalai();
    new mob_janalai_firebomb();
    new mob_janalai_hatcher();
    new mob_janalai_hatchling();
    new mob_janalai_egg();
    new spell_janalai_firebomb_periodic();
    new spell_janalai_flamebreath_periodic();
    new spell_janalai_summon_players();
    new npc_Amanashi_scout();
}
