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
Name: honor_commandscript
%Complete: 100
Comment: All honor related commands
Category: commandscripts
EndScriptData */

#include "Chat.h"
#include "Language.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

class honor_commandscript : public CommandScript
{
public:
    honor_commandscript() : CommandScript("honor_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> honorAddCommandTable =
        {
            { "kill",           SEC_CONSOLE,     false, &HandleHonorAddKillCommand,         "" },
            { "",               SEC_CONSOLE,     false, &HandleHonorAddCommand,             "" },
        };

        static std::vector<ChatCommand> honorCommandTable =
        {
            { "add",            SEC_CONSOLE,     false, NULL,               "", honorAddCommandTable },
            { "update",         SEC_CONSOLE,     false, &HandleHonorUpdateCommand,          "" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "honor",          SEC_CONSOLE,     false, NULL,                  "", honorCommandTable },
        };
        return commandTable;
    }

    static bool HandleHonorAddCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, 0))
            return false;

        uint32 amount = (uint32)atoi(args);
        target->RewardHonor(NULL, 1, amount);
        return true;
    }

    static bool HandleHonorAddKillCommand(ChatHandler* handler, char const* /*args*/)
    {
        Unit* target = handler->getSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (target->GetTypeId() == TYPEID_PLAYER && handler->HasLowerSecurity((Player*)target, 0))
            return false;

        handler->GetSession()->GetPlayer()->RewardHonor(target, 1);
        return true;
    }

    static bool HandleHonorUpdateCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (handler->HasLowerSecurity(target, 0))
            return false;

        target->UpdateHonorFields();
        return true;
    }
};

void AddSC_honor_commandscript()
{
    new honor_commandscript();
}
