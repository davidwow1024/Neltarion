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

#ifndef __BATTLEGROUNDQUEUE_H
#define __BATTLEGROUNDQUEUE_H

#include "Common.h"
#include "DBCEnums.h"
#include "Battleground.h"
#include "EventProcessor.h"

#include <deque>
#include "SoloQueue.h"

//this container can't be deque, because deque doesn't like removing the last element - if you remove it, it invalidates next iterator and crash appears
typedef std::list<Battleground*> BGFreeSlotQueueContainer;

#define COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME 10

struct GroupQueueInfo;                                      // type predefinition
struct PlayerQueueInfo                                      // stores information for players in queue
{
    uint32  LastOnlineTime;                                 // for tracking and removing offline players from queue after 5 minutes
    GroupQueueInfo* GroupInfo;                             // pointer to the associated groupqueueinfo
};

struct GroupQueueInfo                                       // stores information about the group in queue (also used when joined as solo!)
{
    std::map<uint64, PlayerQueueInfo*> Players;             // player queue info map
    uint32  Team;                                           // Player team (ALLIANCE/HORDE)
    uint32  OTeam; 
    BattlegroundTypeId BgTypeId;                            // battleground type id
    bool    IsRated;                                        // rated
    uint8   ArenaType;                                      // 2v2, 3v3, 5v5 or 0 when BG
    uint32  ArenaTeamIdOrRbgLeaderGuid;                                    // team id if rated match
    uint32  JoinTime;                                       // time when group was added
    uint32  RemoveInviteTime;                               // time when we will remove invite for players in group
    uint32  IsInvitedToBGInstanceGUID;                      // was invited to certain BG
    uint32  ArenaTeamRating;                                // if rated match, inited to the rating of the team
    uint32  ArenaMatchmakerRating;                          // if rated match, inited to the rating of the team
    uint32  OpponentsTeamRating;                            // for rated arena matches
    uint32  OpponentsMatchmakerRating;                      // for rated arena matches
    uint32  ratingRange;                                    // for rated arena matches
    uint32  ratingRangeIncreaseCounter;                     // for rated arena matches
    bool    isSoloQueueGroup = false;                       // needed for RemovePlayer() handling
};

struct WarGameGroupQueueInfo
{
    std::list<uint64> TeamA;
    std::list<uint64> TeamB;
    BattlegroundTypeId BgTypeId;
    BattlegroundBracketId BracketId;
    uint32  IsInvitedToBGInstanceGUID;
    uint32  JoinTime;
    uint8 ArenaType;

    bool IsPlayerIn(uint64 guid, uint8* team = nullptr, bool remove = false)
    {
        std::list<uint64>* loopList;
        for (uint8 i = 0; i < 2; ++i)
        {
            loopList = i == 0 ? &TeamA : &TeamB;
            for (std::list<uint64>::iterator itr = (*loopList).begin(); itr != (*loopList).end(); ++itr)
                if (guid == (*itr))
                {
                    if (team != nullptr)
                        *team = i;

                    if (remove)
                    {
                        if (i == 0)
                            (*loopList).erase(itr);
                        else (*loopList).erase(itr);
                    }
                    return true;
                }
        }
        return false;
    }
};

enum BattlegroundQueueGroupTypes
{
    BG_QUEUE_PREMADE_ALLIANCE   = 0,
    BG_QUEUE_PREMADE_HORDE      = 1,
    BG_QUEUE_NORMAL_ALLIANCE    = 2,
    BG_QUEUE_NORMAL_HORDE       = 3,
    BG_QUEUE_MIXED              = 4,
    BG_QUEUE_RBG                = 5
};
#define BG_QUEUE_GROUP_TYPES_COUNT 6
#define BG_NORMAL_QUEUE_GROUP_TYPES_COUNT 5

class Battleground;
class BattlegroundQueue
{
    public:
        BattlegroundQueue();
        ~BattlegroundQueue();

        void BattlegroundQueueUpdate(uint32 diff, BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id, uint8 arenaType = 0, bool isRated = false, uint32 minRating = 0);
        void BattlegroundWarGameQueueUpdate(BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id);
        void UpdateEvents(uint32 diff);

        bool FillXPlayersToBG(BattlegroundBracketId bracket_id, Battleground* bg, bool start = false, uint8 minPlayers = 0, uint8 maxPlayers = 0); 
        typedef std::multimap<size_t, GroupQueueInfo*> QueuedGroupMap;
        int32 PreAddPlayers(QueuedGroupMap m_PreGroupMap, int32 MaxAdd, uint32 MaxInTeam);
        bool CheckCrossFactionMatch(BattlegroundBracketId bracket_id, Battleground* bg, uint8 minPlayers, uint8 maxPlayers);
        bool CanJoinBattleground(GroupQueueInfo* ginfo, Battleground const* bgOrTemplate);
        void FillPlayersToBG(Battleground* bg, BattlegroundBracketId bracket_id);
        bool CheckPremadeMatch(BattlegroundBracketId bracket_id, uint32 MinPlayersPerTeam, uint32 MaxPlayersPerTeam);
        bool CheckNormalMatch(Battleground* bg_template, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers);
        bool CheckSkirmishForSameFaction(BattlegroundBracketId bracket_id, uint32 minPlayersPerTeam);
        GroupQueueInfo* AddGroup(Player* leader, Group* group, BattlegroundTypeId bgTypeId, PvPDifficultyEntry const*  bracketEntry, uint8 ArenaType, bool isRated, bool isPremade, uint32 ArenaRating, uint32 MatchmakerRating, uint32 ArenaTeamId = 0);
        WarGameGroupQueueInfo AddWarGameGroup(Group* groupA, Group* groupB, BattlegroundTypeId bgTypeId, BattlegroundBracketId bracketId, uint8 ArenaType);
        GroupQueueInfo* AddSoloQueueGroup(std::list<SoloQueueInfo*> playerList, BattlegroundBracketId bracketId, uint32 team);
        void RemovePlayer(uint64 guid, bool decreaseInvitedCount);
        bool IsPlayerInvited(uint64 pl_guid, const uint32 bgInstanceGuid, const uint32 removeTime);
        bool GetPlayerGroupInfoData(uint64 guid, GroupQueueInfo* ginfo);
        bool GetInvitedPlayerWarGameGroupInfoData(uint64 guid, WarGameGroupQueueInfo& wginfo, uint8& team);
        void PlayerInvitedToBGUpdateAverageWaitTime(GroupQueueInfo* ginfo, BattlegroundBracketId bracket_id);
        uint32 GetAverageQueueWaitTime(GroupQueueInfo* ginfo, BattlegroundBracketId bracket_id) const;
        void IncreaseTeamMMrRange(GroupQueueInfo* ginfo);
        uint32 GetQueuedGroups() const;

        typedef std::map<uint64, PlayerQueueInfo> QueuedPlayersMap;
        QueuedPlayersMap m_QueuedPlayers;

        //we need constant add to begin and constant remove / add from the end, therefore deque suits our problem well
        typedef std::list<GroupQueueInfo*> GroupsQueueType;

        /*
        This two dimensional array is used to store All queued groups
        First dimension specifies the bgTypeId
        Second dimension specifies the player's group types -
             BG_QUEUE_PREMADE_ALLIANCE  is used for premade alliance groups and alliance rated arena teams
             BG_QUEUE_PREMADE_HORDE     is used for premade horde groups and horde rated arena teams
             BG_QUEUE_NORMAL_ALLIANCE   is used for normal (or small) alliance groups or non-rated arena matches
             BG_QUEUE_NORMAL_HORDE      is used for normal (or small) horde groups or non-rated arena matches
        */
        GroupsQueueType m_QueuedGroups[MAX_BATTLEGROUND_BRACKETS][BG_QUEUE_GROUP_TYPES_COUNT];

        // for WarGames
        std::list<WarGameGroupQueueInfo> m_QueuedWargameGroups;
        std::list<WarGameGroupQueueInfo> m_InvitedWargameGroups;

        // class to select and invite groups to bg
        class SelectionPool
        {
        public:
            SelectionPool(): PlayerCount(0) {};
            void Init();
            bool AddGroup(GroupQueueInfo* ginfo, uint32 desiredCount);
            bool KickGroup(uint32 size);
            uint32 GetPlayerCount() const {return PlayerCount;}
        public:
            GroupsQueueType SelectedGroups;
        private:
            uint32 PlayerCount;
        };

        //one selection pool for horde, other one for alliance
        SelectionPool m_SelectionPools[BG_TEAMS_COUNT];
        uint32 GetPlayersInQueue(TeamId id);

        // Random Battleground selection
        BattlegroundTypeId GetNextRandomBattleground() { return nextRandomTypeId; }
        void SetNextRandomBattleground(BattlegroundTypeId typeId) { nextRandomTypeId = typeId; }
        uint32 GetRandomBattlegroundDecayTimer() { return m_RandomBattlegroundDecayTimer; }
        void SetRandomBattlegroundDecayTimer(time_t timer) { m_RandomBattlegroundDecayTimer = timer; }
    private:

        bool InviteGroupToBG(GroupQueueInfo* ginfo, Battleground* bg, uint32 side);
        void InviteWarGameGroupToBG(WarGameGroupQueueInfo* wginfo, Battleground* bg);
        uint32 m_WaitTimes[BG_TEAMS_COUNT][MAX_BATTLEGROUND_BRACKETS][COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME];
        uint32 m_WaitTimeLastPlayer[BG_TEAMS_COUNT][MAX_BATTLEGROUND_BRACKETS];
        uint32 m_SumOfWaitTimes[BG_TEAMS_COUNT][MAX_BATTLEGROUND_BRACKETS];
        BattlegroundTypeId nextRandomTypeId = BATTLEGROUND_TYPE_NONE;

        // Event handler
        EventProcessor m_events;

        time_t m_RandomBattlegroundDecayTimer;
};

/*
    This class is used to invite player to BG again, when minute lasts from his first invitation
    it is capable to solve all possibilities
*/
class BGQueueInviteEvent : public BasicEvent
{
    public:
        BGQueueInviteEvent(uint64 pl_guid, uint32 BgInstanceGUID, BattlegroundTypeId BgTypeId, uint8 arenaType, uint32 removeTime) :
          m_PlayerGuid(pl_guid), m_BgInstanceGUID(BgInstanceGUID), m_BgTypeId(BgTypeId), m_ArenaType(arenaType), m_RemoveTime(removeTime)
          { }
        virtual ~BGQueueInviteEvent() { }

        virtual bool Execute(uint64 e_time, uint32 p_time);
        virtual void Abort(uint64 e_time);
    private:
        uint64 m_PlayerGuid;
        uint32 m_BgInstanceGUID;
        BattlegroundTypeId m_BgTypeId;
        uint8  m_ArenaType;
        uint32 m_RemoveTime;
};

/*
    This class is used to remove player from BG queue after 1 minute 20 seconds from first invitation
    We must store removeInvite time in case player left queue and joined and is invited again
    We must store bgQueueTypeId, because battleground can be deleted already, when player entered it
*/
class BGQueueRemoveEvent : public BasicEvent
{
    public:
        BGQueueRemoveEvent(uint64 pl_guid, uint32 bgInstanceGUID, BattlegroundTypeId BgTypeId, uint8 ArenaType, BattlegroundQueueTypeId bgQueueTypeId, uint32 removeTime)
            : m_PlayerGuid(pl_guid), m_BgInstanceGUID(bgInstanceGUID), m_ArenaType(ArenaType), m_RemoveTime(removeTime), m_BgTypeId(BgTypeId), m_BgQueueTypeId(bgQueueTypeId)
        {}

        virtual ~BGQueueRemoveEvent() {}

        virtual bool Execute(uint64 e_time, uint32 p_time);
        virtual void Abort(uint64 e_time);
    private:
        uint64 m_PlayerGuid;
        uint32 m_BgInstanceGUID;
        uint8 m_ArenaType;
        uint32 m_RemoveTime;
        BattlegroundTypeId m_BgTypeId;
        BattlegroundQueueTypeId m_BgQueueTypeId;
};

#endif
