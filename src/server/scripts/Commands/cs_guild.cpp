/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
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
Name: guild_commandscript
%Complete: 100
Comment: All guild related commands
Category: commandscripts
EndScriptData */

#include "AchievementMgr.h"
#include "Chat.h"
#include "Language.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "ScriptMgr.h"

class guild_commandscript : public CommandScript
{
public:
    guild_commandscript() : CommandScript("guild_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> guildCommandTable =
        {
            { "create",         SEC_CONSOLE,     true,  &HandleGuildCreateCommand,           "" },
            { "delete",         SEC_CONSOLE,     true,  &HandleGuildDeleteCommand,           "" },
            { "invite",         SEC_CONSOLE,     true,  &HandleGuildInviteCommand,           "" },
            { "uninvite",       SEC_CONSOLE,     true,  &HandleGuildUninviteCommand,         "" },
            { "rank",           SEC_CONSOLE,     true,  &HandleGuildRankCommand,             "" },
            { "level",          SEC_CONSOLE,     true,  &HandleGuildLevelCommand,            "" },
            { "rename",         SEC_CONSOLE,     true,  &HandleGuildRenameCommand,           "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "guild",          SEC_CONSOLE,         true,  NULL,                                 "", guildCommandTable },
        };
        return commandTable;
    }

    /** \brief GM command level 3 - Create a guild.
     *
     * This command allows a GM (level 3) to create a guild.
     *
     * The "args" parameter contains the name of the guild leader
     * and then the name of the guild.
     *
     */
    static bool HandleGuildCreateCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // if not guild name only (in "") then player name
        Player* target;
        if (!handler->extractPlayerTarget(*args != '"' ? (char*)args : NULL, &target))
            return false;

        char* tailStr = *args != '"' ? strtok(NULL, "") : (char*)args;
        if (!tailStr)
            return false;

        char* guildStr = handler->extractQuotedArg(tailStr);
        if (!guildStr)
            return false;

        std::string guildName = guildStr;

        if (target->GetGuildId())
        {
            handler->SendSysMessage(LANG_PLAYER_IN_GUILD);
            return true;
        }

        Guild* guild = new Guild;
        if (!guild->Create(target, guildName))
        {
            delete guild;
            handler->SendSysMessage(LANG_GUILD_NOT_CREATED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        sGuildMgr->AddGuild(guild);

        return true;
    }

    static bool HandleGuildDeleteCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        char* guildStr = handler->extractQuotedArg((char*)args);
        if (!guildStr)
            return false;

        std::string guildName = guildStr;

        Guild* targetGuild = sGuildMgr->GetGuildByName(guildName);
        if (!targetGuild)
            return false;

        targetGuild->Disband();
        delete targetGuild;

        return true;
    }

    static bool HandleGuildInviteCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // if not guild name only (in "") then player name
        uint64 targetGuid;
        if (!handler->extractPlayerTarget(*args != '"' ? (char*)args : NULL, NULL, &targetGuid))
            return false;

        char* tailStr = *args != '"' ? strtok(NULL, "") : (char*)args;
        if (!tailStr)
            return false;

        char* guildStr = handler->extractQuotedArg(tailStr);
        if (!guildStr)
            return false;

        std::string guildName = guildStr;
        Guild* targetGuild = sGuildMgr->GetGuildByName(guildName);
        if (!targetGuild)
            return false;

        // player's guild membership checked in AddMember before add
        return targetGuild->AddMember(targetGuid);
    }

    static bool HandleGuildUninviteCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        uint64 targetGuid;
        if (!handler->extractPlayerTarget((char*)args, &target, &targetGuid))
            return false;

        uint32 guildId = target ? target->GetGuildId() : Player::GetGuildIdFromDB(targetGuid);
        if (!guildId)
            return false;

        Guild* targetGuild = sGuildMgr->GetGuildById(guildId);
        if (!targetGuild)
            return false;

        targetGuild->DeleteMember(targetGuid, false, true, true);
        return true;
    }

    static bool HandleGuildRankCommand(ChatHandler* handler, char const* args)
    {
        char* nameStr;
        char* rankStr;
        handler->extractOptFirstArg((char*)args, &nameStr, &rankStr);
        if (!rankStr)
            return false;

        Player* target;
        uint64 targetGuid;
        std::string target_name;
        if (!handler->extractPlayerTarget(nameStr, &target, &targetGuid, &target_name))
            return false;

        uint32 guildId = target ? target->GetGuildId() : Player::GetGuildIdFromDB(targetGuid);
        if (!guildId)
            return false;

        Guild* targetGuild = sGuildMgr->GetGuildById(guildId);
        if (!targetGuild)
            return false;

        uint8 newRank = uint8(atoi(rankStr));
        return targetGuild->ChangeMemberRank(targetGuid, newRank);
    }

    static bool HandleGuildLevelCommand(ChatHandler* handler, char const* args)
    {
        char* nameStr;
        char* levelStr;
        handler->extractOptFirstArg((char*)args, &nameStr, &levelStr);

        if (!levelStr)
            return false;

        Player* target;
        uint64 targetGuid;
        std::string target_name;
        if (!handler->extractPlayerTarget(nameStr, &target, &targetGuid, &target_name))
            return false;

        uint32 guildId = target ? target->GetGuildId() : Player::GetGuildIdFromDB(targetGuid);
        if (!guildId)
            return false;

        Guild* targetGuild = sGuildMgr->GetGuildById(guildId);
        if (!targetGuild)
            return false;

        int newLevel = atoi(levelStr);
        if (newLevel < 0)
            newLevel = 0;
        if (newLevel > 25)
            newLevel = 25;
        uint32 oldLevel = targetGuild->GetLevel();
        for (int i  = targetGuild->GetLevel(); i < newLevel; i++)
            targetGuild->LevelUp(oldLevel, target);

        return true;
    }

    static bool HandleGuildRenameCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string newGuildNameStr = args;
        if (newGuildNameStr.size() < 3)
        {
            handler->SendSysMessage("Guild name size too low.");
            return false;
        }

        Player* target = handler->getSelectedPlayer();
        if (!target)
            target = handler->GetSession()->GetPlayer();

        if (!target)
        {
            handler->SendSysMessage("No player found");
            return false;
        }

        uint32 guildId = target->GetGuildId();
        if (!guildId)
        {
            handler->PSendSysMessage("No guild found for player %s", target->GetName().c_str());
            return false;
        }

        Guild* targetGuild = sGuildMgr->GetGuildById(guildId);
        if (!targetGuild)
        {
            handler->PSendSysMessage("No guild found for player %s", target->GetName().c_str());
            return false;
        }

        std::string oldName = targetGuild->GetName();

        newGuildNameStr = my_escape_string(newGuildNameStr);

        if (sGuildMgr->GetGuildByName(newGuildNameStr))
        {
            handler->PSendSysMessage("Guild name %s already exist", newGuildNameStr.c_str());
            return false;
        }

        targetGuild->UpdateName(newGuildNameStr);
        /*WorldPacket data(SMSG_GUILD_RENAMED, 1);
            data << uint32(1);
            target->GetSession()->SendPacket(&data);*/

        handler->PSendSysMessage("Guild %s succesfully rename in %s", oldName.c_str(), newGuildNameStr.c_str());
        return true;
    }
};

void AddSC_guild_commandscript()
{
    new guild_commandscript();
}
