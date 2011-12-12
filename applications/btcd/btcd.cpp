// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//#include "headers.h"
#include "btc/strlcpy.h"

#include "btcNode/db.h"
#include "btcNode/net.h"

#include "btcHTTP/Server.h"

#include "proxy.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <exception>
#include <signal.h>

using namespace std;
using namespace boost;

typedef void wxWindow;
#define wxYES                   0x00000002
#define wxOK                    0x00000004
#define wxNO                    0x00000008
#define wxYES_NO                (wxYES|wxNO)
#define wxCANCEL                0x00000010
#define wxAPPLY                 0x00000020
#define wxCLOSE                 0x00000040
#define wxOK_DEFAULT            0x00000000
#define wxYES_DEFAULT           0x00000000
#define wxNO_DEFAULT            0x00000080
#define wxCANCEL_DEFAULT        0x80000000
#define wxICON_EXCLAMATION      0x00000100
#define wxICON_HAND             0x00000200
#define wxICON_WARNING          wxICON_EXCLAMATION
#define wxICON_ERROR            wxICON_HAND
#define wxICON_QUESTION         0x00000400
#define wxICON_INFORMATION      0x00000800
#define wxICON_STOP             wxICON_HAND
#define wxICON_ASTERISK         wxICON_INFORMATION
#define wxICON_MASK             (0x00000100|0x00000200|0x00000400|0x00000800)
#define wxFORWARD               0x00001000
#define wxBACKWARD              0x00002000
#define wxRESET                 0x00004000
#define wxHELP                  0x00008000
#define wxMORE                  0x00010000
#define wxSETUP                 0x00020000

inline int MyMessageBox(const std::string& message, const std::string& caption="Message", int style=wxOK, wxWindow* parent=NULL, int x=-1, int y=-1)
{
    printf("%s: %s\n", caption.c_str(), message.c_str());
    fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    return 4;
}
#define wxMessageBox  MyMessageBox

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

void ExitTimeout(void* parg)
{
#ifdef _WIN32
    Sleep(5000);
    ExitProcess(0);
#endif
}

class CShutdown : public CHandler
{
    virtual void operator()()
    {
        static CCriticalSection cs_Shutdown;
        static bool fTaken;
        bool fFirstThread;
        CRITICAL_BLOCK(cs_Shutdown)
        {
            fFirstThread = !fTaken;
            fTaken = true;
        }
        static bool fExit;
        if (fFirstThread)
        {
            fShutdown = true;
            nTransactionsUpdated++;
            DBFlush(false);
            StopNode();
            DBFlush(true);
            boost::filesystem::remove(GetPidFile());
            //        UnregisterWallet(pwalletMain);
            //        delete pwalletMain;
            CreateThread(ExitTimeout, NULL);
            Sleep(50);
            printf("Bitcoin exiting\n\n");
            fExit = true;
            exit(0);
        }
        else
        {
            while (!fExit)
                Sleep(500);
            Sleep(100);
            ExitThread(0);
        }
    }
    
};

void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//

bool AppInit(int argc, char* argv[]);
bool AppInit2(int argc, char* argv[]);

#ifndef GUI
int main(int argc, char* argv[])
{
    bool fRet = false;
    fRet = AppInit(argc, argv);

    if (fRet && fDaemon)
        return 0;

    return 1;
}
#endif

bool AppInit(int argc, char* argv[])
{
    bool fRet = false;
    try
    {
        fRet = AppInit2(argc, argv);
    }
    catch (std::exception& e) {
        PrintException(&e, "AppInit()");
    } catch (...) {
        PrintException(NULL, "AppInit()");
    }
    if (!fRet)
        Shutdown(NULL);
    return fRet;
}

bool AppInit2(int argc, char* argv[])
{
    CShutdown shutdown;
    CMain::instance().registerShutdownHandler(shutdown);
    
#ifdef _MSC_VER
    // Turn off microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, ctrl-c
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifndef _WIN32
    umask(077);
#endif
#ifndef _WIN32
    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
#endif
    
    //
    // Parameters
    //
    ParseParameters(argc, argv);

    if (mapArgs.count("-datadir"))
    {
        if (filesystem::is_directory(filesystem::system_complete(mapArgs["-datadir"])))
        {
            filesystem::path pathDataDir = filesystem::system_complete(mapArgs["-datadir"]);
            strlcpy(pszSetDataDir, pathDataDir.string().c_str(), sizeof(pszSetDataDir));
        }
        else
        {
            fprintf(stderr, "Error: Specified directory does not exist\n");
            Shutdown(NULL);
        }
    }


    ReadConfigFile(mapArgs, mapMultiArgs); // Must be done after processing datadir

    if (mapArgs.count("-?") || mapArgs.count("--help"))
    {
        string strUsage = string() +
          _("Bitcoin version") + " " + FormatFullVersion() + "\n\n" +
          _("Usage:") + "\t\t\t\t\t\t\t\t\t\t\n" +
            "  bitcoin [options]                   \t  " + "\n" +
            "  bitcoin [options] <command> [params]\t  " + _("Send command to -server or bitcoind\n") +
            "  bitcoin [options] help              \t\t  " + _("List commands\n") +
            "  bitcoin [options] help <command>    \t\t  " + _("Get help for a command\n") +
          _("Options:\n") +
            "  -conf=<file>     \t\t  " + _("Specify configuration file (default: bitcoin.conf)\n") +
            "  -pid=<file>      \t\t  " + _("Specify pid file (default: bitcoind.pid)\n") +
            "  -gen             \t\t  " + _("Generate coins\n") +
            "  -gen=0           \t\t  " + _("Don't generate coins\n") +
            "  -min             \t\t  " + _("Start minimized\n") +
            "  -datadir=<dir>   \t\t  " + _("Specify data directory\n") +
            "  -timeout=<n>     \t  "   + _("Specify connection timeout (in milliseconds)\n") +
            "  -proxy=<ip:port> \t  "   + _("Connect through socks4 proxy\n") +
            "  -dns             \t  "   + _("Allow DNS lookups for addnode and connect\n") +
            "  -addnode=<ip>    \t  "   + _("Add a node to connect to\n") +
            "  -connect=<ip>    \t\t  " + _("Connect only to the specified node\n") +
            "  -nolisten        \t  "   + _("Don't accept connections from outside\n") +
#ifdef USE_UPNP
#if USE_UPNP
            "  -noupnp          \t  "   + _("Don't attempt to use UPnP to map the listening port\n") +
#else
            "  -upnp            \t  "   + _("Attempt to use UPnP to map the listening port\n") +
#endif
#endif
            "  -paytxfee=<amt>  \t  "   + _("Fee per KB to add to transactions you send\n") +
#ifdef GUI
            "  -server          \t\t  " + _("Accept command line and JSON-RPC commands\n") +
#endif
#ifndef _WIN32
            "  -daemon          \t\t  " + _("Run in the background as a daemon and accept commands\n") +
#endif
            "  -testnet         \t\t  " + _("Use the test network\n") +
            "  -rpcuser=<user>  \t  "   + _("Username for JSON-RPC connections\n") +
            "  -rpcpassword=<pw>\t  "   + _("Password for JSON-RPC connections\n") +
            "  -rpcport=<port>  \t\t  " + _("Listen for JSON-RPC connections on <port> (default: 8332)\n") +
            "  -rpcallowip=<ip> \t\t  " + _("Allow JSON-RPC connections from specified IP address\n") +
            "  -rpcconnect=<ip> \t  "   + _("Send commands to node running on <ip> (default: 127.0.0.1)\n") +
        "  -keypool=<n>     \t  "   + _("Set key pool size to <n> (default: 100)\n");// +
//            "  -rescan          \t  "   + _("Rescan the block chain for missing wallet transactions\n");

#ifdef USE_SSL
        strUsage += string() +
            _("\nSSL options: (see the Bitcoin Wiki for SSL setup instructions)\n") +
            "  -rpcssl                                \t  " + _("Use OpenSSL (https) for JSON-RPC connections\n") +
            "  -rpcsslcertificatechainfile=<file.cert>\t  " + _("Server certificate file (default: server.cert)\n") +
            "  -rpcsslprivatekeyfile=<file.pem>       \t  " + _("Server private key (default: server.pem)\n") +
            "  -rpcsslciphers=<ciphers>               \t  " + _("Acceptable ciphers (default: TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH)\n");
#endif

        strUsage += string() +
            "  -?               \t\t  " + _("This help message\n");

#if defined(_WIN32) && defined(GUI)
        // Tabs make the columns line up in the message box
        wxMessageBox(strUsage, "Bitcoin", wxOK);
#else
        // Remove tabs
        strUsage.erase(std::remove(strUsage.begin(), strUsage.end(), '\t'), strUsage.end());
        fprintf(stderr, "%s", strUsage.c_str());
#endif
        return false;
    }

    fDebug = GetBoolArg("-debug");
    fAllowDNS = GetBoolArg("-dns");

#ifndef _WIN32
    fDaemon = GetBoolArg("-daemon");
#else
    fDaemon = false;
#endif

    if (fDaemon)
        fServer = true;
    else
        fServer = GetBoolArg("-server");

    /* force fServer when running without GUI */
#ifndef GUI
    fServer = true;
#endif

    fPrintToConsole = GetBoolArg("-printtoconsole");
    fPrintToDebugger = GetBoolArg("-printtodebugger");

    fTestNet = GetBoolArg("-testnet");
    bool fTOR = (fUseProxy && addrProxy.getPort() == htons(9050));
    fNoListen = GetBoolArg("-nolisten") || fTOR;
    fLogTimestamps = GetBoolArg("-logtimestamps");

    for (int i = 1; i < argc; i++)
        if (!IsSwitchChar(argv[i][0]))
            fCommandLine = true;

#ifndef _WIN32
    if (fDaemon)
    {
        // Daemonize
        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
            return false;
        }
        if (pid > 0)
        {
            CreatePidFile(GetPidFile(), pid);
            return true;
        }

        pid_t sid = setsid();
        if (sid < 0)
            fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
    }
#endif

    if (!fDebug && !pszSetDataDir[0])
        ShrinkDebugFile();
    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    printf("Bitcoin version %s\n", FormatFullVersion().c_str());
#ifdef GUI
    printf("OS version %s\n", ((string)wxGetOsDescription()).c_str());
    printf("System default language is %d %s\n", g_locale.GetSystemLanguage(), ((string)g_locale.GetSysName()).c_str());
    printf("Language file %s (%s)\n", (string("locale/") + (string)g_locale.GetCanonicalName() + "/LC_MESSAGES/bitcoin.mo").c_str(), ((string)g_locale.GetLocale()).c_str());
#endif
    printf("Default data directory %s\n", GetDefaultDataDir().c_str());

    if (GetBoolArg("-loadblockindextest"))
    {
        CTxDB txdb("r");
        txdb.LoadBlockIndex();
        PrintBlockTree();
        return false;
    }

    //
    // Limit to single instance per user
    // Required to protect the database files if we're going to keep deleting log.*
    //
#if defined(_WIN32) && defined(GUI)
    // wxSingleInstanceChecker doesn't work on Linux
    wxString strMutexName = wxString("bitcoin_running.") + getenv("HOMEPATH");
    for (int i = 0; i < strMutexName.size(); i++)
        if (!isalnum(strMutexName[i]))
            strMutexName[i] = '.';
    wxSingleInstanceChecker* psingleinstancechecker = new wxSingleInstanceChecker(strMutexName);
    if (psingleinstancechecker->IsAnotherRunning())
    {
        printf("Existing instance found\n");
        unsigned int nStart = GetTime();
        loop
        {
            // Show the previous instance and exit
            HWND hwndPrev = FindWindowA("wxWindowClassNR", "Bitcoin");
            if (hwndPrev)
            {
                if (IsIconic(hwndPrev))
                    ShowWindow(hwndPrev, SW_RESTORE);
                SetForegroundWindow(hwndPrev);
                return false;
            }

            if (GetTime() > nStart + 60)
                return false;

            // Resume this instance if the other exits
            delete psingleinstancechecker;
            Sleep(1000);
            psingleinstancechecker = new wxSingleInstanceChecker(strMutexName);
            if (!psingleinstancechecker->IsAnotherRunning())
                break;
        }
    }
#endif

    // Make sure only a single bitcoin process is using the data directory.
    string strLockFile = GetDataDir() + "/.lock";
    FILE* file = fopen(strLockFile.c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(strLockFile.c_str());
    if (!lock.try_lock())
    {
        wxMessageBox(strprintf(_("Cannot obtain a lock on data directory %s.  Bitcoin is probably already running."), GetDataDir().c_str()), "Bitcoin");
        return false;
    }

    // Bind to the port early so we can tell if another instance is already running.
    string strErrors;
    if (!fNoListen)
    {
        if (!BindListenPort(strErrors))
        {
            wxMessageBox(strErrors, "Bitcoin");
            return false;
        }
    }

    //
    // Load data files
    //
    if (fDaemon)
        fprintf(stdout, "bitcoin server starting\n");
    strErrors = "";
    int64 nStart;

    printf("Loading addresses...\n");
    nStart = GetTimeMillis();

    
    printf("Loading block index...\n");
    nStart = GetTimeMillis();
    if (!LoadBlockIndex())
        strErrors += _("Error loading blkindex.dat      \n");
    printf(" block index %15"PRI64d"ms\n", GetTimeMillis() - nStart);
    
    printf("Done loading\n");

        //// debug print
        printf("mapBlockIndex.size() = %d\n",   mapBlockIndex.size());
        printf("nBestHeight = %d\n",            nBestHeight);

    if (!strErrors.empty())
    {
        wxMessageBox(strErrors, "Bitcoin", wxOK | wxICON_ERROR);
        return false;
    }

    //
    // Parameters
    //
    if (GetBoolArg("-printblockindex") || GetBoolArg("-printblocktree"))
    {
        PrintBlockTree();
        return false;
    }

    if (mapArgs.count("-timeout"))
    {
        int nNewTimeout = GetArg("-timeout", 5000);
        if (nNewTimeout > 0 && nNewTimeout < 600000)
            nConnectTimeout = nNewTimeout;
    }

    if (mapArgs.count("-printblock"))
    {
        string strMatch = mapArgs["-printblock"];
        int nFound = 0;
        for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
        {
            uint256 hash = (*mi).first;
            if (strncmp(hash.toString().c_str(), strMatch.c_str(), strMatch.size()) == 0)
            {
                CBlockIndex* pindex = (*mi).second;
                CBlock block;
                __blockFile.readFromDisk(block, pindex);
                block.BuildMerkleTree();
                block.print();
                printf("\n");
                nFound++;
            }
        }
        if (nFound == 0)
            printf("No blocks matching %s were found\n", strMatch.c_str());
        return false;
    }

    fGenerateBitcoins = GetBoolArg("-gen");

    if (mapArgs.count("-proxy"))
    {
        fUseProxy = true;
        addrProxy = Endpoint(mapArgs["-proxy"]);
        if (!addrProxy.isValid())
        {
            wxMessageBox(_("Invalid -proxy address"), "Bitcoin");
            return false;
        }
    }
/*
    if (mapArgs.count("-addnode"))
    {
        BOOST_FOREACH(string hostname, mapMultiArgs["-addnode"])
        {
            Endpoint ep(hostname, fAllowDNS);
            hostname.setTime(0); // so it won't relay unless successfully connected
            if (hostname.isValid())
                AddEndpoint(hostname);
        }
    }
*/
    if (GetBoolArg("-nodnsseed"))
        printf("DNS seeding disabled\n");
    else
        DNSAddressSeed();

    if (mapArgs.count("-paytxfee"))
    {
        if (!ParseMoney(mapArgs["-paytxfee"], nTransactionFee))
        {
            wxMessageBox(_("Invalid amount for -paytxfee=<amount>"), "Bitcoin");
            return false;
        }
        if (nTransactionFee > 0.25 * COIN)
            wxMessageBox(_("Warning: -paytxfee is set very high.  This is the transaction fee you will pay if you send a transaction."), "Bitcoin", wxOK | wxICON_EXCLAMATION);
    }

    if (fHaveUPnP)
    {
#if USE_UPNP
    if (GetBoolArg("-noupnp"))
        fUseUPnP = false;
#else
    if (GetBoolArg("-upnp"))
        fUseUPnP = true;
#endif
    }

    if (!BlockFile::checkDiskSpace())
        return false;

    RandAddSeedPerfmon();

    if (!CreateThread(StartNode, NULL))
        wxMessageBox("Error: CreateThread(StartNode) failed", "Bitcoin");

    // startup the node
    // Node n(db_location, accept_incoming_connections, use_upnp...);
    // n.start(); // now n runs in a thread communicating with other peers, and with the database
    // Node exposes an interface: 
    //  bestHeight()
    //  postTransaction(..)
    //  getCredits(..)
    //  getDebits(..)
    //  getTransaction(..)
    //  registerNotification(..)
    //  other ?
    
    try {
        // Initialise the HTTP Server.
        
        Server s("0.0.0.0", "9332", filesystem::initial_path().string());
        // register some kind of exit handler from n
        s.registerMethod(method_ptr(new GetDebit)); // should be GetDebit(n)
        s.registerMethod(method_ptr(new GetCredit));
        s.registerMethod(method_ptr(new GetTxDetails));        
        
        /*
         mapCallTable.insert(make_pair("gettxmaturity",          &gettxmaturity));
         mapCallTable.insert(make_pair("getvalue",               &getvalue));
         //    mapCallTable.insert(make_pair("getdebit",               &getdebit));
         //    mapCallTable.insert(make_pair("getcredit",              &getcredit));
         mapCallTable.insert(make_pair("getcoins",               &getcoins));
         //    mapCallTable.insert(make_pair("gettxdetails",           &gettxdetails));
         mapCallTable.insert(make_pair("posttx",                 &posttx));
         mapCallTable.insert(make_pair("checkvalue",                 &checkvalue));
*/
        
        printf("ThreadProxyServer started\n");
        
        // Run the server until stopped.
        s.run();
    }
    catch (std::exception& e) {
        cerr << "HTTP Server exception: " << e.what() << endl;
    }
    catch (...) {
        cerr << "HTTP Server exception: " << endl;
    }

    // kill the node!
    
    return true;
}
