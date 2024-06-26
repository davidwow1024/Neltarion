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

/** \file
    \ingroup Trinityd
*/

#include <ace/Sig_Handler.h>

#include "Common.h"
#include "SystemConfig.h"
#include "SignalHandler.h"
#include "World.h"
#include "WorldRunnable.h"
#include "WorldSocket.h"
#include "WorldSocketMgr.h"
#include "Configuration/Config.h"
#include "Database/DatabaseEnv.h"
#include "Database/DatabaseWorkerPool.h"

#include "CliRunnable.h"
#include "Log.h"
#include "Master.h"
#include "RARunnable.h"
#include "TCSoap.h"
#include "Timer.h"
#include "Util.h"
#include "AuthSocket.h"
#include "RealmList.h"

#include "BigNumber.h"

#ifdef _WIN32
#include "ServiceWin32.h"
extern int m_ServiceStatus;
#endif

/// Handle worldservers's termination signals
class WorldServerSignalHandler : public Trinity::SignalHandler
{
    public:
        virtual void HandleSignal(int SigNum)
        {
            switch (SigNum)
            {
                case SIGINT:
                    World::StopNow(RESTART_EXIT_CODE);
                    break;
                case SIGTERM:
                #ifdef _WIN32
                case SIGBREAK:
                    if (m_ServiceStatus != 1)
                #endif /* _WIN32 */
                    World::StopNow(SHUTDOWN_EXIT_CODE);
                    break;
            }
        }
};

class FreezeDetectorRunnable : public ACE_Based::Runnable
{
public:
    FreezeDetectorRunnable() { _delaytime = 0; }
    uint32 m_loops, m_lastchange;
    uint32 w_loops, w_lastchange;
    uint32 _delaytime;
    void SetDelayTime(uint32 t) { _delaytime = t; }
    void run(void)
    {
        if (!_delaytime)
            return;
        TC_LOG_INFO("server.worldserver", "Starting up anti-freeze thread (%u seconds max stuck time)...", _delaytime/1000);
        m_loops = 0;
        w_loops = 0;
        m_lastchange = 0;
        w_lastchange = 0;
        while (!World::IsStopped())
        {
            ACE_Based::Thread::Sleep(1000);
            uint32 curtime = getMSTime();
            // normal work
            uint32 worldLoopCounter = World::m_worldLoopCounter.value();
            if (w_loops != worldLoopCounter)
            {
                w_lastchange = curtime;
                w_loops = worldLoopCounter;
            }
            // possible freeze
            else if (getMSTimeDiff(w_lastchange, curtime) > _delaytime)
            {
                TC_LOG_ERROR("server.worldserver", "World Thread hangs, kicking out server!");
                ASSERT(false);
            }
        }
        TC_LOG_INFO("server.worldserver", "Anti-freeze thread exiting without problems.");
    }
};

Master::Master()
{
}

Master::~Master()
{
}

/// Main function
int Master::Run()
{
    BigNumber seed1;
    seed1.SetRand(16 * 8);

    TC_LOG_INFO("server.worldserver", "%s (worldserver-daemon)", _FULLVERSION);
    TC_LOG_INFO("server.worldserver", "<Ctrl-C> to stop.\n");
	
	TC_LOG_INFO("server.worldserver", "  _   _      _ _   _      ____               ");
	TC_LOG_INFO("server.worldserver", " | \\ | | ___| | |_| |__  / ___|___  _ __ ___ ");
	TC_LOG_INFO("server.worldserver", " |  \\| |/ _ | | __| '_ \\| |   / _ \\| '__/ _ \\");
	TC_LOG_INFO("server.worldserver", " | |\\  |  __| | |_| | | | |__| (_) | | |  __/");
	TC_LOG_INFO("server.worldserver", " |_| \\_|\\___|_|\\__|_| |_|\\____\\___/|_|  \\___|");
	TC_LOG_INFO("server.worldserver", "              Project Nelth Development Team  ");
	TC_LOG_INFO("server.worldserver", "                                              ");

    /// worldserver PID file creation
    std::string pidfile = sConfigMgr->GetStringDefault("PidFile", "");
    if (!pidfile.empty())
    {
        uint32 pid = CreatePIDFile(pidfile);
        if (!pid)
        {
            TC_LOG_ERROR("server.worldserver", "Cannot create PID file %s.\n", pidfile.c_str());
            return 1;
        }

        TC_LOG_INFO("server.worldserver", "Daemon PID: %u\n", pid);
    }

    ///- Start the databases
    if (!_StartDB())
        return 1;

    // set server offline (not connectable)
    LoginDatabase.DirectPExecute("UPDATE realmlist SET flag = (flag & ~%u) | %u WHERE id = '%d'", REALM_FLAG_OFFLINE, REALM_FLAG_INVALID, realmID);

    ///- Initialize the World
    sWorld->SetInitialWorldSettings();

    ///- Initialize the signal handlers
    WorldServerSignalHandler SignalINT, SignalTERM;
    #ifdef _WIN32
    WorldServerSignalHandler SignalBREAK;
    #endif /* _WIN32 */

    ///- Register worldserver's signal handlers
    ACE_Sig_Handler Handler;
    Handler.register_handler(SIGINT, &SignalINT);
    Handler.register_handler(SIGTERM, &SignalTERM);
    #ifdef _WIN32
    Handler.register_handler(SIGBREAK, &SignalBREAK);
    #endif /* _WIN32 */

    ///- Launch WorldRunnable thread
    ACE_Based::Thread world_thread(new WorldRunnable);
    world_thread.setPriority(ACE_Based::Highest);

    ACE_Based::Thread* cliThread = NULL;

#ifdef _WIN32
    if (sConfigMgr->GetBoolDefault("Console.Enable", true) && (m_ServiceStatus == -1)/* need disable console in service mode*/)
#else
    if (sConfigMgr->GetBoolDefault("Console.Enable", true))
#endif
    {
        ///- Launch CliRunnable thread
        cliThread = new ACE_Based::Thread(new CliRunnable);
    }

    ACE_Based::Thread rar_thread(new RARunnable);

    ///- Handle affinity for multiple processors and process priority on Windows
    #ifdef _WIN32
    {
        HANDLE hProcess = GetCurrentProcess();

        uint32 Aff = sConfigMgr->GetIntDefault("UseProcessors", 0);
        if (Aff > 0)
        {
            ULONG_PTR appAff;
            ULONG_PTR sysAff;

            if (GetProcessAffinityMask(hProcess, &appAff, &sysAff))
            {
                ULONG_PTR curAff = Aff & appAff;            // remove non accessible processors

                if (!curAff)
                {
                    TC_LOG_ERROR("server.worldserver", "Processors marked in UseProcessors bitmask (hex) %x are not accessible for the worldserver. Accessible processors bitmask (hex): %x", Aff, appAff);
                }
                else
                {
                    if (SetProcessAffinityMask(hProcess, curAff))
                        TC_LOG_INFO("server.worldserver", "Using processors (bitmask, hex): %x", curAff);
                    else
                        TC_LOG_ERROR("server.worldserver", "Can't set used processors (hex): %x", curAff);
                }
            }
        }

        bool Prio = sConfigMgr->GetBoolDefault("ProcessPriority", false);

        //if (Prio && (m_ServiceStatus == -1)  /* need set to default process priority class in service mode*/)
        if (Prio)
        {
            if (SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
                TC_LOG_INFO("server.worldserver", "worldserver process priority class set to HIGH");
            else
                TC_LOG_ERROR("server.worldserver", "Can't set worldserver process priority class.");
        }
    }
    #endif
    //Start soap serving thread
    ACE_Based::Thread* soap_thread = NULL;

    if (sConfigMgr->GetBoolDefault("SOAP.Enabled", false))
    {
        TCSoapRunnable* runnable = new TCSoapRunnable();
        runnable->setListenArguments(sConfigMgr->GetStringDefault("SOAP.IP", "127.0.0.1"), uint16(sConfigMgr->GetIntDefault("SOAP.Port", 7878)));
        soap_thread = new ACE_Based::Thread(runnable);
    }

    ///- Start up freeze catcher thread
    if (uint32 freeze_delay = sConfigMgr->GetIntDefault("MaxCoreStuckTime", 0))
    {
        FreezeDetectorRunnable* fdr = new FreezeDetectorRunnable();
        fdr->SetDelayTime(freeze_delay * 1000);
        ACE_Based::Thread freeze_thread(fdr);
        freeze_thread.setPriority(ACE_Based::Highest);
    }

    ///- Launch the world listener socket
    uint16 wsport = uint16(sWorld->getIntConfig(CONFIG_PORT_WORLD));
    std::string bind_ip = sConfigMgr->GetStringDefault("BindIP", "0.0.0.0");

    if (sWorldSocketMgr->StartNetwork(wsport, bind_ip.c_str()) == -1)
    {
        TC_LOG_ERROR("server.worldserver", "Failed to start network");
        World::StopNow(ERROR_EXIT_CODE);
        // go down and shutdown the server
    }

    // set server online (allow connecting now)
    LoginDatabase.DirectPExecute("UPDATE realmlist SET flag = flag & ~%u, population = 0 WHERE id = '%u'", REALM_FLAG_INVALID, realmID);

    TC_LOG_INFO("server.worldserver", "%s (worldserver-daemon) ready...", _FULLVERSION);

    // when the main thread closes the singletons get unloaded
    // since worldrunnable uses them, it will crash if unloaded after master
    world_thread.wait();
    rar_thread.wait();

    if (soap_thread)
    {
        soap_thread->wait();
        soap_thread->destroy();
        delete soap_thread;
    }

    // set server offline
    LoginDatabase.DirectPExecute("UPDATE realmlist SET flag = flag | %u WHERE id = '%d'", REALM_FLAG_OFFLINE, realmID);

    ///- Clean database before leaving
    ClearOnlineAccounts();

    _StopDB();

    TC_LOG_INFO("server.worldserver", "Halting process...");

    if (cliThread)
    {
        #ifdef _WIN32

        // this only way to terminate CLI thread exist at Win32 (alt. way exist only in Windows Vista API)
        //_exit(1);
        // send keyboard input to safely unblock the CLI thread
        INPUT_RECORD b[5];
        HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        b[0].EventType = KEY_EVENT;
        b[0].Event.KeyEvent.bKeyDown = TRUE;
        b[0].Event.KeyEvent.uChar.AsciiChar = 'X';
        b[0].Event.KeyEvent.wVirtualKeyCode = 'X';
        b[0].Event.KeyEvent.wRepeatCount = 1;

        b[1].EventType = KEY_EVENT;
        b[1].Event.KeyEvent.bKeyDown = FALSE;
        b[1].Event.KeyEvent.uChar.AsciiChar = 'X';
        b[1].Event.KeyEvent.wVirtualKeyCode = 'X';
        b[1].Event.KeyEvent.wRepeatCount = 1;

        b[2].EventType = KEY_EVENT;
        b[2].Event.KeyEvent.bKeyDown = TRUE;
        b[2].Event.KeyEvent.dwControlKeyState = 0;
        b[2].Event.KeyEvent.uChar.AsciiChar = '\r';
        b[2].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        b[2].Event.KeyEvent.wRepeatCount = 1;
        b[2].Event.KeyEvent.wVirtualScanCode = 0x1c;

        b[3].EventType = KEY_EVENT;
        b[3].Event.KeyEvent.bKeyDown = FALSE;
        b[3].Event.KeyEvent.dwControlKeyState = 0;
        b[3].Event.KeyEvent.uChar.AsciiChar = '\r';
        b[3].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        b[3].Event.KeyEvent.wVirtualScanCode = 0x1c;
        b[3].Event.KeyEvent.wRepeatCount = 1;
        DWORD numb;
        WriteConsoleInput(hStdIn, b, 4, &numb);

        cliThread->wait();

        #else

        cliThread->destroy();

        #endif

        delete cliThread;
    }

    // for some unknown reason, unloading scripts here and not in worldrunnable
    // fixes a memory leak related to detaching threads from the module
    //UnloadScriptingModule();

    // Exit the process with specified return value
    return World::GetExitCode();
}

/// Initialize connection to the databases
bool Master::_StartDB()
{
    MySQL::Library_Init();

    std::string dbstring;
    uint8 async_threads, synch_threads;

    dbstring = sConfigMgr->GetStringDefault("WorldDatabaseInfo", "");
    if (dbstring.empty())
    {
        TC_LOG_ERROR("server.worldserver", "World database not specified in configuration file");
        return false;
    }

    async_threads = uint8(sConfigMgr->GetIntDefault("WorldDatabase.WorkerThreads", 1));
    if (async_threads < 1 || async_threads > 32)
    {
        TC_LOG_ERROR("server.worldserver", "World database: invalid number of worker threads specified. "
            "Please pick a value between 1 and 32.");
        return false;
    }

    synch_threads = uint8(sConfigMgr->GetIntDefault("WorldDatabase.SynchThreads", 1));
    ///- Initialise the world database
    if (!WorldDatabase.Open(dbstring, async_threads, synch_threads))
    {
        TC_LOG_ERROR("server.worldserver", "Cannot connect to world database %s", dbstring.c_str());
        return false;
    }

    ///- Get character database info from configuration file
    dbstring = sConfigMgr->GetStringDefault("CharacterDatabaseInfo", "");
    if (dbstring.empty())
    {
        TC_LOG_ERROR("server.worldserver", "Character database not specified in configuration file");
        return false;
    }

    async_threads = uint8(sConfigMgr->GetIntDefault("CharacterDatabase.WorkerThreads", 1));
    if (async_threads < 1 || async_threads > 32)
    {
        TC_LOG_ERROR("server.worldserver", "Character database: invalid number of worker threads specified. "
            "Please pick a value between 1 and 32.");
        return false;
    }

    synch_threads = uint8(sConfigMgr->GetIntDefault("CharacterDatabase.SynchThreads", 2));

    ///- Initialise the Character database
    if (!CharacterDatabase.Open(dbstring, async_threads, synch_threads))
    {
        TC_LOG_ERROR("server.worldserver", "Cannot connect to Character database %s", dbstring.c_str());
        return false;
    }

    ///- Get login database info from configuration file
    dbstring = sConfigMgr->GetStringDefault("LoginDatabaseInfo", "");
    if (dbstring.empty())
    {
        TC_LOG_ERROR("server.worldserver", "Login database not specified in configuration file");
        return false;
    }

    async_threads = uint8(sConfigMgr->GetIntDefault("LoginDatabase.WorkerThreads", 1));
    if (async_threads < 1 || async_threads > 32)
    {
        TC_LOG_ERROR("server.worldserver", "Login database: invalid number of worker threads specified. "
            "Please pick a value between 1 and 32.");
        return false;
    }

    synch_threads = uint8(sConfigMgr->GetIntDefault("LoginDatabase.SynchThreads", 1));
    ///- Initialise the login database
    if (!LoginDatabase.Open(dbstring, async_threads, synch_threads))
    {
        TC_LOG_ERROR("server.worldserver", "Cannot connect to login database %s", dbstring.c_str());
        return false;
    }

    ///- Get the realm Id from the configuration file
    realmID = sConfigMgr->GetIntDefault("RealmID", 0);
    if (!realmID)
    {
        TC_LOG_ERROR("server.worldserver", "Realm ID not defined in configuration file");
        return false;
    }
    TC_LOG_INFO("server.worldserver", "Realm running as realm ID %d", realmID);
    sWorld->setRealmID(realmID);

    ///- Clean the database before starting
    ClearOnlineAccounts();

    ///- Insert version info into DB
    WorldDatabase.PExecute("UPDATE version SET core_version = '%s', core_revision = '%s'", _DATE, _HASH);        // One-time query

    sWorld->LoadDBVersion();

    TC_LOG_INFO("server.worldserver", "Using World DB: %s", sWorld->GetDBVersion());
    return true;
}

void Master::_StopDB()
{
    CharacterDatabase.Close();
    WorldDatabase.Close();
    LoginDatabase.Close();

    MySQL::Library_End();
}

/// Clear 'online' status for all accounts with characters in this realm
void Master::ClearOnlineAccounts()
{
    // Reset online status for all accounts with characters on the current realm
    LoginDatabase.DirectPExecute("UPDATE account SET online = 0 WHERE online > 0 AND id IN (SELECT acctid FROM realmcharacters WHERE realmid = %d)", realmID);

    // Reset online status for all characters
    CharacterDatabase.DirectExecute("UPDATE characters SET online = 0 WHERE online <> 0");

    // Battleground instance ids reset at server restart
    CharacterDatabase.DirectExecute("UPDATE character_battleground_data SET instanceId = 0");
}
