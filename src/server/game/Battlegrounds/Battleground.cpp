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

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Creature.h"
#include "Formulas.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "MapManager.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"
#include "InfoMgr.h"
#include "Transport.h"
#include "Chat.h"

namespace Trinity
{
    class BattlegroundChatBuilder
    {
        public:
            BattlegroundChatBuilder(ChatMsg msgtype, int32 textId, Player const* source, va_list* args = NULL)
                : _msgtype(msgtype), _textId(textId), _source(source), _args(args) { }

            void operator()(WorldPacket& data, LocaleConstant loc_idx)
            {
                char const* text = sObjectMgr->GetTrinityString(_textId, loc_idx);
                if (_args)
                {
                    // we need copy va_list before use or original va_list will corrupted
                    va_list ap;
                    va_copy(ap, *_args);

                    char str[2048];
                    vsnprintf(str, 2048, text, ap);
                    va_end(ap);

                    do_helper(data, &str[0]);
                }
                else
                    do_helper(data, text);
            }

        private:
            void do_helper(WorldPacket& data, char const* text)
            {
                uint64 target_guid = _source ? _source->GetGUID() : 0;

                data << uint8 (_msgtype);
                data << uint32(LANG_UNIVERSAL);
                data << uint64(target_guid);                // there 0 for BG messages
                data << uint32(0);                          // can be chat msg group or something
                data << uint64(target_guid);
                data << uint32(strlen(text) + 1);
                data << text;
                data << uint8 (_source ? _source->GetChatTag() : 0);
            }

            ChatMsg _msgtype;
            int32 _textId;
            Player const* _source;
            va_list* _args;
    };

    class Battleground2ChatBuilder
    {
        public:
            Battleground2ChatBuilder(ChatMsg msgtype, int32 textId, Player const* source, int32 arg1, int32 arg2)
                : _msgtype(msgtype), _textId(textId), _source(source), _arg1(arg1), _arg2(arg2) {}

            void operator()(WorldPacket& data, LocaleConstant loc_idx)
            {
                char const* text = sObjectMgr->GetTrinityString(_textId, loc_idx);
                char const* arg1str = _arg1 ? sObjectMgr->GetTrinityString(_arg1, loc_idx) : "";
                char const* arg2str = _arg2 ? sObjectMgr->GetTrinityString(_arg2, loc_idx) : "";

                char str[2048];
                snprintf(str, 2048, text, arg1str, arg2str);

                uint64 target_guid = _source  ? _source->GetGUID() : 0;

                data << uint8 (_msgtype);
                data << uint32(LANG_UNIVERSAL);
                data << uint64(target_guid);                // there 0 for BG messages
                data << uint32(0);                          // can be chat msg group or something
                data << uint64(target_guid);
                data << uint32(strlen(str) + 1);
                data << str;
                data << uint8 (_source ? _source->GetChatTag() : uint8(0));
            }

        private:
            ChatMsg _msgtype;
            int32 _textId;
            Player const* _source;
            int32 _arg1;
            int32 _arg2;
    };
}                                                           // namespace Trinity

template<class Do>
void Battleground::BroadcastWorker(Do& _do)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(MAKE_NEW_GUID(itr->first, 0, HIGHGUID_PLAYER)))
            _do(player);
}

Battleground::Battleground()
{
    m_Guid              = 0;
    m_TypeID            = BATTLEGROUND_TYPE_NONE;
    m_RandomTypeID      = BATTLEGROUND_TYPE_NONE;
    m_InstanceID        = 0;
    m_Status            = STATUS_NONE;
    m_ClientInstanceID  = 0;
    m_EndTime           = 0;
    m_LastResurrectTime = 0;
    m_BracketId         = BG_BRACKET_ID_FIRST;
    m_InvitedAlliance   = 0;
    m_InvitedHorde      = 0;
    m_ArenaType         = 0;
    m_IsArena           = false;
    m_IsSoloQueueArena  = false;
    m_IsRatedBattleground = false;
    m_Winner            = 2;
    m_StartTime         = 0;
    m_DampeningTimer    = 60 * MINUTE * IN_MILLISECONDS;
    m_DampeningStack    = 0;
    m_CountdownTimer    = 0;
    m_ResetStatTimer    = 0;
    m_ValidStartPositionTimer = 0;
    m_Events            = 0;
    m_IsRated           = false;
    m_IsWarGame         = false;
    m_BuffChange        = false;
    m_IsRandom          = false;
    m_Name              = "";
    m_LevelMin          = 0;
    m_LevelMax          = 0;
    m_InBGFreeSlotQueue = false;
    m_SetDeleteThis     = false;

    m_MaxPlayersPerTeam = 0;
    m_MaxPlayers        = 0;
    m_MinPlayersPerTeam = 0;
    m_MinPlayers        = 0;

    m_MapId             = 0;
    m_Map               = NULL;
    m_StartMaxDist      = 0.0f;
    m_ActiveEventMask   = 0;

    m_TeamStartLocX[TEAM_ALLIANCE]   = 0;
    m_TeamStartLocX[TEAM_HORDE]      = 0;

    m_TeamStartLocY[TEAM_ALLIANCE]   = 0;
    m_TeamStartLocY[TEAM_HORDE]      = 0;

    m_TeamStartLocZ[TEAM_ALLIANCE]   = 0;
    m_TeamStartLocZ[TEAM_HORDE]      = 0;

    m_TeamStartLocO[TEAM_ALLIANCE]   = 0;
    m_TeamStartLocO[TEAM_HORDE]      = 0;

    m_ArenaTeamIds[TEAM_ALLIANCE]   = 0;
    m_ArenaTeamIds[TEAM_HORDE]      = 0;

    m_ArenaTeamRatingChanges[TEAM_ALLIANCE]   = 0;
    m_ArenaTeamRatingChanges[TEAM_HORDE]      = 0;

    m_BgRaids[TEAM_ALLIANCE]         = NULL;
    m_BgRaids[TEAM_HORDE]            = NULL;

    m_PlayersCount[TEAM_ALLIANCE]    = 0;
    m_PlayersCount[TEAM_HORDE]       = 0;

    m_TeamScores[TEAM_ALLIANCE]      = 0;
    m_TeamScores[TEAM_HORDE]         = 0;

    m_PrematureCountDown = false;

    m_HonorMode = BG_NORMAL;

    StartDelayTimes[BG_STARTING_EVENT_FIRST]  = BG_START_DELAY_2M;
    StartDelayTimes[BG_STARTING_EVENT_SECOND] = BG_START_DELAY_1M;
    StartDelayTimes[BG_STARTING_EVENT_THIRD]  = BG_START_DELAY_30S;
    StartDelayTimes[BG_STARTING_EVENT_FOURTH] = BG_START_DELAY_NONE;
    //we must set to some default existing values
    StartMessageIds[BG_STARTING_EVENT_FIRST]  = LANG_BG_WS_START_TWO_MINUTES;
    StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_WS_START_ONE_MINUTE;
    StartMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_BG_WS_START_HALF_MINUTE;
    StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_WS_HAS_BEGUN;
}

Battleground::~Battleground()
{
    // remove objects and creatures
    // (this is done automatically in mapmanager update, when the instance is reset after the reset time)
    uint32 size = uint32(BgCreatures.size());
    for (uint32 i = 0; i < size; ++i)
        DelCreature(i);

    size = uint32(BgObjects.size());
    for (uint32 i = 0; i < size; ++i)
        DelObject(i);

    sBattlegroundMgr->RemoveBattleground(GetTypeID(), GetInstanceID());
    // unload map
    if (m_Map)
    {
        m_Map->SetUnload();
        //unlink to prevent crash, always unlink all pointer reference before destruction
        m_Map->SetBG(NULL);
        m_Map = NULL;
    }
    // remove from bg free slot queue
    RemoveFromBGFreeSlotQueue();

    for (BattlegroundScoreMap::const_iterator itr = PlayerScores.begin(); itr != PlayerScores.end(); ++itr)
        delete itr->second;
}

void Battleground::Update(uint32 diff) //This is the "clock tick" of every battleground. It's fired every 50 ms
{
    if (!PreUpdateImpl(diff))
        return;

    if (!GetPlayersSize())
    {
        //BG is empty
        // if there are no players invited, delete BG
        // this will delete arena or bg object, where any player entered
        // [[   but if you use battleground object again (more battles possible to be played on 1 instance)
        //      then this condition should be removed and code:
        //      if (!GetInvitedCount(HORDE) && !GetInvitedCount(ALLIANCE))
        //          this->AddToFreeBGObjectsQueue(); // not yet implemented
        //      should be used instead of current
        // ]]
        // Battleground Template instance cannot be updated, because it would be deleted
        if (!GetInvitedCount(HORDE) && !GetInvitedCount(ALLIANCE))
            m_SetDeleteThis = true;
        return;
    }

    switch (GetStatus())
    {
        case STATUS_WAIT_JOIN:
            if (GetPlayersSize())
            {
                _ProcessJoin(diff);
                _CheckSafePositions(diff);
            }
            break;
        case STATUS_IN_PROGRESS:
            _ProcessOfflineQueue();
            // after 47 minutes without one team losing, the arena closes with no winner and no rating change
            if (isArena())
            {
                if (GetElapsedTime() >= 47 * MINUTE*IN_MILLISECONDS)
                {
                    UpdateArenaWorldState();
                    CheckArenaAfterTimerConditions();
                    return;
                }
            }
            else
            {
                _ProcessRessurect(diff);
                if (!isRatedBattleground() && sBattlegroundMgr->GetPrematureFinishTime() 
                    && (GetPlayersCountByTeam(ALLIANCE) < GetMinPlayersPerTeam() || GetPlayersCountByTeam(HORDE) < GetMinPlayersPerTeam()))
                    _ProcessProgress(diff);
                else if (m_PrematureCountDown)
                {
                    m_PrematureCountDown = false;
                    m_PrematureCountDownTimer = 0;
                    // Send battleground status opcode to inform the players that there is no premature timer anymore
                    for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
                    {
                        if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                        {
                            WorldPacket data;
                            BattlegroundQueueTypeId bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(m_TypeID, GetArenaType());
                            uint32 queueSlot = player->GetBattlegroundQueueIndex(bgQueueTypeId);
                            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, queueSlot, STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(GetTypeID()), GetElapsedTime(), GetArenaType());
                            player->GetSession()->SendPacket(&data);
                        }
                    }
                }
            }
            break;
        case STATUS_WAIT_LEAVE:
            _ProcessLeave(diff);
            break;
        default:
            break;
    }

    // Dampening
    if (GetStatus() == STATUS_IN_PROGRESS)
    if (isArena() && isRated())
    {
        if (sWorld->getBoolConfig(CONFIG_ARENA_DAMPENING_ENABLED))
        {

            auto dampBrackets = sWorld->getIntConfig(CONFIG_ARENA_DAMPENING_BRACKETS);
            bool activeBracket{ bool(dampBrackets & ArenaDampeningBitmask(GetArenaType())) };

            if (activeBracket)
                if (m_DampeningTimer <= diff)
                {

                    m_DampeningStack += 1;

                    auto players = GetPlayers();

                    if (players.size())
                    for (auto itr : players)
                        if (Player* player = ObjectAccessor::FindPlayer(itr.first))
                            if (!player->IsSpectator())
                                if (!player->isGameMaster())
                                    if (player->isAlive())
                                    {
                                        if (auto a = player->GetAura(110310))
                                            a->SetStackAmount(m_DampeningStack < 100 ? m_DampeningStack : 100);
                                        else if (auto a = player->AddAura(110310, player))
                                            a->SetStackAmount(m_DampeningStack < 100 ? m_DampeningStack : 100);
                                    }

                    m_DampeningTimer = sWorld->getIntConfig(CONFIG_ARENA_DAMPENING_INTERVAL) * IN_MILLISECONDS;
                }
                else m_DampeningTimer -= diff;
        }
    }

    // Update start time and reset stats timer
    SetElapsedTime(GetElapsedTime() + diff);
    if (GetStatus() == STATUS_WAIT_JOIN)
    {
        m_ResetStatTimer += diff;
        m_CountdownTimer += diff;
    }

    PostUpdateImpl(diff);
}

inline void Battleground::_CheckSafePositions(uint32 diff)
{
    float maxDist = GetStartMaxDist();
    if (!maxDist)
        return;

    m_ValidStartPositionTimer += diff;
    if (m_ValidStartPositionTimer >= CHECK_PLAYER_POSITION_INVERVAL)
    {
        m_ValidStartPositionTimer = 0;

        Position pos;
        float x, y, z, o;
        for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(itr->first))
            {
                player->GetPosition(&pos);
                GetTeamStartLoc(player->GetTeam(), x, y, z, o);
                if (pos.GetExactDistSq(x, y, z) > maxDist)
                if (!player->isGameMaster())
                {
                    TC_LOG_DEBUG("bg.battleground", "BATTLEGROUND: Sending %s back to start location (map: %u) (possible exploit)", player->GetName().c_str(), GetMapId());
                    player->TeleportTo(GetMapId(), x, y, z, o);
                }
            }
    }
}

inline void Battleground::_ProcessOfflineQueue()
{
    // remove offline players from bg after 5 minutes
    if (!m_OfflineQueue.empty())
    {
        BattlegroundPlayerMap::iterator itr = m_Players.find(*(m_OfflineQueue.begin()));
        if (itr != m_Players.end())
        {
            if (itr->second.OfflineRemoveTime <= sWorld->GetGameTime())
            {
                RemovePlayerAtLeave(itr->first, true, true);// remove player from BG
                m_OfflineQueue.pop_front();                 // remove from offline queue
                //do not use itr for anything, because it is erased in RemovePlayerAtLeave()
            }
        }
    }
}

inline void Battleground::_ProcessRessurect(uint32 diff)
{
    // *********************************************************
    // ***        BATTLEGROUND RESSURECTION SYSTEM           ***
    // *********************************************************
    // this should be handled by spell system
    m_LastResurrectTime += diff;
    if (m_LastResurrectTime >= getRespawnTime())
    {
        if (GetReviveQueueSize())
        {
            for (std::map<uint64, std::vector<uint64> >::iterator itr = m_ReviveQueue.begin(); itr != m_ReviveQueue.end(); ++itr)
            {
                Creature* sh = NULL;
                for (std::vector<uint64>::const_iterator itr2 = (itr->second).begin(); itr2 != (itr->second).end(); ++itr2)
                {
                    Player* player = ObjectAccessor::FindPlayer(*itr2);
                    if (!player)
                        continue;

                    if (!sh && player->IsInWorld())
                    {
                        sh = player->GetMap()->GetCreature(itr->first);
                        // only for visual effect
                        if (sh)
                            // Spirit Heal, effect 117
                            sh->CastSpell(sh, SPELL_SPIRIT_HEAL, true);
                    }

                    // Resurrection visual
                    player->CastSpell(player, SPELL_RESURRECTION_VISUAL, true);
                    m_ResurrectQueue.push_back(*itr2);
                }
                (itr->second).clear();
            }

            m_ReviveQueue.clear();
            m_LastResurrectTime = 0;
        }
        else
            // queue is clear and time passed, just update last resurrection time
            m_LastResurrectTime = 0;
    }
    else if (m_LastResurrectTime > 500)    // Resurrect players only half a second later, to see spirit heal effect on NPC
    {
        for (std::vector<uint64>::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end(); ++itr)
        {
            Player* player = ObjectAccessor::FindPlayer(*itr);
            if (!player)
                continue;
            player->ResurrectPlayer(1.0f);
            player->CastSpell(player, 6962, true);
            player->CastSpell(player, SPELL_SPIRIT_HEAL_MANA, true);
            sObjectAccessor->ConvertCorpseForPlayer(*itr);
        }
        m_ResurrectQueue.clear();
    }
}

uint32 Battleground::GetPrematureWinner()
{
    uint32 winner = 0;
    if (GetPlayersCountByTeam(ALLIANCE) >= GetMinPlayersPerTeam())
        winner = ALLIANCE;
    else if (GetPlayersCountByTeam(HORDE) >= GetMinPlayersPerTeam())
        winner = HORDE;

    return winner;
}

inline void Battleground::_ProcessProgress(uint32 diff)
{
    // *********************************************************
    // ***           BATTLEGROUND BALLANCE SYSTEM            ***
    // *********************************************************
    // if less then minimum players are in on one side, then start premature finish timer
    if (sBattlegroundMgr->isTesting())
        return;

    if (!m_PrematureCountDown)
    {
        m_PrematureCountDown = true;
        m_PrematureCountDownTimer = isRatedBattleground() ? MINUTE * IN_MILLISECONDS : sBattlegroundMgr->GetPrematureFinishTime();

        // Send battleground status opcode to inform the players about the premature cooldown
        for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(itr->first))
            {
                WorldPacket data;
                BattlegroundQueueTypeId bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(m_TypeID, GetArenaType());
                uint32 queueSlot = player->GetBattlegroundQueueIndex(bgQueueTypeId);
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, queueSlot, STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(GetTypeID()), GetElapsedTime(), GetArenaType());
                player->GetSession()->SendPacket(&data);
            }
        }
    }
    else if (m_PrematureCountDownTimer < diff || GetPlayersCountByTeam(ALLIANCE) == 0 || GetPlayersCountByTeam(HORDE) == 0)
    {
        // time's up!
        EndBattleground(isRatedBattleground() ? WINNER_NONE : GetPrematureWinner());
        m_PrematureCountDown = false;
    }
    else
        m_PrematureCountDownTimer -= diff;
}

inline void Battleground::_ProcessJoin(uint32 diff)
{
    // *********************************************************
    // ***           BATTLEGROUND STARTING SYSTEM            ***
    // *********************************************************
    ModifyStartDelayTime(diff);

    if (!isArena())
        SetRemainingTime(300000);

    if (m_ResetStatTimer > 5000)
    {
        m_ResetStatTimer = 0;
        for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(itr->first))
            {
                player->ResetAllPowers();
                if (Pet* pet = player->GetPet())
                    pet->ResetAllPowers();
            }
    }

    // Send packet every 10 seconds until the 2nd field reach 0
    if (m_CountdownTimer >= 10000 && !(isArena() && GetElapsedTime() < 45 * IN_MILLISECONDS))
    {
        uint32 countdownMaxForBGType = isArena() ? ARENA_COUNTDOWN_MAX : BATTLEGROUND_COUNTDOWN_MAX;

        WorldPacket data(SMSG_START_TIMER, 4+4+4);
        data << uint32(0); // unk
        data << uint32(countdownMaxForBGType - (GetElapsedTime() / 1000));
        data << uint32(countdownMaxForBGType);

        for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                player->GetSession()->SendPacket(&data);

        m_CountdownTimer = 0;
    }

    if (!(m_Events & BG_STARTING_EVENT_1))
    {
        m_Events |= BG_STARTING_EVENT_1;

        if (!FindBgMap())
        {
            TC_LOG_ERROR("bg.battleground", "Battleground::_ProcessJoin: map (map id: %u, instance id: %u) is not created!", m_MapId, m_InstanceID);
            EndNow();
            return;
        }

        // Setup here, only when at least one player has ported to the map
        if (!SetupBattleground())
        {
            EndNow();
            return;
        }

        StartingEventCloseDoors();
        SetStartDelayTime(StartDelayTimes[BG_STARTING_EVENT_FIRST]);
        // First start warning - 2 or 1 minute
        SendMessageToAll(StartMessageIds[BG_STARTING_EVENT_FIRST], CHAT_MSG_BG_SYSTEM_NEUTRAL);
    }
    // After 1 minute or 30 seconds, warning is signaled
    else if (GetStartDelayTime() <= StartDelayTimes[BG_STARTING_EVENT_SECOND] && !(m_Events & BG_STARTING_EVENT_2))
    {
        m_Events |= BG_STARTING_EVENT_2;
        SendMessageToAll(StartMessageIds[BG_STARTING_EVENT_SECOND], CHAT_MSG_BG_SYSTEM_NEUTRAL);
    }
    // After 30 or 15 seconds, warning is signaled
    else if (GetStartDelayTime() <= StartDelayTimes[BG_STARTING_EVENT_THIRD] && !(m_Events & BG_STARTING_EVENT_3))
    {
        m_Events |= BG_STARTING_EVENT_3;
        SendMessageToAll(StartMessageIds[BG_STARTING_EVENT_THIRD], CHAT_MSG_BG_SYSTEM_NEUTRAL);
    }
    // Delay expired (after 2 or 1 minute)
    else if (GetStartDelayTime() <= 0 && !(m_Events & BG_STARTING_EVENT_4))
    {
        m_Events |= BG_STARTING_EVENT_4;

        StartingEventOpenDoors();
        m_DampeningTimer = sWorld->getIntConfig(CONFIG_ARENA_DAMPENING_STARTTIME) * MINUTE * IN_MILLISECONDS;
        SendWarningToAll(StartMessageIds[BG_STARTING_EVENT_FOURTH]);
        SetStatus(STATUS_IN_PROGRESS);
        SetStartDelayTime(StartDelayTimes[BG_STARTING_EVENT_FOURTH]);

        if (isArena() && isRated())
        {
            SlotStoreType Slot;
            uint32 MMR1 = GetArenaMatchmakerRatingByIndex(0);
            uint32 MMR2 = GetArenaMatchmakerRatingByIndex(1);
            switch (GetArenaType())
            {
                case ARENA_TYPE_2v2:
                    Slot = SLOT_2VS2;
                    break;
                case ARENA_TYPE_3v3:
                    Slot = SLOT_3VS3;
                    break;
                case ARENA_TYPE_3v3_SOLO:
                    Slot = SLOT_SOLO_QUEUE;
                    break;
            }
            sInfoMgr->AddBGInfo(Slot, GetInstanceID(), GetArenaTeamIdByIndex(0), GetArenaTeamIdByIndex(1), MMR1, MMR2, GetTypeID());
        }

        // Remove preparation
        if (isArena())
        {
            // TODO : add arena sound PlaySoundToAll(SOUND_ARENA_START);
            for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
                if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                {
                    // BG Status packet
                    WorldPacket status;
                    BattlegroundQueueTypeId bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(m_TypeID, GetArenaType());
                    uint32 queueSlot = player->GetBattlegroundQueueIndex(bgQueueTypeId);
                    sBattlegroundMgr->BuildBattlegroundStatusPacket(&status, this, player, queueSlot, STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(m_TypeID), GetElapsedTime(), GetArenaType());
                    player->GetSession()->SendPacket(&status);

                    // Correctly display EnemyUnitFrame
                    player->SetByteValue(PLAYER_BYTES_3, 3, player->GetTeam());

                    player->RemoveAurasDueToSpell(SPELL_ARENA_PREPARATION);
                    player->ResetAllPowers();
                    player->SetCommandStatusOff(CHEAT_CASTTIME);
                    if (!player->isGameMaster())
                    {
                        // remove auras with duration lower than 30s
                        Unit::AuraApplicationMap & auraMap = player->GetAppliedAuras();
                        for (auto iter = auraMap.begin(); iter != auraMap.end();)
                        {
                            AuraApplication * aurApp = iter->second;
                            Aura* aura = aurApp->GetBase();
                            if (!aura->IsPermanent()
                                && aura->GetDuration() <= 30*IN_MILLISECONDS
                                && aurApp->IsPositive()
                                && (!(aura->GetSpellInfo()->Attributes & SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
                                && (!aura->HasEffectType(SPELL_AURA_MOD_INVISIBILITY)))
                                player->RemoveAura(iter);
                            else
                                ++iter;
                        }

                        if (Pet* pet = player->GetPet())
                        {
                            // remove auras with duration lower than 30s
                            Unit::AuraApplicationMap & auraMap = pet->GetAppliedAuras();
                            for (auto iter = auraMap.begin(); iter != auraMap.end();)
                            {
                                AuraApplication * aurApp = iter->second;
                                Aura* aura = aurApp->GetBase();
                                if (!aura->IsPermanent()
                                    && aura->GetDuration() <= 30*IN_MILLISECONDS
                                    && aurApp->IsPositive()
                                    && (!(aura->GetSpellInfo()->Attributes & SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
                                    && (!aura->HasEffectType(SPELL_AURA_MOD_INVISIBILITY)))
                                    pet->RemoveAura(iter);
                                else
                                    ++iter;
                            }
                        }
                    }
                }
                if (!GetAlivePlayersCountByTeam(ALLIANCE) && GetPlayersCountByTeam(HORDE))
                    EndBattleground(WINNER_NOT_JOINED);
                else if (GetPlayersCountByTeam(ALLIANCE) && !GetAlivePlayersCountByTeam(HORDE))
                    EndBattleground(WINNER_NOT_JOINED);
                else if (isSoloQueueArena() && (GetPlayersCountByTeam(ALLIANCE) < 3 || GetPlayersCountByTeam(HORDE) < 3))
                {
                    for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
                        if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                            ChatHandler(player->GetSession()).PSendSysMessage("The fight was interrupted! Not all players have entered the arena...");
                    EndSoloBattleground(WINNER_NOT_JOINED);
                }
        }
        else
        {
            PlaySoundToAll(SOUND_BG_START);

            for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
                if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                {
                    player->RemoveAurasDueToSpell(SPELL_PREPARATION);
                    player->ResetAllPowers();
                }
            // Announce BG starting
            if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ENABLE))
                sWorld->SendWorldText(LANG_BG_STARTED_ANNOUNCE_WORLD, GetName(), GetMinLevel(), GetMaxLevel());
        }
    }

    if (GetRemainingTime() > 0 && (m_EndTime -= diff) > 0)
        SetRemainingTime(GetRemainingTime() - diff);
}

inline void Battleground::_ProcessLeave(uint32 diff)
{
    // *********************************************************
    // ***           BATTLEGROUND ENDING SYSTEM              ***
    // *********************************************************
    // remove all players from battleground after 2 minutes
    SetRemainingTime(GetRemainingTime() - diff);
    if (GetRemainingTime() <= 0)
    {
        SetRemainingTime(0);
        BattlegroundPlayerMap::iterator itr, next;
        for (itr = m_Players.begin(); itr != m_Players.end(); itr = next)
        {
            next = itr;
            ++next;
            //itr is erased here!
            RemovePlayerAtLeave(itr->first, true, true);// remove player from BG
            // do not change any battleground's private variables
        }
    }
}

inline Player* Battleground::_GetPlayer(uint64 guid, bool offlineRemove, char const* context) const
{
    Player* player = NULL;
    if (!offlineRemove)
    {
        player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            TC_LOG_ERROR("bg.battleground", "Battleground::%s: player (GUID: %u) not found for BG (map: %u, instance id: %u)!",
                context, GUID_LOPART(guid), m_MapId, m_InstanceID);
    }
    return player;
}

inline Player* Battleground::_GetPlayer(BattlegroundPlayerMap::iterator itr, char const* context)
{
    return _GetPlayer(itr->first, itr->second.OfflineRemoveTime, context);
}

inline Player* Battleground::_GetPlayer(BattlegroundPlayerMap::const_iterator itr, char const* context) const
{
    return _GetPlayer(itr->first, itr->second.OfflineRemoveTime, context);
}

inline Player* Battleground::_GetPlayerForTeam(uint32 teamId, BattlegroundPlayerMap::const_iterator itr, char const* context) const
{
    Player* player = _GetPlayer(itr, context);
    if (player)
    {
        uint32 team = itr->second.Team;
        if (!team)
            team = player->GetTeam();
        if (team != teamId)
            player = NULL;
    }
    return player;
}

void Battleground::SetTeamStartLoc(uint32 TeamID, float X, float Y, float Z, float O)
{
    TeamId idx = GetTeamIndexByTeamId(TeamID);
    m_TeamStartLocX[idx] = X;
    m_TeamStartLocY[idx] = Y;
    m_TeamStartLocZ[idx] = Z;
    m_TeamStartLocO[idx] = O;
}

void Battleground::SendPacketToAll(WorldPacket* packet)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayer(itr, "SendPacketToAll"))
            player->GetSession()->SendPacket(packet);
}

void Battleground::SendPacketToTeam(uint32 TeamID, WorldPacket* packet, Player* sender, bool self)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayerForTeam(TeamID, itr, "SendPacketToTeam"))
            if (self || sender != player)
            {
                WorldSession* session = player->GetSession();
                TC_LOG_DEBUG("bg.battleground", "%s %s - SendPacketToTeam %u, Player: %s", GetOpcodeNameForLogging(packet->GetOpcode()).c_str(),
                    session->GetPlayerInfo().c_str(), TeamID, sender ? sender->GetName().c_str() : "null");
                session->SendPacket(packet);
            }
}

void Battleground::PlaySoundToAll(uint32 SoundID)
{
    WorldPacket data;
    sBattlegroundMgr->BuildPlaySoundPacket(&data, SoundID);
    SendPacketToAll(&data);
}

void Battleground::PlaySoundToTeam(uint32 SoundID, uint32 TeamID)
{
    WorldPacket data;
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayerForTeam(TeamID, itr, "PlaySoundToTeam"))
        {
            sBattlegroundMgr->BuildPlaySoundPacket(&data, SoundID);
            player->GetSession()->SendPacket(&data);
        }
}

void Battleground::CastSpellOnTeam(uint32 SpellID, uint32 TeamID)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayerForTeam(TeamID, itr, "CastSpellOnTeam"))
            player->CastSpell(player, SpellID, true);
}

void Battleground::RemoveAuraOnTeam(uint32 SpellID, uint32 TeamID)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayerForTeam(TeamID, itr, "RemoveAuraOnTeam"))
            player->RemoveAura(SpellID);
}

void Battleground::YellToAll(Creature* creature, char const* text, uint32 language)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayer(itr, "YellToAll"))
        {
            WorldPacket data(SMSG_MESSAGECHAT, 200);
            creature->BuildMonsterChat(&data, CHAT_MSG_MONSTER_YELL, text, language, creature->GetName(), itr->first);
            player->GetSession()->SendPacket(&data);
        }
}

void Battleground::DistributeHonorToAll(uint32 winner)
{
    if (isWarGame())
        return;

    RewardEndGameHonorToTeam((winner == ALLIANCE ? true : false), ALLIANCE);
    RewardEndGameHonorToTeam((winner == HORDE ? true : false), HORDE);
}

void Battleground::RewardEndGameHonorToTeam(bool winner, uint32 TeamID)
{
    double base_honor_multiplier = 1.0;
    switch (GetMapId())
    {
    case 726:   //twin peaks
        base_honor_multiplier = 1.0;
        break;

    case 489:   //warsong gulch
        base_honor_multiplier = 1.0;
        break;

    case 761:   //battle for gilneas
        base_honor_multiplier = 1.3;
        break;

    case 529:   //arathi basin
        base_honor_multiplier = 1.3;
        break;

    case 566:   //eye of the storm
        base_honor_multiplier = 1.3;
        break;

    case 607:   //strand of the ancients
        base_honor_multiplier = 1.5;
        break;

    case 628:   //Isle of conquest
        base_honor_multiplier = 2.0;
        break;

    case 30:    //alterac valley
        base_honor_multiplier = 2.0;
        break;

    default:
        break;
    }
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayerForTeam(TeamID, itr, "RewardHonorToTeam"))
        {
            if (winner)
                UpdatePlayerScore(player, SCORE_BONUS_HONOR, BG_REWARD_WINNER_HONOR_STANDARD * base_honor_multiplier);
            else
                UpdatePlayerScore(player, SCORE_BONUS_HONOR, BG_REWARD_LOSER_HONOR_STANDARD * base_honor_multiplier);
        }
}

void Battleground::RewardHonorToTeam(uint32 Honor, uint32 TeamID)
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = _GetPlayerForTeam(TeamID, itr, "RewardHonorToTeam"))
            UpdatePlayerScore(player, SCORE_BONUS_HONOR, Honor);
}

void Battleground::RewardReputationToTeam(uint32 a_faction_id, uint32 h_faction_id, uint32 Reputation, uint32 teamId)
{
    FactionEntry const* a_factionEntry = sFactionStore.LookupEntry(a_faction_id);
    FactionEntry const* h_factionEntry = sFactionStore.LookupEntry(h_faction_id);

    if (!a_factionEntry || !h_factionEntry)
        return;

    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
    {
        if (itr->second.OfflineRemoveTime)
            continue;

        Player* plr = ObjectAccessor::FindPlayer(itr->first);

        if (!plr)
        {
            TC_LOG_ERROR("misc", "BattleGround:RewardReputationToTeam: %u not found!", GUID_LOPART(itr->first));
            continue;
        }

        uint32 team = plr->GetTeam();

        if (team == teamId)
            plr->GetReputationMgr().ModifyReputation(plr->GetOTeam() == ALLIANCE ? a_factionEntry : h_factionEntry, Reputation);
    }
}

void Battleground::CompleteAchievementToTeam(uint32 AchievementId, uint32 TeamID)
{
    AchievementEntry const* pAE = sAchievementStore.LookupEntry(AchievementId);
    if (pAE)
        for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
            if (Player* player = _GetPlayerForTeam(TeamID, itr, "CompleteAchievementToTeam"))
                player->CompletedAchievement(pAE);
}

void Battleground::UpdateWorldState(uint32 Field, uint32 Value)
{
    WorldPacket data;
    sBattlegroundMgr->BuildUpdateWorldStatePacket(&data, Field, Value);
    SendPacketToAll(&data);
}

void Battleground::UpdateWorldStateForPlayer(uint32 Field, uint32 Value, Player* Source)
{
    WorldPacket data;
    sBattlegroundMgr->BuildUpdateWorldStatePacket(&data, Field, Value);
    Source->GetSession()->SendPacket(&data);
}

void Battleground::AddEmptyPlayerScoresForGUID(uint64 guid, uint32 team)
{
    auto score_base = PlayerScores.find(guid);
    if (auto score = score_base->second)
    {

    }
    else
    {
        BattlegroundScore* sc = new BattlegroundScore;
        sc->BgTeam = team;
        sc->DamageDone = 0;
        sc->Deaths = 0;
        sc->HealingDone = 0;
        sc->KillingBlows = 0;
        sc->HonorableKills = 0;
        sc->HealingTaken = 0;
        sc->DamageTaken = 0;

        if (auto player = ObjectAccessor::FindPlayer(guid)) // Get the rating if player is online.
            sc->TalentTree = player->GetPrimaryTalentTree(player->GetActiveSpec());
        else
            sc->TalentTree = 0;

        PlayerScores[guid] = sc;
    }
}

void Battleground::EndBattleground(uint32 winner)
{
    if (isSoloQueueArena())
    {
        EndSoloBattleground(winner);
        return;
    }

    RemoveFromBGFreeSlotQueue();

    ArenaTeam* winnerArenaTeam = NULL;
    ArenaTeam* loserArenaTeam = NULL;

    uint32 loserTeamRating = 0;
    uint32 loserMatchmakerRating = 0;
    int32  loserChange = 0;
    int32  loserMatchmakerChange = 0;
    uint32 winnerTeamRating = 0;
    uint32 winnerMatchmakerRating = 0;
    int32  winnerChange = 0;
    int32  winnerMatchmakerChange = 0;

    int32 winmsg_id = 0;

    if (winner == ALLIANCE)
    {
        winmsg_id = isBattleground() ? LANG_BG_A_WINS : LANG_ARENA_GOLD_WINS;
        PlaySoundToAll(SOUND_ALLIANCE_WINS);                // alliance wins sound
        SetWinner(WINNER_ALLIANCE);
    }
    else if (winner == HORDE)
    {
        winmsg_id = isBattleground() ? LANG_BG_H_WINS : LANG_ARENA_GREEN_WINS;
        PlaySoundToAll(SOUND_HORDE_WINS);                   // horde wins sound
        SetWinner(WINNER_HORDE);
    }
    else
    {
        SetWinner(WINNER_NONE);
    }

    PreparedStatement* stmt = nullptr;
    uint64 battlegroundId = 1;
    if (isBattleground() && sWorld->getBoolConfig(CONFIG_BATTLEGROUND_STORE_STATISTICS_ENABLE))
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PVPSTATS_MAXID);
        PreparedQueryResult result = CharacterDatabase.Query(stmt);

        if (result)
        {
            Field* fields = result->Fetch();
            battlegroundId = fields[0].GetUInt64() + 1;
        }

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PVPSTATS_BATTLEGROUND);
        stmt->setUInt64(0, battlegroundId);
        stmt->setUInt8(1, GetWinner());
        stmt->setUInt8(2, GetUniqueBracketId());
        stmt->setUInt8(3, GetTypeID(true));
        CharacterDatabase.Execute(stmt);
    }

    SetStatus(STATUS_WAIT_LEAVE);
    //we must set it this way, because end time is sent in packet!
    SetRemainingTime(TIME_AUTOCLOSE_BATTLEGROUND);

    // arena rating calculation
    if (isArena() && isRated())
    {
        winnerArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(winner));
        loserArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(GetOtherTeam(winner)));

        if (winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
        {
            if (winner != WINNER_NONE && winner != WINNER_NOT_JOINED)
            {
                loserTeamRating             = loserArenaTeam->GetRating();
                loserMatchmakerRating       = GetArenaMatchmakerRating(GetOtherTeam(winner));
                winnerTeamRating            = winnerArenaTeam->GetRating();
                winnerMatchmakerRating      = GetArenaMatchmakerRating(winner);
                winnerMatchmakerChange      = winnerArenaTeam->WonAgainst(winnerMatchmakerRating, loserMatchmakerRating, winnerChange);
                loserMatchmakerChange       = loserArenaTeam->LostAgainst(loserMatchmakerRating, winnerMatchmakerRating, loserChange);

                SetArenaMatchmakerRating(winner, winnerMatchmakerRating + winnerMatchmakerChange);
                SetArenaMatchmakerRating(GetOtherTeam(winner), loserMatchmakerRating + loserMatchmakerChange);
                SetArenaTeamRatingChangeForTeam(winner, winnerChange);
                SetArenaTeamRatingChangeForTeam(GetOtherTeam(winner), loserChange);

                
                /*
                    BEGINE DB LOGGING
                */

                        auto log_winner_TeamRating      { uint32(winnerArenaTeam->GetRating())                                                                  };
                        auto log_loser_TeamRating       { uint32(loserArenaTeam->GetRating())                                                                   };
                        auto log_loser_AVG_mmr          { uint32(winnerArenaTeam->GetAverageMMR(GetBgRaid(winner == ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE)))    };
                        auto log_winner_AVG_mmr         { uint32(winnerArenaTeam->GetAverageMMR(GetBgRaid(winner == HORDE ? TEAM_HORDE : TEAM_ALLIANCE)))       };
                if (sWorld->getBoolConfig(CONFIG_ARENA_LOG_EXTENDED_INFO))
                {
                    uint8 query_index = 0;
                    if (auto stmt                       { CharacterDatabase.GetPreparedStatement(CHAR_INS_ARENA_LOG)                                            })
                    if (auto log_mapID                  { uint32(GetMapId())                                                                                    })
                    if (auto log_elapsed_time           { uint32(GetElapsedTime())                                                                              })
                    if (auto log_winner_team_id         { uint32(winnerArenaTeam->GetId())                                                                      })
                    if (auto log_loser_team_id          { uint32(loserArenaTeam->GetId())                                                                       })
                    {
                        /*
                            Generic match data
                        */
                        stmt->setUInt32(0, log_mapID); 
                        stmt->setUInt32(1, log_elapsed_time);//arg "1" is listed as "4" in db columns
                        stmt->setUInt32(2, log_winner_team_id);
                        stmt->setUInt32(3, log_loser_team_id);
                        stmt->setInt32(4, winnerChange);
                        stmt->setUInt32(5, log_winner_TeamRating);
                        stmt->setUInt32(6, log_winner_AVG_mmr);
                        stmt->setInt32(7, loserChange);
                        stmt->setUInt32(8, log_loser_TeamRating);
                        stmt->setUInt32(9, log_loser_AVG_mmr);

                        /*
                            winner specific data
                        */
                        std::ostringstream ostream;
                        const std::string did_not_queue{ "DID_NOT_QUEUE" };
                        const std::string abandoned{ "ABANDONED_OFFLINE"};

                            query_index = 0;
                            auto winnersList = winnerArenaTeam->GetMembers();

                            if (winnersList.size())
                            for (auto itr : winnersList)
                            {
                                if (query_index < 5)
                                    if (auto player_guid = itr.Guid)
                                    {

                                        auto score_base = PlayerScores.find(player_guid);
                                        if (score_base != PlayerScores.end())
                                        {
                                            auto score = score_base->second;
                                            stmt->setUInt64(query_index + 10, player_guid);
                                            if (auto player = ObjectAccessor::FindPlayer(player_guid))
                                            {
                                                if (auto session = player->GetSession())
                                                {
                                                    std::stringstream ip_mask;
                                                    ip_mask << session->GetMaskedIP();
                                                    std::string ip_to_db{ ip_mask.str() };
                                                    stmt->setString(query_index + 15, ip_to_db);
                                                }
                                                else
                                                {
                                                    stmt->setString(query_index + 15, abandoned);
                                                }

                                                }
                                                else
                                                    stmt->setString(query_index + 15,           abandoned);
                                                    stmt->setUInt32(query_index + 20,           score->GetDamageDone());
                                                    stmt->setUInt32(query_index + 25,           score->GetHealingDone());
                                                    stmt->setUInt32(query_index + 30,           score->GetDamageTaken());
                                                    stmt->setUInt32(query_index + 35,           score->GetHealingTaken());
                                                    stmt->setUInt32(query_index + 40,           score->GetKillingBlows());
                                                    stmt->setUInt32(query_index + 45,           score->GetDeaths());
                                                    stmt->setInt32(query_index  + 50,           GetArenaTeamRatingChangeByIndex(winner == HORDE ? TEAM_HORDE : TEAM_ALLIANCE));
                                                    stmt->setInt32(query_index  + 55,           winnerMatchmakerChange);
                                        }
                                        else
                                        {
                                            stmt->setUInt64(query_index + 10, player_guid);
                                            stmt->setString(query_index + 15, did_not_queue);
                                        }

                                    }
                                query_index++;
                            }

                        /*
                            loser specific data
                        */
                            query_index = 0;
                            auto losersList = loserArenaTeam->GetMembers();

                        if (losersList.size())
                            for (auto itr : losersList)
                            {
                                if (query_index < 5)
                                    if (auto player_guid = itr.Guid)
                                    {
                                        auto score_base = PlayerScores.find(player_guid);
                                        if (score_base != PlayerScores.end())
                                        {
                                            auto score = score_base->second;
                                            stmt->setUInt64(query_index + 60, player_guid);//+12
                                            if (auto player = ObjectAccessor::FindPlayer(player_guid))
                                            {
                                                if (auto session = player->GetSession())
                                                {
                                                    std::stringstream ip_mask;
                                                    ip_mask << session->GetMaskedIP();
                                                    std::string ip_to_db{ ip_mask.str() };
                                                    stmt->setString(query_index + 65, ip_to_db);
                                                }
                                            }
                                                else
                                                    stmt->setString(query_index + 53, abandoned);

                                                    stmt->setUInt32(query_index + 70,           score->GetDamageDone());
                                                    stmt->setUInt32(query_index + 75,           score->GetHealingDone());
                                                    stmt->setUInt32(query_index + 80,           score->GetDamageTaken());
                                                    stmt->setUInt32(query_index + 85,           score->GetHealingTaken());
                                                    stmt->setUInt32(query_index + 90,           score->GetKillingBlows());
                                                    stmt->setUInt32(query_index + 95,           score->GetDeaths());
                                                    stmt->setInt32(query_index +  100,          GetArenaTeamRatingChangeByIndex(winner == ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE));
                                                    stmt->setInt32(query_index  + 105,          loserMatchmakerChange);
                                        }
                                        else
                                        {
                                            stmt->setUInt64(query_index + 60, player_guid);
                                            stmt->setString(query_index + 65, did_not_queue);
                                        }

                                    }
                                query_index++;
                            }


                        stmt->setUInt32(110, winner == ALLIANCE ? log_winner_team_id : log_loser_team_id);//team_yellow
                        stmt->setUInt32(111, winner == HORDE ? log_winner_team_id : log_loser_team_id);//team_green
                            CharacterDatabase.Execute(stmt);
                        }
                    else TC_LOG_ERROR("slq.sql", "CHAR_INS_PVPSTATS_BATTLEGROUND failed: else log_loser_team_id");
                    else TC_LOG_ERROR("slq.sql", "CHAR_INS_PVPSTATS_BATTLEGROUND failed: else log_winner_team_id");
                    else TC_LOG_ERROR("slq.sql", "CHAR_INS_PVPSTATS_BATTLEGROUND failed: else log_mapID");
                    else TC_LOG_ERROR("slq.sql", "CHAR_INS_PVPSTATS_BATTLEGROUND failed: else stmt");
                }
            }
            else if (winner == WINNER_NOT_JOINED)
            {
                SetArenaTeamRatingChangeForTeam(ALLIANCE, 0);
                SetArenaTeamRatingChangeForTeam(HORDE, 0);
            }
            // Deduct 16 points from each teams arena-rating if there are no winners after 45+2 minutes
            else
            {
                SetArenaTeamRatingChangeForTeam(ALLIANCE, ARENA_TIMELIMIT_POINTS_LOSS);
                SetArenaTeamRatingChangeForTeam(HORDE, ARENA_TIMELIMIT_POINTS_LOSS);
                winnerArenaTeam->FinishGame(ARENA_TIMELIMIT_POINTS_LOSS, true);
                loserArenaTeam->FinishGame(ARENA_TIMELIMIT_POINTS_LOSS, true);
            }
        }
        else
        {
            SetArenaTeamRatingChangeForTeam(ALLIANCE, 0);
            SetArenaTeamRatingChangeForTeam(HORDE, 0);
        }
    }

    if (isArena() && isRated())
    {
        SlotStoreType Slot = SLOT_2VS2;
        switch (GetArenaType())
        {
        case ARENA_TYPE_2v2:
            Slot = SLOT_2VS2;
            break;
        case ARENA_TYPE_3v3:
            Slot = SLOT_3VS3;
            break;
        /*case ARENA_TYPE_3v3_SOLO:     // This dont need to be here, handled in Battleground::EndSoloBattleground
            Slot = SLOT_SOLO_QUEUE;
            break;*/
        }
        sInfoMgr->RemoveBGInfo(Slot, m_InstanceID);
    }

    bool guildAwarded = false;
    WorldPacket pvpLogData;
    sBattlegroundMgr->BuildPvpLogDataPacket(&pvpLogData, this);

    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(GetTypeID(), GetArenaType());

    uint8 aliveWinners = GetAlivePlayersCountByTeam(winner);
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
    {
        uint32 team = itr->second.Team;

        if (itr->second.OfflineRemoveTime)
        {
            //if rated arena match - make member lost!
            if (isArena() && isRated() && winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
            {
                if (team == winner)
                    winnerArenaTeam->OfflineMemberLost(itr->first, loserMatchmakerRating, winnerMatchmakerChange);
                else if (winner != WINNER_NOT_JOINED)
                    loserArenaTeam->OfflineMemberLost(itr->first, winnerMatchmakerRating, loserMatchmakerChange);
            }
            continue;
        }

        Player* player = _GetPlayer(itr, "EndBattleground");
        if (!player)
            continue;

        // should remove spirit of redemption
        if (player->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
            player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

        // Last Man Standing - Rated 3v3 or 5v5 arena & be solely alive player
        if (team == winner && isArena() && isRated() && (GetArenaType() == ARENA_TYPE_3v3 || GetArenaType() == ARENA_TYPE_5v5) && aliveWinners == 1 && player->isAlive())
            player->CastSpell(player, SPELL_THE_LAST_STANDING, true);

        if (!player->isAlive())
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }
        else
        {
            //needed cause else in av some creatures will kill the players at the end
            player->CombatStop();
            player->getHostileRefManager().deleteReferences();
        }

        // per player calculation
        if (isArena() && isRated() && winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
        {
            if (team == winner)
            {
                // update achievement BEFORE personal rating update
                uint32 rating = player->GetArenaPersonalRating(winnerArenaTeam->GetSlot());
                player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA, rating ? rating : 1);
                player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA, GetMapId());
                player->ModifyCurrency(CURRENCY_TYPE_CONQUEST_META_ARENA, sWorld->getIntConfig(CONFIG_CURRENCY_CONQUEST_POINTS_ARENA_REWARD));

                winnerArenaTeam->MemberWon(player, loserMatchmakerRating, winnerMatchmakerChange);

                if (Guild *guild = player->GetGuild())
                    if (Group *group = GetBgRaid(team))
                        if (group->IsGuildGroupFor(player))
                        {
                            guild->GiveXP(sWorld->getIntConfig(CONFIG_GUILD_XP_REWARD_ARENA), player);
                            uint32 guildRep = std::max(uint32(1), uint32(sWorld->getIntConfig(CONFIG_GUILD_XP_REWARD_ARENA)/450));
                            guild->GiveReputation(guildRep, player);
                        }
            }
            else if (winner != WINNER_NOT_JOINED)
            {
                if (winnerArenaTeam->IsMember(player->GetGUID()))
                    winnerArenaTeam->MemberLost(player, winnerMatchmakerRating, loserMatchmakerChange);
                else if (loserArenaTeam->IsMember(player->GetGUID()))
                    loserArenaTeam->MemberLost(player, winnerMatchmakerRating, loserMatchmakerChange);

                // Arena lost => reset the win_rated_arena having the "no_lose" condition
                player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA, ACHIEVEMENT_CRITERIA_CONDITION_NO_LOSE);
            }
        }

        uint32 winner_honor = player->IsRandomDailyBgRewarded() ? BG_REWARD_WINNER_HONOR_LAST : BG_REWARD_WINNER_HONOR_FIRST;
        uint32 loser_honor = player->IsRandomDailyBgRewarded() ? BG_REWARD_LOSER_HONOR_LAST : BG_REWARD_LOSER_HONOR_FIRST;

        // remove temporary currency bonus auras before rewarding player
        player->RemoveAura(SPELL_HONORABLE_DEFENDER_25Y);
        player->RemoveAura(SPELL_HONORABLE_DEFENDER_60Y);

        if (isBattleground() && sWorld->getBoolConfig(CONFIG_BATTLEGROUND_STORE_STATISTICS_ENABLE))
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PVPSTATS_PLAYER);
            BattlegroundScoreMap::const_iterator score = PlayerScores.find(player->GetGUIDLow());

            stmt->setUInt32(0, battlegroundId);
            stmt->setUInt64(1, player->GetGUID());
            stmt->setBool(2, team == winner);
            stmt->setUInt32(3, score->second->GetKillingBlows());
            stmt->setUInt32(4, score->second->GetDeaths());
            stmt->setUInt32(5, score->second->GetHonorableKills());
            stmt->setUInt32(6, score->second->GetBonusHonor());
            stmt->setUInt32(7, score->second->GetDamageDone());
            stmt->setUInt32(8, score->second->GetHealingDone());
            stmt->setUInt32(9, score->second->GetAttr1());
            stmt->setUInt32(10, score->second->GetAttr2());
            stmt->setUInt32(11, score->second->GetAttr3());
            stmt->setUInt32(12, score->second->GetAttr4());
            stmt->setUInt32(13, score->second->GetAttr5());
            CharacterDatabase.Execute(stmt);
        }

        // Reward winner team
        if (team == winner)
        {
            if (IsRandom() && !isRatedBattleground() || BattlegroundMgr::IsBGWeekend(GetTypeID()) && !isRatedBattleground())
            {
                uint32 rbgCap = player->GetCurrencyWeekCap(CURRENCY_TYPE_CONQUEST_META_RBG, true);
                uint32 arenaCap = player->GetCurrencyWeekCap(CURRENCY_TYPE_CONQUEST_META_ARENA, true);
                bool useArenaCap = arenaCap <= rbgCap;
                UpdatePlayerScore(player, SCORE_BONUS_HONOR, winner_honor);
                if (!player->IsRandomDailyBgRewarded())
                {
                    // 100cp awarded for the first random battleground won each day
                    player->ModifyCurrency(useArenaCap ? CURRENCY_TYPE_CONQUEST_META_ARENA : CURRENCY_TYPE_CONQUEST_META_RBG, BG_REWARD_WINNER_CONQUEST_FIRST);
                    player->SetRandomDailyBgRewarded(true);
                }
                else if (player->IsRandomDailyBgRewarded())
                    player->ModifyCurrency(useArenaCap ? CURRENCY_TYPE_CONQUEST_META_ARENA : CURRENCY_TYPE_CONQUEST_META_RBG, BG_REWARD_WINNER_CONQUEST_LAST);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90002))
                    if (player->GetQuestStatus(90002) == QUEST_STATUS_INCOMPLETE)
                        player->CompleteQuest(90002);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90003))
                    if (player->GetQuestStatus(90003) == QUEST_STATUS_INCOMPLETE)
                        player->CompleteQuest(90003);
            }
            else if (isRatedBattleground())
                player->ModifyCurrency(CURRENCY_TYPE_CONQUEST_META_RBG, BG_REWARD_WINNER_RBG_CONQUEST);
            else if (isArena() && isRated() && winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
            {
                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90004))
                    if (player->GetQuestStatus(90004) == QUEST_STATUS_INCOMPLETE)
                        player->CompleteQuest(90004);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90005))
                    if (player->GetQuestStatus(90005) == QUEST_STATUS_INCOMPLETE)
                        player->CompleteQuest(90005);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90006))
                    if (player->GetQuestStatus(90006) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 1600)
                            player->CompleteQuest(90006);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90007))
                    if (player->GetQuestStatus(90007) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 1700)
                            player->CompleteQuest(90007);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90008))
                    if (player->GetQuestStatus(90008) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 1800)
                            player->CompleteQuest(90008);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90009))
                    if (player->GetQuestStatus(90009) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 1900)
                            player->CompleteQuest(90009);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90010))
                    if (player->GetQuestStatus(90010) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 2000)
                            player->CompleteQuest(90010);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90011))
                    if (player->GetQuestStatus(90011) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 2100)
                            player->CompleteQuest(90011);

                if (Quest const* quest = sObjectMgr->GetQuestTemplate(90012))
                    if (player->GetQuestStatus(90012) == QUEST_STATUS_INCOMPLETE)
                        if (player->GetArenaPersonalRating(winnerArenaTeam->GetSlot()) >= 2200)
                            player->CompleteQuest(90012);
            }

            if (!isWarGame())
            {
                if (!isRatedBattleground())
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_BG, 1);
                else
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_BATTLEGROUND, 1);
            }

            if (!guildAwarded)
            {
                guildAwarded = true;
                if (uint32 guildId = GetBgMap()->GetOwnerGuildId(player->GetTeam()))
                    if (Guild* guild = sGuildMgr->GetGuildById(guildId))
                    {
                        guild->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_BG, 1, 0, 0, NULL, player);
                        if (isArena() && isRated() && winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
                            guild->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA, std::max<uint32>(winnerArenaTeam->GetRating(), 1), 0, 0, NULL, player);
                    }
            }

            if (player->getLevel() < 85)
            {
                if (isBattleground() && !isRatedBattleground() && !isWarGame())
                    if (Quest const* qInfo = sObjectMgr->GetQuestTemplate(90020))
                    {
                        player->AddQuest(qInfo, nullptr);
                        player->CompleteQuest(90020);
                    }
            }
        }
        else
        {
            if (IsRandom() && !isRatedBattleground() || BattlegroundMgr::IsBGWeekend(GetTypeID()) && !isRatedBattleground())
                UpdatePlayerScore(player, SCORE_BONUS_HONOR, loser_honor);

            if (player->getLevel() < 85)
            {
                if (isBattleground() && !isRatedBattleground() && !isWarGame())
                    if (Quest const* qInfo = sObjectMgr->GetQuestTemplate(90021))
                    {
                        player->AddQuest(qInfo, nullptr);
                        player->CompleteQuest(90021);
                    }
            }
        }

        // Update Rated BG Stats at the end of the Game
        if (isRatedBattleground())
        {
            for (BattlegroundPlayerStatsMap::const_iterator itr = m_AllPlayers.begin(); itr != m_AllPlayers.end(); ++itr)
            {
                if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                {
                    uint32 team = player->HasAura(81748) ? 469 : 67;
                    bool rewardWinner = (team == winner && itr->second.joined);
                    uint32 personalRating = itr->second.personalRating;
                    uint32 matchMakerRating = itr->second.matchMakerRating;
                    int32 personal_mod = GetRatingMod(personalRating, GetArenaMatchmakerRating(GetOtherTeam(team)), rewardWinner);
                    int32 matchmaker_mod = GetMatchmakerRatingMod(matchMakerRating, GetArenaMatchmakerRating(GetOtherTeam(team)), rewardWinner);

                    uint16 RBGpersonalRating = GetBattlegroundRating(personalRating, personal_mod);
                    uint16 RBGmatchMakerRating = GetBattlegroundRating(matchMakerRating, matchmaker_mod);
                    uint16 RBGgames = itr->second.games + 1;
                    uint16 RBGwins = itr->second.games + (rewardWinner ? 1 : 0);

                    sInfoMgr->UpdateCharRBGstats(itr->first, RBGwins, RBGgames, RBGpersonalRating);
                    sInfoMgr->UpdateCharMMR(itr->first, 3, RBGmatchMakerRating);

                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_REACH_BG_RATING, RBGpersonalRating);

                    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_RBG_STATS);
                    stmt->setUInt32(0, GUID_LOPART(itr->first));
                    stmt->setUInt16(1, RBGwins);
                    stmt->setUInt16(2, RBGgames);
                    stmt->setUInt16(3, RBGpersonalRating);
                    CharacterDatabase.Execute(stmt);
                }
            }
        }
        m_AllPlayers.clear();

        player->ResetAllPowers();
        player->CombatStopWithPets(true);

        BlockMovement(player);

        player->GetSession()->SendPacket(&pvpLogData);
        //TC_LOG_ERROR("sql.sql","SCOREBOARD PACKET SENT TO %s", player->GetName().c_str());

        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, player->GetBattlegroundQueueIndex(bgQueueTypeId), STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(GetTypeID()), GetElapsedTime(), GetArenaType(), true);
        player->GetSession()->SendPacket(&data);

        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND, 1);
    }

    if (isArena() && isRated() && winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
    {
        // save the stat changes
        winnerArenaTeam->SaveToDB();
        loserArenaTeam->SaveToDB();
        // send updated arena team stats to players
        // this way all arena team members will get notified, not only the ones who participated in this match
        winnerArenaTeam->NotifyStatsChanged();
        loserArenaTeam->NotifyStatsChanged();
    }

    if (winmsg_id)
        SendMessageToAll(winmsg_id, CHAT_MSG_BG_SYSTEM_NEUTRAL);
}

void Battleground::EndSoloBattleground(uint32 winner)
{
    RemoveFromBGFreeSlotQueue();

    uint32 loserTeamRating = 0;
    uint32 loserMatchmakerRating = 0;
    int32  loserChange = 0;
    int32  loserMatchmakerChange = 0;
    uint32 winnerTeamRating = 0;
    uint32 winnerMatchmakerRating = 0;
    int32  winnerChange = 0;
    int32  winnerMatchmakerChange = 0;
    int32 winmsg_id = 0;

    if (winner == ALLIANCE)
    {
        winmsg_id = isBattleground() ? LANG_BG_A_WINS : LANG_ARENA_GOLD_WINS;
        PlaySoundToAll(SOUND_ALLIANCE_WINS);
        SetWinner(WINNER_ALLIANCE);
    }
    else if (winner == HORDE)
    {
        winmsg_id = isBattleground() ? LANG_BG_H_WINS : LANG_ARENA_GREEN_WINS;
        PlaySoundToAll(SOUND_HORDE_WINS);
        SetWinner(WINNER_HORDE);
    }
    else
        SetWinner(WINNER_NONE);

    SetStatus(STATUS_WAIT_LEAVE);
    SetRemainingTime(TIME_AUTOCLOSE_BATTLEGROUND);

    // arena rating calculation
    if (winner != WINNER_NONE && winner != WINNER_NOT_JOINED)
    {
        for (auto& itr : PlayerScores)
        {
            if (itr.second->BgTeam == winner)
            {
                if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr.first, ARENA_TEAM_5v5)))
                    winnerTeamRating += ceil(team->GetRating() / 3);
            }
            else
            {
                if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr.first, ARENA_TEAM_5v5)))
                    loserTeamRating += ceil(team->GetRating() / 3);
            }
        }

        loserMatchmakerRating = GetArenaMatchmakerRating(GetOtherTeam(winner));
        winnerMatchmakerRating = GetArenaMatchmakerRating(winner);

        for (auto& itr : PlayerScores)
        {
            if (itr.second->BgTeam == winner)
            {
                if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr.first, ARENA_TEAM_5v5)))
                    winnerMatchmakerChange += team->WonAgainst(winnerMatchmakerRating, loserMatchmakerRating, winnerChange);
            }
            else
            {
                if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr.first, ARENA_TEAM_5v5)))
                    loserMatchmakerChange += team->LostAgainst(loserMatchmakerRating, winnerMatchmakerRating, loserChange);
            }
        }

        if (!sWorld->getBoolConfig(CONFIG_SOLO_QUEUE_ENABLE_MMR))
        {
            winnerMatchmakerChange = 0;
            loserMatchmakerChange = 0;
        }

        SetArenaMatchmakerRating(winner, winnerMatchmakerRating + winnerMatchmakerChange);
        SetArenaMatchmakerRating(GetOtherTeam(winner), loserMatchmakerRating + loserMatchmakerChange);

        SetArenaTeamRatingChangeForTeam(winner, winnerChange);
        SetArenaTeamRatingChangeForTeam(GetOtherTeam(winner), loserChange);
        
    }
    else if (winner == WINNER_NOT_JOINED)
    {
        SetArenaTeamRatingChangeForTeam(ALLIANCE, 0);
        SetArenaTeamRatingChangeForTeam(HORDE, 0);
    }
    // Deduct 12 points from each teams arena-rating if there are no winners after 45+2 minutes
    else
    {
        SetArenaTeamRatingChangeForTeam(ALLIANCE, ARENA_TIMELIMIT_POINTS_LOSS);
        SetArenaTeamRatingChangeForTeam(HORDE, ARENA_TIMELIMIT_POINTS_LOSS);

        for (auto& itr : PlayerScores)
            if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr.first, ARENA_TEAM_5v5)))
                team->FinishGame(ARENA_TIMELIMIT_POINTS_LOSS, true);
    }
    sInfoMgr->RemoveBGInfo(SLOT_SOLO_QUEUE, m_InstanceID);

    WorldPacket pvpLogData;
    sBattlegroundMgr->BuildPvpLogDataPacket(&pvpLogData, this);
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(GetTypeID(), GetArenaType());

    uint8 aliveWinners = GetAlivePlayersCountByTeam(winner);
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
    {
        uint32 team = itr->second.Team;

        if (itr->second.OfflineRemoveTime)
        {
            if (team == winner)
            {
                if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr->first, ARENA_TEAM_5v5)))
                    team->OfflineMemberLost(itr->first, loserMatchmakerRating, winnerMatchmakerChange);
            }
            else if (winner != WINNER_NOT_JOINED)
            {
                if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr->first, ARENA_TEAM_5v5)))
                    team->OfflineMemberLost(itr->first, winnerMatchmakerRating, loserMatchmakerChange);
            }
            continue;
        }

        Player* player = _GetPlayer(itr, "EndBattleground");
        if (!player)
            continue;

        // should remove spirit of redemption
        if (player->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
            player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

        if (!player->isAlive())
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }
        else
        {
            //needed cause else in av some creatures will kill the players at the end
            player->CombatStop();
            player->getHostileRefManager().deleteReferences();
        }

        // per player calculation
        if (team == winner)
        {
            // update achievement BEFORE personal rating update
            uint32 rating = player->GetArenaPersonalRating(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5));
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA, rating ? rating : 1);
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA, GetMapId());
            player->ModifyCurrency(CURRENCY_TYPE_CONQUEST_META_RBG, sWorld->getIntConfig(CONFIG_CURRENCY_CONQUEST_POINTS_SOLO_ARENA_REWARD));
            if (ArenaTeam* winTeam = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5))))
                winTeam->MemberWon(player, loserMatchmakerRating, winnerMatchmakerChange);
        }
        else if (winner != WINNER_NOT_JOINED)
        {
            if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5))))
                team->MemberLost(player, winnerMatchmakerRating, loserMatchmakerChange);

            // Arena lost => reset the win_rated_arena having the "no_lose" condition
            player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA, ACHIEVEMENT_CRITERIA_CONDITION_NO_LOSE);
        }

        m_AllPlayers.clear();
        player->ResetAllPowers();
        player->CombatStopWithPets(true);
        BlockMovement(player);
        player->GetSession()->SendPacket(&pvpLogData);

        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, player->GetBattlegroundQueueIndex(bgQueueTypeId), STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(GetTypeID()), GetElapsedTime(), GetArenaType(), true);
        player->GetSession()->SendPacket(&data);

        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND, 1);
    }

    // save the stat changes
    for (auto& itr : PlayerScores)
    {
        if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(Player::GetArenaTeamIdFromDB(itr.first, ARENA_TEAM_5v5)))
        {
            team->SaveToDB();
            team->NotifyStatsChanged();
        }
    }

    if (winmsg_id)
        SendMessageToAll(winmsg_id, CHAT_MSG_BG_SYSTEM_NEUTRAL);
}

uint32 Battleground::GetBonusHonorFromKill(uint32 kills) const
{
    //variable kills means how many honorable kills you scored (so we need kills * honor_for_one_kill)
    uint32 maxLevel = std::min<uint32>(GetMaxLevel(), 85U);
    return Trinity::Honor::hk_honor_at_level(maxLevel, float(kills));
}

void Battleground::BlockMovement(Player* player)
{
    player->SetClientControl(player, 0);                          // movement disabled NOTE: the effect will be automatically removed by client when the player is teleported from the battleground, so no need to send with uint8(1) in RemovePlayerAtLeave()
}

void Battleground::SendSpectateAddonsMsg(SpectatorAddonMsg msg)
{
    if (!HaveSpectators())
        return;

    for (SpectatorList::iterator itr = m_Spectators.begin(); itr != m_Spectators.end(); ++itr)
        msg.SendPacket(*itr);
}

uint8 Battleground::ArenaDampeningBitmask(uint8 aType) const
{
    auto arenaType = 0;
    
    switch (aType)
    {
    case ARENA_TYPE_2v2:        arenaType = DAMPEN_2v2;         break;
    case ARENA_TYPE_3v3:        arenaType = DAMPEN_3v3;         break;
    case ARENA_TYPE_3v3_SOLO:   arenaType = DAMPEN_3v3_SOLO;    break;
    case ARENA_TYPE_5v5:        arenaType = DAMPEN_5v5;         break;
    }

    return arenaType;
}

void Battleground::RemovePlayerAtLeave(uint64 guid, bool Transport, bool SendPacket)
{
    uint32 team = GetPlayerTeam(guid);
    bool participant = false;
    // Remove from lists/maps
    BattlegroundPlayerMap::iterator itr = m_Players.find(guid);
    if (itr != m_Players.end())
    {
        UpdatePlayersCountByTeam(team, true);               // -1 player
        m_Players.erase(itr);
        // check if the player was a participant of the match, or only entered through gm command (goname)
        participant = true;
    }

    BattlegroundScoreMap::iterator itr2 = PlayerScores.find(guid);
    if (!isArena() && itr2 != PlayerScores.end())
    {
        delete itr2->second;                                // delete player's score
        PlayerScores.erase(itr2);
    }

    RemovePlayerFromResurrectQueue(guid);

    Player* player = ObjectAccessor::FindPlayer(guid);

    if (player)
    {
        // should remove spirit of redemption
        if (player->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
            player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        player->RemoveAurasByType(SPELL_AURA_FLY);

        if (!player->isAlive())                              // resurrect on exit
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }
    }
    else // try to resurrect the offline player. If he is alive nothing will happen
        sObjectAccessor->ConvertCorpseForPlayer(guid);

    BattlegroundTypeId bgTypeId = GetTypeID();
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(GetTypeID(), GetArenaType());

    if (participant) // if the player was a match participant, remove auras, calc rating, update queue
    {
        if (player)
        {
            player->ClearAfkReports();
            player->ClearComboPoints();
            player->ClearComboPointHolders();

            if (!team) team = player->GetTeam();

            // if arena, remove the specific arena auras
            if (!isWarGame() && isArena() && GetStatus() != STATUS_WAIT_LEAVE)
            {
                bgTypeId = BATTLEGROUND_AA;                   // set the bg type to all arenas (it will be used for queue refreshing)
                if (isRated())
                {
                    //left a rated match while the encounter was in progress, consider as loser
                    ArenaTeam* winnerArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(GetOtherTeam(team)));
                    ArenaTeam* loserArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(team));
                    if (winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
                        loserArenaTeam->MemberLost(player, GetArenaMatchmakerRating(GetOtherTeam(team)));

                    if (isSoloQueueArena()) // Test if this fixes it
                        if (ArenaTeam* soloTeam = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5))))
                            soloTeam->MemberLost(player, GetArenaMatchmakerRating(GetOtherTeam(team)));
                }
            }
            if (SendPacket)
            {
                WorldPacket data;
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, player->GetBattlegroundQueueIndex(bgQueueTypeId), STATUS_NONE, player->GetBattlegroundQueueJoinTime(bgTypeId), 0, m_ArenaType);
                player->GetSession()->SendPacket(&data);
            }

            // this call is important, because player, when joins to battleground, this method is not called, so it must be called when leaving bg
            player->RemoveBattlegroundQueueId(bgQueueTypeId);
        }
        else
        // removing offline participant
        {
            if (isRated() && GetStatus() == STATUS_IN_PROGRESS)
            {
                //left a rated match while the encounter was in progress, consider as loser
                ArenaTeam* others_arena_team = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(GetOtherTeam(team)));
                ArenaTeam* players_arena_team = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(team));
                if (others_arena_team && players_arena_team)
                    players_arena_team->OfflineMemberLost(guid, GetArenaMatchmakerRating(GetOtherTeam(team)));
            }
        }

        // remove from raid group if player is member
        if (Group* group = GetBgRaid(team))
        {
            if (!group->RemoveMember(guid))                // group was disbanded
            {
                SetBgRaid(team, NULL);
            }
        }
        DecreaseInvitedCount(team);
        //we should update battleground queue, but only if bg isn't ending
        if (!isRated() && !isWarGame() && isBattleground() && GetStatus() < STATUS_WAIT_LEAVE)
        {
            // a player has left the battleground, so there are free slots -> add to queue
            AddToBGFreeSlotQueue();
            sBattlegroundMgr->ScheduleQueueUpdate(0, 0, bgQueueTypeId, bgTypeId, GetBracketId());
        }
        // Let others know
        WorldPacket data;
        sBattlegroundMgr->BuildPlayerLeftBattlegroundPacket(&data, guid);

        SendPacketToTeam(team, &data, player, false);
        if (isArena() && isRated())
            SendPacketToTeam(GetOtherTeam(team), &data, player, false);
    }

    RemovePlayer(player, guid, team);                           // BG subclass specific code

    if (player)
    {
        player->FitPlayerInTeam(false, this);
        player->ForceBGFactions(false);
        // Do next only if found in battleground
        player->SetBattlegroundId(0, BATTLEGROUND_TYPE_NONE);  // We're not in BG.
        // reset destination bg team
        player->SetBGTeam(0);
        player->RemoveBattlegroundQueueJoinTime(bgTypeId);
        player->SetCommandStatusOff(CHEAT_CASTTIME);

        if (Transport)
            player->TeleportToBGEntryPoint();
        if (player->GetGuild() && player->GetGuild()->GetLevel() >= 3)
            player->AddAura(78633, player);

        TC_LOG_DEBUG("bg.battleground", "Removed player %s from Battleground.", player->GetName().c_str());
    }

    //battleground object will be deleted next Battleground::Update() call
}

// this method is called when no players remains in battleground
void Battleground::Reset()
{
    SetWinner(WINNER_NONE);
    SetStatus(STATUS_WAIT_QUEUE);
    SetElapsedTime(0);
    SetRemainingTime(0);
    SetLastResurrectTime(0);
    m_Events = 0;

    if (m_InvitedAlliance > 0 || m_InvitedHorde > 0)
        TC_LOG_ERROR("bg.battleground", "Battleground::Reset: one of the counters is not 0 (alliance: %u, horde: %u) for BG (map: %u, instance id: %u)!",
            m_InvitedAlliance, m_InvitedHorde, m_MapId, m_InstanceID);

    m_InvitedAlliance = 0;
    m_InvitedHorde = 0;
    m_InBGFreeSlotQueue = false;

    m_Players.clear();

    for (BattlegroundScoreMap::const_iterator itr = PlayerScores.begin(); itr != PlayerScores.end(); ++itr)
        delete itr->second;
    PlayerScores.clear();

    ResetBGSubclass();
}

void Battleground::StartBattleground()
{
    SetElapsedTime(0);
    SetLastResurrectTime(0);
    // add BG to free slot queue
    AddToBGFreeSlotQueue();

    // add bg to update list
    // This must be done here, because we need to have already invited some players when first BG::Update() method is executed
    // and it doesn't matter if we call StartBattleground() more times, because m_Battlegrounds is a map and instance id never changes
    sBattlegroundMgr->AddBattleground(this);

    if (m_IsRated)
        TC_LOG_DEBUG("bg.arena", "Arena match type: %u for Team1Id: %u - Team2Id: %u started.", m_ArenaType, m_ArenaTeamIds[TEAM_ALLIANCE], m_ArenaTeamIds[TEAM_HORDE]);
}

void Battleground::AddPlayer(Player* player)
{
    // remove afk from player
    if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_AFK))
        player->ToggleAFK();

    // remove druid shapeshift
    if (player->getClass() == CLASS_DRUID) {
        player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    }
    /*
    force players into worgen form so that the crossfaction model is displayed.
    The two forms spell will be blocked under these same conditions so that they will remain in their fake form for the duration of the battleground.
    */
    if (player->getORace() == RACE_WORGEN)
    {
        if (!player->IsPlayingNative())
        {
            player->CastSpell(player, 97709, TRIGGERED_FULL_MASK);//force worgen form
        }
    }


    player->RemoveAurasDueToSpell(23335);//remove flag auras if they exist.
    player->RemoveAurasDueToSpell(23333);//remove flag auras if they exist.
    // score struct must be created in inherited class

    uint64 guid = player->GetGUID();
    uint32 team = player->GetTeam();

    BattlegroundPlayer bp;
    bp.OfflineRemoveTime = 0;
    bp.Team = team;

    // Add to list/maps
    m_Players[guid] = bp;

    UpdatePlayersCountByTeam(team, false);                  // +1 player

    WorldPacket data;
    sBattlegroundMgr->BuildPlayerJoinedBattlegroundPacket(&data, player->GetGUID());
    SendPacketToTeam(team, &data, player, false);

    // BG Status packet
    BattlegroundQueueTypeId bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(m_TypeID, GetArenaType());
    uint32 queueSlot = player->GetBattlegroundQueueIndex(bgQueueTypeId);

    sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, queueSlot, STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(m_TypeID), GetElapsedTime(), GetArenaType());
    player->GetSession()->SendPacket(&data);

    player->RemoveAurasByType(SPELL_AURA_MOUNTED);
    player->RemoveAurasByType(SPELL_AURA_FLY);
    player->RemoveAura(78633);
    player->_markedReady = false;
    player->ClearComboPointHolders();
    player->ClearComboPoints();

    // add arena specific auras
    if (isArena())
    {
        if (isSoloQueueArena())
            player->ActivateSpec(player->soloQueueSpec);

        player->RemoveArenaEnchantments(TEMP_ENCHANTMENT_SLOT);
        if (team == ALLIANCE)                                // gold
        {
            if (player->GetTeam() == HORDE)
                player->CastSpell(player, SPELL_HORDE_GOLD_FLAG, true);
            else
                player->CastSpell(player, SPELL_ALLIANCE_GOLD_FLAG, true);
        }
        else                                                // green
        {
            if (player->GetTeam() == HORDE)
                player->CastSpell(player, SPELL_HORDE_GREEN_FLAG, true);
            else
                player->CastSpell(player, SPELL_ALLIANCE_GREEN_FLAG, true);
        }

        player->DestroyConjuredItems(true);

        if (GetStatus() == STATUS_WAIT_JOIN)                 // not started yet
        {
            player->CastSpell(player, SPELL_ARENA_PREPARATION, true);
            player->ResetAllPowers();
            player->SetCommandStatusOn(CHEAT_CASTTIME);

            // Custom arena preparation to stress down the preparation time
            // in order to make it more fun
            switch (player->getClass())
            {
                case CLASS_MAGE:
                    player->CastSpell(player, 92830, true); // conjure refreshment table
                    break;
                case CLASS_WARLOCK:
                    player->CastSpell(player, 29886, true); // create soulwell
                    break;
            }
        }
    }
    else
    {
        if (isRatedBattleground())
        {
            if (player->GetTeam() == ALLIANCE)
                player->CastSpell(player, 81748, true); // Alliance buff
            else
                player->CastSpell(player, 81744, true); // Horde buff
        }
        
        if (GetStatus() == STATUS_WAIT_JOIN)                 // not started yet
        {
            player->CastSpell(player, SPELL_PREPARATION, true);   // reduces all mana cost of spells.

            int32 countdownMaxForBGType = isArena() ? ARENA_COUNTDOWN_MAX : BATTLEGROUND_COUNTDOWN_MAX;
            WorldPacket data(SMSG_START_TIMER, 4+4+4);
            data << uint32(0); // unk
            data << uint32(countdownMaxForBGType - (GetElapsedTime() / 1000));
            data << uint32(countdownMaxForBGType);
            player->GetSession()->SendPacket(&data);
        }

        //Send players update packets of eachother to let them target eacho ther.
        //BUG: This will misdirect players into thinking the otheres are still in the starter zone until they enter 
        //visibility range with the player and have this targability either cleared or updated.


        /*
        for (auto player_info : GetPlayers())
            if (auto p = ObjectAccessor::GetPlayer(*player, player_info.first))
                if (p->GetGUID() != player->GetGUID())
                {
                    UpdateData transData(player->GetMapId());
                    player->BuildCreateUpdateBlockForPlayer(&transData, p);
                    WorldPacket packet;
                    transData.BuildPacket(&packet);
                    p->GetSession()->SendPacket(&packet);




                    UpdateData transData2(p->GetMapId());
                    p->BuildCreateUpdateBlockForPlayer(&transData, player);
                    WorldPacket packet2;
                    transData.BuildPacket(&packet2);
                    player->GetSession()->SendPacket(&packet2);
                }

        */
    }

    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_BG, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL, ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP, GetMapId(), true);
    player->RemoveAurasByType(SPELL_AURA_FLY);

    // setup BG group membership
    PlayerAddedToBGCheckIfBGIsRunning(player);
    AddOrSetPlayerToCorrectBgGroup(player, team);
    player->FitPlayerInTeam(true, this);

    if (isBattleground() || isRatedBattleground())
        player->GetRole(true);
}

// this method adds player to his team's bg group, or sets his correct group if player is already in bg group
void Battleground::AddOrSetPlayerToCorrectBgGroup(Player* player, uint32 team)
{
    uint64 playerGuid = player->GetGUID();
    Group* group = GetBgRaid(team);
    if (!group)                                      // first player joined
    {
        group = new Group;
        SetBgRaid(team, group);
        group->Create(player);
    }
    else                                            // raid already exist
    {
        if (group->IsMember(playerGuid))
        {
            uint8 subgroup = group->GetMemberGroup(playerGuid);
            player->SetBattlegroundOrBattlefieldRaid(group, subgroup);
        }
        else
        {
            group->AddMember(player);
            if (Group* originalGroup = player->GetOriginalGroup())
                if (originalGroup->IsLeader(playerGuid))
                {
                    group->ChangeLeader(playerGuid);
                    group->SendUpdate();
                }
        }
    }
}

// This method should be called when player logs into running battleground
void Battleground::EventPlayerLoggedIn(Player* player)
{
    uint64 guid = player->GetGUID();
    // player is correct pointer
    for (std::deque<uint64>::iterator itr = m_OfflineQueue.begin(); itr != m_OfflineQueue.end(); ++itr)
    {
        if (*itr == guid)
        {
            m_OfflineQueue.erase(itr);
            break;
        }
    }
    m_Players[guid].OfflineRemoveTime = 0;
    player->FitPlayerInTeam(true, this);
    PlayerAddedToBGCheckIfBGIsRunning(player);
    // if battleground is starting, then add preparation aura
    // we don't have to do that, because preparation aura isn't removed when player logs out
}

// This method should be called when player logs out from running battleground
void Battleground::EventPlayerLoggedOut(Player* player)
{

    uint64 guid = player->GetGUID();
    if (!IsPlayerInBattleground(guid))  // Check if this player really is in battleground (might be a GM who teleported inside)
        return;

    // player is correct pointer, it is checked in WorldSession::LogoutPlayer()
    m_OfflineQueue.push_back(player->GetGUID());
    m_Players[guid].OfflineRemoveTime = sWorld->GetGameTime() + MAX_OFFLINE_TIME;
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        if (!player->IsSpectator())
        {
            // drop flag and handle other cleanups
            RemovePlayer(player, guid, GetPlayerTeam(guid));

            // 1 player is logging out, if it is the last, then end arena!
            if (isArena())
                if (auto team = player->GetTeam())
                    if (GetAlivePlayersCountByTeam(team) <= 1 && GetPlayersCountByTeam(GetOtherTeam(team)))
                        EndBattleground(GetOtherTeam(team));
        }
    }
    if (!player->IsSpectator())
        player->LeaveBattleground();
    else
    {
        player->TeleportToBGEntryPoint();
        RemoveSpectator(player->GetGUID());
    }
}

// This method should be called only once ... it adds pointer to queue
void Battleground::AddToBGFreeSlotQueue()
{
    if (!m_InBGFreeSlotQueue && isBattleground())
    {
        sBattlegroundMgr->AddToBGFreeSlotQueue(m_TypeID, this);
        m_InBGFreeSlotQueue = true;
    }
}

// This method removes this battleground from free queue - it must be called when deleting battleground
void Battleground::RemoveFromBGFreeSlotQueue()
{
    if (m_InBGFreeSlotQueue)
    {
        sBattlegroundMgr->RemoveFromBGFreeSlotQueue(m_TypeID, m_InstanceID);
        m_InBGFreeSlotQueue = false;
    }
}

// get the number of free slots for team
// returns the number how many players can join battleground to MaxPlayersPerTeam
uint32 Battleground::GetFreeSlotsForTeam(uint32 Team) const
{
    // if BG is starting ... invite anyone
    if (GetStatus() == STATUS_WAIT_JOIN)
        return (GetInvitedCount(Team) < GetMaxPlayersPerTeam()) ? GetMaxPlayersPerTeam() - GetInvitedCount(Team) : 0;
    // if BG is already started .. do not allow to join too much players of one faction
    uint32 otherTeam;
    uint32 otherIn;
    if (Team == ALLIANCE)
    {
        otherTeam = GetInvitedCount(HORDE);
        otherIn = GetPlayersCountByTeam(HORDE);
    }
    else
    {
        otherTeam = GetInvitedCount(ALLIANCE);
        otherIn = GetPlayersCountByTeam(ALLIANCE);
    }
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        // difference based on ppl invited (not necessarily entered battle)
        // default: allow 0
        uint32 diff = 0;
        // allow join one person if the sides are equal (to fill up bg to minplayersperteam)
        if (otherTeam == GetInvitedCount(Team))
            diff = 1;
        // allow join more ppl if the other side has more players
        else if (otherTeam > GetInvitedCount(Team))
            diff = otherTeam - GetInvitedCount(Team);

        // difference based on max players per team (don't allow inviting more)
        uint32 diff2 = (GetInvitedCount(Team) < GetMaxPlayersPerTeam()) ? GetMaxPlayersPerTeam() - GetInvitedCount(Team) : 0;
        // difference based on players who already entered
        // default: allow 0
        uint32 diff3 = 0;
        // allow join one person if the sides are equal (to fill up bg minplayersperteam)
        if (otherIn == GetPlayersCountByTeam(Team))
            diff3 = 1;
        // allow join more ppl if the other side has more players
        else if (otherIn > GetPlayersCountByTeam(Team))
            diff3 = otherIn - GetPlayersCountByTeam(Team);
        // or other side has less than minPlayersPerTeam
        else if (GetInvitedCount(Team) <= GetMinPlayersPerTeam())
            diff3 = GetMinPlayersPerTeam() - GetInvitedCount(Team) + 1;

        // return the minimum of the 3 differences

        // min of diff and diff 2
        diff = std::min(diff, diff2);
        // min of diff, diff2 and diff3
        return std::min(diff, diff3);
    }
    return 0;
}

bool Battleground::HasFreeSlots() const
{
    return GetPlayersSize() < GetMaxPlayers();
}

void Battleground::UpdatePlayerScore(Player* Source, uint32 type, uint32 value, bool doAddHonor)
{
    //this procedure is called from virtual function implemented in bg subclass
    BattlegroundScoreMap::const_iterator itr = PlayerScores.find(Source->GetGUID());
    if (itr == PlayerScores.end())                         // player not found...
        return;

    switch (type)
    {
        case SCORE_KILLING_BLOWS:                           // Killing blows
            itr->second->KillingBlows += value;
            break;
        case SCORE_DEATHS:                                  // Deaths
            itr->second->Deaths += value;
            break;
        case SCORE_HONORABLE_KILLS:                         // Honorable kills
            itr->second->HonorableKills += value;
            break;
        case SCORE_BONUS_HONOR:                             // Honor bonus
            // do not add honor in arenas
            if (isBattleground())
            {
                // reward honor instantly
                if (doAddHonor)
                    Source->RewardHonor(NULL, 1, value);    // RewardHonor calls UpdatePlayerScore with doAddHonor = false
                else
                    itr->second->BonusHonor += value;
            }
            break;
            // used only in EY, but in MSG_PVP_LOG_DATA opcode
        case SCORE_DAMAGE_DONE:                             // Damage Done
            itr->second->DamageDone += value;
            break;
        case SCORE_HEALING_DONE:                            // Healing Done
            itr->second->HealingDone += value;
            break;
        case SCORE_DAMAGE_TAKEN:
            itr->second->DamageTaken += value;
            break;
        case SCORE_HEALING_TAKEN:
            itr->second->HealingTaken += value;
            break;
        default:
            TC_LOG_ERROR("bg.battleground", "Battleground::UpdatePlayerScore: unknown score type (%u) for BG (map: %u, instance id: %u)!",
                type, m_MapId, m_InstanceID);
            break;
    }
}

void Battleground::AddPlayerToResurrectQueue(uint64 npc_guid, uint64 player_guid)
{
    m_ReviveQueue[npc_guid].push_back(player_guid);

    Player* player = ObjectAccessor::FindPlayer(player_guid);
    if (!player)
        return;

    player->CastSpell(player, SPELL_WAITING_FOR_RESURRECT, true);
}

void Battleground::RemovePlayerFromResurrectQueue(uint64 player_guid, bool isReapply)
{
    for (std::map<uint64, std::vector<uint64> >::iterator itr = m_ReviveQueue.begin(); itr != m_ReviveQueue.end(); ++itr)
    {
        for (std::vector<uint64>::iterator itr2 = (itr->second).begin(); itr2 != (itr->second).end(); ++itr2)
        {
            if (*itr2 == player_guid)
            {
                (itr->second).erase(itr2);

                if (!isReapply)
                    if (Player* player = ObjectAccessor::FindPlayer(player_guid))
                        player->RemoveAurasDueToSpell(SPELL_WAITING_FOR_RESURRECT);
                return;
            }
        }
    }
}

bool Battleground::AddObject(uint32 type, uint32 entry, float x, float y, float z, float o, float rotation0, float rotation1, float rotation2, float rotation3, uint32 /*respawnTime*/)
{
    // If the assert is called, means that BgObjects must be resized!
    ASSERT(type < BgObjects.size());

    Map* map = FindBgMap();
    if (!map)
        return false;

    GameObjectTemplate const* gInfo = sObjectMgr->GetGameObjectTemplate(entry);
    if (!gInfo)
        return false;

    // Must be created this way, adding to godatamap would add it to the base map of the instance
    // and when loading it (in go::LoadFromDB()), a new guid would be assigned to the object, and a new object would be created
    // So we must create it specific for this instance
    GameObject* go = nullptr;
    /*
    if (gInfo->type == GAMEOBJECT_TYPE_TRANSPORT)
    {
        go = new LinearTransport();
        if (!go->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_TRANSPORT), entry, GetBgMap(),
            PHASEMASK_NORMAL, x, y, z, o, rotation0, rotation1, rotation2, rotation3, 100, GO_STATE_READY))
        {
            TC_LOG_ERROR("bg.battlefield", "Battlefield::SpawnGameObject: Gameobject template %u could not be found in the database! Battlefield has not been created!", entry);
            TC_LOG_ERROR("bg.battlefield", "Battlefield::SpawnGameObject: Could not create gameobject template %u! Battlefield has not been created!", entry);
            delete go;
            return nullptr;
        }
    }
    else
    {
    */
        go = new GameObject;
        if (!go->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_GAMEOBJECT), entry, GetBgMap(),
            PHASEMASK_NORMAL, x, y, z, o, rotation0, rotation1, rotation2, rotation3, 100, GO_STATE_READY))
        {
            TC_LOG_ERROR("sql.sql", "Battleground::AddObject: cannot create gameobject (entry: %u) for BG (map: %u, instance id: %u)!",
                entry, m_MapId, m_InstanceID);
            TC_LOG_ERROR("bg.battleground", "Battleground::AddObject: cannot create gameobject (entry: %u) for BG (map: %u, instance id: %u)!",
                entry, m_MapId, m_InstanceID);
            delete go;
            return false;
        }
    //}
/*
    uint32 guid = go->GetGUIDLow();

    // without this, UseButtonOrDoor caused the crash, since it tried to get go info from godata
    // iirc that was changed, so adding to go data map is no longer required if that was the only function using godata from GameObject without checking if it existed
    GameObjectData& data = sObjectMgr->NewGOData(guid);

    data.id             = entry;
    data.mapid          = GetMapId();
    data.posX           = x;
    data.posY           = y;
    data.posZ           = z;
    data.orientation    = o;
    data.rotation0      = rotation0;
    data.rotation1      = rotation1;
    data.rotation2      = rotation2;
    data.rotation3      = rotation3;
    data.spawntimesecs  = respawnTime;
    data.spawnMask      = 1;
    data.animprogress   = 100;
    data.go_state       = 1;
*/
    // Add to world, so it can be later looked up from HashMapHolder
    if (!map->AddToMap(go))
    {
        delete go;
        return false;
    }

    BgObjects[type] = go->GetGUID();
    return true;
}

// Some doors aren't despawned so we cannot handle their closing in gameobject::update()
// It would be nice to correctly implement GO_ACTIVATED state and open/close doors in gameobject code
void Battleground::DoorClose(uint32 type)
{
    if (GameObject* obj = GetBgMap()->GetGameObject(BgObjects[type]))
    {
        // If doors are open, close it
        if (obj->getLootState() == GO_ACTIVATED && obj->GetGoState() != GO_STATE_READY)
        {
            obj->SetLootState(GO_READY);
            obj->SetGoState(GO_STATE_READY);
        }
    }
    else
        TC_LOG_ERROR("bg.battleground", "Battleground::DoorClose: door gameobject (type: %u, GUID: %u) not found for BG (map: %u, instance id: %u)!",
            type, GUID_LOPART(BgObjects[type]), m_MapId, m_InstanceID);
}

void Battleground::DoorOpen(uint32 type)
{
    if (GameObject* obj = GetBgMap()->GetGameObject(BgObjects[type]))
    {
        obj->SetLootState(GO_ACTIVATED);
        obj->SetGoState(GO_STATE_ACTIVE);
    }
    else
        TC_LOG_ERROR("bg.battleground", "Battleground::DoorOpen: door gameobject (type: %u, GUID: %u) not found for BG (map: %u, instance id: %u)!",
            type, GUID_LOPART(BgObjects[type]), m_MapId, m_InstanceID);
}

GameObject* Battleground::GetBGObject(uint32 type)
{
    GameObject* obj = GetBgMap()->GetGameObject(BgObjects[type]);
    if (!obj)
        TC_LOG_ERROR("bg.battleground", "Battleground::GetBGObject: gameobject (type: %u, GUID: %u) not found for BG (map: %u, instance id: %u)!",
            type, GUID_LOPART(BgObjects[type]), m_MapId, m_InstanceID);
    return obj;
}

int32 Battleground::GetCreatureType(uint64 guid)
{
    /*
    The creature "type" is functionally a replacement of GUIDs for creatures in battleground scripts.
    All Creatures spawned via BG scripts exist through a vector called BgCreatures.
    Their slot within this vector is how every BG script references a creature for spawning, actions, modifying, and deletion.
    This method will fetch that value in events where it's not readily available such as the HandleKillUnit() event in some BGs that returns just the creature itself.
    If this is used in an attempt to find the BgCreatures type(vector slot) of a creature NOT spawned via BG scripts, it will cause issues.
    */
    for (uint32 i = 0; i < BgCreatures.size(); ++i)
        if (BgCreatures[i] == guid) {
            return i;
        }
    TC_LOG_ERROR("bg.battleground", "Battleground::GetCreatureType: could not find this GUID within BgCreatures Vector: %u", GUID_LOPART(guid), m_MapId, m_InstanceID);
    return -1;
}

Creature* Battleground::GetBGCreature(uint32 type)
{
    Creature* creature = GetBgMap()->GetCreature(BgCreatures[type]);
    if (!creature)
        TC_LOG_ERROR("bg.battleground", "Battleground::GetBGCreature: creature (type: %u, GUID: %u) not found for BG (map: %u, instance id: %u)!",
            type, GUID_LOPART(BgCreatures[type]), m_MapId, m_InstanceID);
    return creature;
}

void Battleground::SpawnBGObject(uint32 type, uint32 respawntime)
{
    if (Map* map = FindBgMap())
        if (GameObject* obj = map->GetGameObject(BgObjects[type]))
        {
            if (respawntime)
                obj->SetLootState(GO_JUST_DEACTIVATED);
            else
                if (obj->getLootState() == GO_JUST_DEACTIVATED)
                    // Change state from GO_JUST_DEACTIVATED to GO_READY in case battleground is starting again
                    obj->SetLootState(GO_READY);
            obj->SetRespawnTime(respawntime);
            map->AddToMap(obj);
        }
}

Creature* Battleground::AddCreature(uint32 entry, uint32 type, uint32 teamval, float x, float y, float z, float o, uint32 respawntime)
{
    // If the assert is called, means that BgCreatures must be resized!
    if (!(type < BgCreatures.size())) {
        TC_LOG_ERROR("bg.battleground", "BG NPC Spawn: type %u was greater than bg creature size: %u.", type, BgCreatures.size());
    }
    ASSERT(type < BgCreatures.size());

    Map* map = FindBgMap();
    if (!map)
        return NULL;

    Creature* creature = new Creature;
    if (!creature->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT), map, PHASEMASK_NORMAL, entry, 0, teamval, x, y, z, o))
    {
        TC_LOG_ERROR("bg.battleground", "Battleground::AddCreature: cannot create creature (entry: %u) for BG (map: %u, instance id: %u)!",
            entry, m_MapId, m_InstanceID);
        delete creature;
        return NULL;
    }

    creature->SetHomePosition(x, y, z, o);

    CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!cinfo)
    {
        TC_LOG_ERROR("bg.battleground", "Battleground::AddCreature: creature template (entry: %u) does not exist for BG (map: %u, instance id: %u)!",
            entry, m_MapId, m_InstanceID);
        delete creature;
        return NULL;
    }
    // Force using DB speeds
    creature->SetSpeed(MOVE_WALK,  cinfo->speed_walk);
    creature->SetSpeed(MOVE_RUN,   cinfo->speed_run);

    if (!map->AddToMap(creature))
    {
        delete creature;
        return NULL;
    }

    BgCreatures[type] = creature->GetGUID();

    if (respawntime)
        creature->SetRespawnDelay(respawntime);

    return  creature;
}

bool Battleground::DelCreature(uint32 type)
{
    if (!BgCreatures[type])
        return true;

    if (Creature* creature = GetBgMap()->GetCreature(BgCreatures[type]))
    {
        creature->AddObjectToRemoveList();
        BgCreatures[type] = 0;
        return true;
    }

    TC_LOG_ERROR("bg.battleground", "Battleground::DelCreature: creature (type: %u, GUID: %u) not found for BG (map: %u, instance id: %u)!",
        type, GUID_LOPART(BgCreatures[type]), m_MapId, m_InstanceID);
    BgCreatures[type] = 0;
    return false;
}

bool Battleground::DelObject(uint32 type)
{
    if (!BgObjects[type])
        return true;

    if (GameObject* obj = GetBgMap()->GetGameObject(BgObjects[type]))
    {
        obj->SetRespawnTime(0);                                 // not save respawn time
        obj->Delete();
        obj->DeleteFromDB();
        BgObjects[type] = 0;
        return true;
    }
    TC_LOG_ERROR("bg.battleground", "Battleground::DelObject: gameobject (type: %u, GUID: %u) not found for BG (map: %u, instance id: %u)!",
        type, GUID_LOPART(BgObjects[type]), m_MapId, m_InstanceID);
    BgObjects[type] = 0;
    return false;
}

bool Battleground::AddSpiritGuide(uint32 type, float x, float y, float z, float o, uint32 team)
{
    uint32 entry = (team == ALLIANCE) ?
        BG_CREATURE_ENTRY_A_SPIRITGUIDE :
        BG_CREATURE_ENTRY_H_SPIRITGUIDE;

    if (Creature* creature = AddCreature(entry, type, team, x, y, z, o))
    {
        creature->setDeathState(DEAD);
        creature->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, creature->GetGUID());
        // aura
        // TODO: Fix display here
        // creature->SetVisibleAura(0, SPELL_SPIRIT_HEAL_CHANNEL);
        // casting visual effect
        creature->SetUInt32Value(UNIT_CHANNEL_SPELL, SPELL_SPIRIT_HEAL_CHANNEL);
        // correct cast speed
        creature->SetFloatValue(UNIT_MOD_CAST_SPEED, 1.0f);
        creature->SetFloatValue(UNIT_MOD_CAST_HASTE, 1.0f);
        //creature->CastSpell(creature, SPELL_SPIRIT_HEAL_CHANNEL, true);
        return true;
    }
    TC_LOG_ERROR("bg.battleground", "Battleground::AddSpiritGuide: cannot create spirit guide (type: %u, entry: %u) for BG (map: %u, instance id: %u)!",
        type, entry, m_MapId, m_InstanceID);
    EndNow();
    return false;
}

void Battleground::SendMessageToAll(int32 entry, ChatMsg type, Player const* source)
{
    if (!entry)
        return;

    Trinity::BattlegroundChatBuilder bg_builder(type, entry, source);
    Trinity::LocalizedPacketDo<Trinity::BattlegroundChatBuilder> bg_do(bg_builder);
    BroadcastWorker(bg_do);
}

void Battleground::PSendMessageToAll(int32 entry, ChatMsg type, Player const* source, ...)
{
    if (!entry)
        return;

    va_list ap;
    va_start(ap, source);

    Trinity::BattlegroundChatBuilder bg_builder(type, entry, source, &ap);
    Trinity::LocalizedPacketDo<Trinity::BattlegroundChatBuilder> bg_do(bg_builder);
    BroadcastWorker(bg_do);

    va_end(ap);
}

void Battleground::SendWarningToAll(int32 entry, ...)
{
    if (!entry)
        return;

    char const* format = sObjectMgr->GetTrinityStringForDBCLocale(entry);

    char str[1024];
    va_list ap;
    va_start(ap, entry);
    vsnprintf(str, 1024, format, ap);
    va_end(ap);
    std::string msg(str);

    WorldPacket data(SMSG_MESSAGECHAT, 200);

    data << (uint8)CHAT_MSG_RAID_BOSS_EMOTE;
    data << (uint32)LANG_UNIVERSAL;
    data << (uint64)0;
    data << (uint32)0;                                     // 2.1.0
    data << (uint32)1;
    data << (uint8)0;
    data << (uint64)0;
    data << (uint32)(msg.length() + 1);
    data << msg.c_str();
    data << (uint8)0;
    data << (float)0.0f;                                   // added in 4.2.0, unk
    data << (uint8)0;                                      // added in 4.2.0, unk
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(MAKE_NEW_GUID(itr->first, 0, HIGHGUID_PLAYER)))
            if (player->GetSession())
                player->GetSession()->SendPacket(&data);
}

void Battleground::SendMessage2ToAll(int32 entry, ChatMsg type, Player const* source, int32 arg1, int32 arg2)
{
    Trinity::Battleground2ChatBuilder bg_builder(type, entry, source, arg1, arg2);
    Trinity::LocalizedPacketDo<Trinity::Battleground2ChatBuilder> bg_do(bg_builder);
    BroadcastWorker(bg_do);
}

void Battleground::EndNow()
{
    RemoveFromBGFreeSlotQueue();
    SetStatus(STATUS_WAIT_LEAVE);
    SetRemainingTime(0);
}

// To be removed
char const* Battleground::GetTrinityString(int32 entry)
{
    // FIXME: now we have different DBC locales and need localized message for each target client
    return sObjectMgr->GetTrinityStringForDBCLocale(entry);
}

// IMPORTANT NOTICE:
// buffs aren't spawned/despawned when players captures anything
// buffs are in their positions when battleground starts
void Battleground::HandleTriggerBuff(uint64 go_guid)
{
    GameObject* obj = GetBgMap()->GetGameObject(go_guid);
    if (!obj || obj->GetGoType() != GAMEOBJECT_TYPE_TRAP || !obj->isSpawned())
        return;

    // Change buff type, when buff is used:
    int32 index = BgObjects.size() - 1;
    while (index >= 0 && BgObjects[index] != go_guid)
        index--;
    if (index < 0)
    {
        TC_LOG_ERROR("bg.battleground", "Battleground::HandleTriggerBuff: cannot find buff gameobject (GUID: %u, entry: %u, type: %u) in internal data for BG (map: %u, instance id: %u)!",
            GUID_LOPART(go_guid), obj->GetEntry(), obj->GetGoType(), m_MapId, m_InstanceID);
        return;
    }

    // Randomly select new buff
    uint8 buff = urand(0, 2);
    uint32 entry = obj->GetEntry();
    if (m_BuffChange && entry != Buff_Entries[buff])
    {
        // Despawn current buff
        SpawnBGObject(index, RESPAWN_ONE_DAY);
        // Set index for new one
        for (uint8 currBuffTypeIndex = 0; currBuffTypeIndex < 3; ++currBuffTypeIndex)
            if (entry == Buff_Entries[currBuffTypeIndex])
            {
                index -= currBuffTypeIndex;
                index += buff;
            }
    }

    SpawnBGObject(index, BUFF_RESPAWN_TIME);
}

void Battleground::HandleKillPlayer(Player* victim, Player* killer)
{
    // Keep in mind that for arena this will have to be changed a bit

    // Add +1 deaths
    UpdatePlayerScore(victim, SCORE_DEATHS, 1);
    // Add +1 kills to group and +1 killing_blows to killer
    if (killer)
    {
        // Don't reward credit for killing ourselves, like fall damage of hellfire (warlock)
        if (killer == victim)
            return;

        UpdatePlayerScore(killer, SCORE_HONORABLE_KILLS, 1);
        UpdatePlayerScore(killer, SCORE_KILLING_BLOWS, 1);

        for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        {
            Player* creditedPlayer = ObjectAccessor::FindPlayer(itr->first);
            if (!creditedPlayer || creditedPlayer == killer)
                continue;

            if (creditedPlayer->GetTeam() == killer->GetTeam() && creditedPlayer->IsAtGroupRewardDistance(victim))
                UpdatePlayerScore(creditedPlayer, SCORE_HONORABLE_KILLS, 1);
        }
    }

    if (!isArena())
    {
        // To be able to remove insignia -- ONLY IN Battlegrounds
        victim->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE);
        RewardXPAtKill(killer, victim);
    }
}

// Return the player's team based on battlegroundplayer info
// Used in same faction arena matches mainly
uint32 Battleground::GetPlayerTeam(uint64 guid) const
{
    auto itr = m_Players.find(guid);
    if (itr != m_Players.end())
        return itr->second.Team;
    return 0;
}

uint32 Battleground::GetOtherTeam(uint32 teamId) const
{
    return teamId ? ((teamId == ALLIANCE) ? HORDE : ALLIANCE) : 0;
}

bool Battleground::IsPlayerInBattleground(uint64 guid) const
{
    auto itr = m_Players.find(guid);
    if (itr != m_Players.end())
        return true;
    return false;
}

void Battleground::PlayerAddedToBGCheckIfBGIsRunning(Player* player)
{
    if (GetStatus() != STATUS_WAIT_LEAVE)
        return;

    WorldPacket data;
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(GetTypeID(), GetArenaType());

    BlockMovement(player);

    sBattlegroundMgr->BuildPvpLogDataPacket(&data, this);
    player->GetSession()->SendPacket(&data);

    sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, this, player, player->GetBattlegroundQueueIndex(bgQueueTypeId), STATUS_IN_PROGRESS, player->GetBattlegroundQueueJoinTime(GetTypeID()), GetElapsedTime(), GetArenaType());
    player->GetSession()->SendPacket(&data);
}

uint32 Battleground::GetAlivePlayersCountByTeam(uint32 Team) const
{
    int count = 0;
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
    {
        if (itr->second.Team == Team)
        {
            Player* player = ObjectAccessor::FindPlayer(itr->first);
            if (player && player->isAlive() && !player->HasByteFlag(UNIT_FIELD_BYTES_2, 3, FORM_SPIRITOFREDEMPTION))
                ++count;
        }
    }
    return count;
}

void Battleground::SetHoliday(bool is_holiday)
{
    m_HonorMode = is_holiday ? BG_HOLIDAY : BG_NORMAL;
}

int32 Battleground::GetObjectType(uint64 guid)
{
    for (uint32 i = 0; i < BgObjects.size(); ++i)
        if (BgObjects[i] == guid)
            return i;
    TC_LOG_ERROR("bg.battleground", "Battleground::GetObjectType: player used gameobject (GUID: %u) which is not in internal data for BG (map: %u, instance id: %u), cheating?",
        GUID_LOPART(guid), m_MapId, m_InstanceID);
    return -1;
}

void Battleground::HandleKillUnit(Creature* /*victim*/, Player* /*killer*/)
{
}

void Battleground::CheckArenaAfterTimerConditions()
{
    EndBattleground(WINNER_NONE);
}

void Battleground::CheckArenaWinConditions()
{
    if (!GetAlivePlayersCountByTeam(ALLIANCE) && GetPlayersCountByTeam(HORDE))
        EndBattleground(HORDE);
    else if (GetPlayersCountByTeam(ALLIANCE) && !GetAlivePlayersCountByTeam(HORDE))
        EndBattleground(ALLIANCE);
}

void Battleground::UpdateArenaWorldState()
{
    UpdateWorldState(0xe10, GetAlivePlayersCountByTeam(HORDE));
    UpdateWorldState(0xe11, GetAlivePlayersCountByTeam(ALLIANCE));
}

float Battleground::GetChanceAgainst(uint32 ownRating, uint32 opponentRating)
{
    // Returns the chance to win against a team with the given rating, used in the rating adjustment calculation
    // ELO system
    return 1.0f / (1.0f + exp(log(10.0f) * (float)((float)opponentRating - (float)ownRating) / 650.0f));
}

int32 Battleground::GetRatingMod(uint32 ownRating, uint32 opponentRating, bool won)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;

    // Calculate the rating modification
    float mod;

    // TODO: Replace this hack with using the confidence factor (limiting the factor to 2.0f)
    if (won && ownRating < 1300)
    {
        if (ownRating < 1000)
            mod = 192 * (won_mod - chance);
        else
            mod = (72.0f + (24.0f * (1300.0f - float(ownRating)) / 300.0f)) * (won_mod - chance);
    }
    else
        mod = 48.0f * (won_mod - chance);

    return (int32)ceil(mod);
}

int32 Battleground::GetMatchmakerRatingMod(uint32 ownRating, uint32 opponentRating, bool won)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;
    float mod = won_mod - chance;

    // Real rating modification
    mod *= 24.0f;

    return (int32)ceil(mod * 2);
}
uint32 Battleground::GetBattlegroundRating(uint32 rating, int32 mod)
{
    if (int32(rating) + mod < 0)
        return 0;
    else
        return rating + mod;
}

void Battleground::SetRBGdidntJoin(uint64 guid)
{
    BattlegroundPlayerStatsMap::iterator itr = m_AllPlayers.find(guid);
    if (itr != m_AllPlayers.end())
        itr->second.joined = false;
}

void Battleground::SetBgRaid(uint32 TeamID, Group* bg_raid)
{
    Group*& old_raid = TeamID == ALLIANCE ? m_BgRaids[TEAM_ALLIANCE] : m_BgRaids[TEAM_HORDE];
    if (old_raid)
        old_raid->SetBattlegroundGroup(NULL);
    if (bg_raid)
        bg_raid->SetBattlegroundGroup(this);
    old_raid = bg_raid;
}

WorldSafeLocsEntry const* Battleground::GetClosestGraveYard(Player* player)
{
    return sObjectMgr->GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
}

void Battleground::StartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry)
{
    for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(itr->first))
            player->StartTimedAchievement(type, entry);
}

void Battleground::SetBracket(PvPDifficultyEntry const* bracketEntry)
{
    m_BracketId = bracketEntry->GetBracketId();
    SetLevelRange(bracketEntry->minLevel, bracketEntry->maxLevel);
}

void Battleground::RewardXPAtKill(Player* killer, Player* victim)
{
    if (sWorld->getBoolConfig(CONFIG_BG_XP_FOR_KILL) && killer && victim)
        killer->RewardPlayerAndGroupAtKill(victim, true);
}

uint32 Battleground::GetTeamScore(uint32 teamId) const
{
    if (teamId == TEAM_ALLIANCE || teamId == TEAM_HORDE)
        return m_TeamScores[teamId];

    TC_LOG_ERROR("bg.battleground", "GetTeamScore with wrong Team %u for BG %u", teamId, GetTypeID());
    return 0;
}

void Battleground::HandleAreaTrigger(Player* player, uint32 trigger)
{
    TC_LOG_DEBUG("bg.battleground", "Unhandled AreaTrigger %u in Battleground %u. Player coords (x: %f, y: %f, z: %f)",
                   trigger, player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
}

uint8 Battleground::GetUniqueBracketId() const
{
    return uint8(GetMinLevel() / 5) - 1; // 10 - 1, 15 - 2, 20 - 3, etc.
}

Player* Battleground::GetFirstPlayerInArena()
{
    for (auto itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        if (Player* target = ObjectAccessor::FindPlayer(itr->first))
            if (target->GetMap()->IsBattleArena())
                return target;
    return NULL;
}