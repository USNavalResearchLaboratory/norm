#include "protoApp.h"
#include "protoNet.h"
#include "norp.h"
#include "norpVersion.h"

#include <stdio.h>  // for stderr
#include <stdlib.h>  // for atoi()

class NorpApp : public ProtoApp
{
    public:
        NorpApp();
        ~NorpApp();
  
        // Required verrides from ProtoApp base
        bool OnStartup(int argc, const char*const* argv);
        bool ProcessCommands(int argc, const char*const* argv);
        void OnShutdown();
        
    private:
        enum CmdType {CMD_INVALID, CMD_ARG, CMD_NOARG};
        static CmdType GetCmdType(const char* string);
        bool OnCommand(const char* cmd, const char* val);        
        void Usage();
  
        static const char* const CMD_LIST[];
  
        Norp        norp;
        bool        norm_enable;
  
}; // end class NorpApp

// Our application instance 
PROTO_INSTANTIATE_APP(NorpApp) 
        
        
NorpApp::NorpApp()
 : norp(dispatcher), norm_enable(true)
{
}

NorpApp::~NorpApp()
{
    norp.StopServer();
}

void NorpApp::Usage()
{
    fprintf(stderr, "Usage: norp [interface <ifaceName>][address <publicAddr>][sport <socksPort>][port <norpPort>]\n"
                    "            [norm {on|off}][id <normId>][nport <normPort>][cce | ccl | rate <bits/sec>]\n"
                    "            [limit <bits/sec>][persist <seconds>][segment <segmentSize>]\n"
                    "            [correspondent <remoteNorpAddr>][forward <tcpPort>,<destAddr>/<destPort>[,<remoteNorpAddr>]]\n"
                    "            [version][debug <level>][trace][dlog <debugLog>][lport <localNorpPort>][rport <remoteNorpPort>]\n");
}

const char* const NorpApp::CMD_LIST[] =
{
    "+interface",       // "public facing" interface (its address is used for bindings)
    "+address",         // explicit "public facing" address to use for bindings
    "+sport",           // SOCKS server port (7000 by default)
    "+port",            // NORP UDP signaling port number (7001 by default)
    "+norm",            // "on" or "off" to enable/disable NORM proxying ("on" by default)
    "+id",              // set local NormNodeId (auto set from IP address by default)       
    "+nport",           // NORM port (UDP) (7002 by default)
    "-cce",             // Use NORM-CCE instead of NORM-CC
    "-ccl",             // Use NORM-CCL instead of NORM-CC
    "+rate",            // set fixed transmit rate (no congestion control)
    "+limit",           // set  _cumulative_ NORP transmit rate limit
    "+segment",         // Set NORM packet segment size (impacts MTU of NORM packets, UDP packets w/ (40 + <segmentSize>) bytes of payload)
    "+persist",         // <seconds> how long to persist NORM data delivery to receiver after TCP socket closure
    "+debug",           // set debug level
    "-trace",           // enables NORM protocol packet send/recv trace in debug output
    "+dlog",            // specify a file for debug logging
    "+lport",           // "local" NORP port (for loopback debugging)
    "+rport",           // "remote" NORP port (for loopback debugging)
    "+forward",         // <port>,<dstAddr>/<dstPort>[,<remoteNorpAddr>] to set up a preset proxy TCP listener
    "+correspondent",   // <remoteNorpAddr> to route proxied connections through remote NORP proxy at given addr
    "-version",         // print version number and exit
    NULL
};
    
NorpApp::CmdType NorpApp::GetCmdType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    unsigned int len = strlen(cmd);
    bool matched = false;
    CmdType type = CMD_INVALID;
    const char* const* nextCmd = CMD_LIST;
    while (*nextCmd)
    {
        if (!strncmp(cmd, *nextCmd+1, len))
        {
            if (matched)
            {
                // ambiguous command (command should match only once)
                return CMD_INVALID;
            }
            else
            {
                matched = true;   
                if ('+' == *nextCmd[0])
                    type = CMD_ARG;
                else
                    type = CMD_NOARG;
            }
        }
        nextCmd++;
    }
    return type; 
}  // end NorpApp::GetCmdType()

bool NorpApp::OnStartup(int argc, const char*const* argv)
{
    if (!ProcessCommands(argc, argv))
    {
        PLOG(PL_ERROR, "NorpApp::OnStartup() error: bad command line\n");
        Usage();
        return false;
    }  
    // If a proxy address wasn't set, use "default" local address for host
    if (!norp.GetProxyAddress().IsValid())
    {
        ProtoAddress theAddr;
        if (theAddr.ResolveLocalAddress())
            norp.SetProxyAddress(theAddr);
        else
            PLOG(PL_WARN, "NorpApp::OnStartup() warning: proxy address undetermined!\n");
    }
    PLOG(PL_INFO, "norp: starting proxy with address %s\n", norp.GetProxyAddress().GetHostString());
    
    if (!norp.StartServer(norm_enable))
    {
        PLOG(PL_ERROR, "NorpApp::OnStartup() error: norp server startup failure!\n");
        return false;
    }
    return true;
}  // end NorpApp::OnStartup()

void NorpApp::OnShutdown()
{
    norp.StopServer();
    CloseDebugLog();
}  // end Norp::OnShutdown()

bool NorpApp::ProcessCommands(int argc, const char*const* argv)
{
    // Dispatch command-line commands to our OnCommand() method
    int i = 1;
    while ( i < argc)
    {
        // Is it a class NorpApp command?
        switch (GetCmdType(argv[i]))
        {
            case CMD_INVALID:
            {
                PLOG(PL_ERROR, "NorpApp::ProcessCommands() Invalid command:%s\n", argv[i]);
                return false;
            }
            case CMD_NOARG:
                if (!OnCommand(argv[i], NULL))
                {
                    PLOG(PL_ERROR, "NorpApp::ProcessCommands() ProcessCommand(%s) error\n", argv[i]);
                    return false;
                }
                i++;
                break;
            case CMD_ARG:
                if (!OnCommand(argv[i], argv[i+1]))
                {
                    PLOG(PL_ERROR, "NorpApp::ProcessCommands() ProcessCommand(%s, %s) error\n", argv[i], argv[i+1]);
                    return false;
                }
                i += 2;
                break;
        }
    }
    return true;  
}  // end NorpApp::ProcessCommands()

bool NorpApp::OnCommand(const char* cmd, const char* val)   
{
    // (TBD) move command processing into Mgen class ???
    CmdType type = GetCmdType(cmd);
    ASSERT(CMD_INVALID != type);
    unsigned int len = strlen(cmd);
    if ((CMD_ARG == type) && !val)
    {
        PLOG(PL_ERROR, "NorpApp::ProcessCommand(%s) missing argument\n", cmd);
        return false;
    }
    else if (!strncmp("version", cmd, len))
    {
        fprintf(stdout, "norp version %s\n", NORP_VERSION);
        exit(0);
    }
    else if (!strncmp("debug", cmd, len))
    {
        SetDebugLevel(atoi(val));
    }
    else if (!strncmp("trace", cmd, len))
    {
        norp.SetNormTrace(true);
    }
    else if (!strncmp("dlog", cmd, len))
    {
        OpenDebugLog(val);
    }
    else if (!strncmp("interface", cmd, len))
    {
        ProtoAddress ifaceAddr;
        if (!ProtoNet::GetInterfaceAddress(val, ProtoAddress::IPv4, ifaceAddr))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(interface) error: unable to get address for interface \"%s\"\n", val);
            return false;
        }
        norp.SetProxyAddress(ifaceAddr);
    }
    else if (!strncmp("address", cmd, len))
    {
        ProtoAddress theAddr;
        if (!theAddr.ResolveFromString(val))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(interface) error: invalid address \"%s\"\n", val);
            return false;
        }
        norp.SetProxyAddress(theAddr);
    }
    else if (!strncmp("sport", cmd, len))
    {
        norp.SetSocksPort(atoi(val));
    }
    else if (!strncmp("port", cmd, len))
    {
        UINT16 norpPort = atoi(val);
        norp.SetLocalNorpPort(norpPort);
        norp.SetRemoteNorpPort(norpPort);
    }
    else if (!strncmp("norm", cmd, len))
    {
        if (0 == strcmp("on", val))
            norm_enable = true;
        else
            norm_enable = false;
    }
    else if (!strncmp("id", cmd, len))
    {
        norp.SetNormNodeId(atoi(val));
    }
    else if (!strncmp("nport", cmd, len))
    {
        norp.SetNormPort(atoi(val));
    }
    else if (!strncmp("cce", cmd, len))
    {
        norp.SetNormCC(Norp::NORM_CCE);
    }
    else if (!strncmp("ccl", cmd, len))
    {
        norp.SetNormCC(Norp::NORM_CCL);
    }
    else if (!strncmp("rate", cmd, len))
    {
        double txRate;
        if (1 != sscanf(val, "%lf", &txRate))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(rate) error: invalid rate \"%s\" bps\n", val);
            return false;
        }
        norp.SetNormCC(Norp::NORM_FIXED);
        norp.SetNormTxRate(txRate);
    }
    else if (!strncmp("limit", cmd, len))
    {
        double txLimit;
        if (1 != sscanf(val, "%lf", &txLimit))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(limit) error: invalid limit rate \"%s\" bps\n", val);
            return false;
        }
        norp.SetNormTxLimit(txLimit);
    }
    else if (!strncmp("segment", cmd, len))
    {
        UINT16 segmentSize;
        if (1 != sscanf(val, "%hu", &segmentSize))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(segment) error: invalid segment size \"%s\" bps\n", val);
            return false;
        }
        norp.SetNormSegmentSize(segmentSize);
    }
    else if (!strncmp("lport", cmd, len))
    {
        norp.SetLocalNorpPort(atoi(val));
    }
    else if (!strncmp("rport", cmd, len))
    {
        norp.SetRemoteNorpPort(atoi(val));
    }
    else if (!strncmp("forward", cmd, len))
    {
        // <port>,<dstAddr>/<dstPort>[,<remoteNorpAddr>]
        char* text = new char[strlen(val) + 1];  // copy so we can slice and dice
        if (NULL == text)
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s) new char[] error: %s\n", cmd, GetErrorString());
            return false;
        }        
        strcpy(text, val);
        char* dstAddrPtr = strchr(text, ',');
        if (NULL == dstAddrPtr)
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: missing <dstAddr>!\n", cmd, val);
            delete[] text;
            return false;
        }
        *dstAddrPtr++ = '\0';
        char* dstPortPtr = strchr(dstAddrPtr, '/');
        if (NULL == dstPortPtr)
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: missing <dstPort>!\n", cmd, val);
            delete[] text;
            return false;
        }
        UINT16 tcpPort;
        int result = sscanf(text, "%hu", &tcpPort);
        if ((1 != result) || (0 == tcpPort))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: invalid <port> \"%s\"!\n", cmd, val, text);
            delete[] text;
            return false;
        }
        *dstPortPtr++ = '\0';
        char* norpAddrPtr = strchr(dstPortPtr, ',');
        if (NULL != norpAddrPtr) *norpAddrPtr++ = '\0';
        ProtoAddress dstAddr;
        if (!dstAddr.ResolveFromString(dstAddrPtr))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: invalid <dstAddr> \"%s\"!\n", cmd, val, dstAddrPtr);
            delete[] text;
            return false;
        }
        UINT16 dstPort;
        result = sscanf(dstPortPtr, "%hu", &dstPort);
        if ((1 != result) || (0 == dstPort))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: invalid <dstPort> \"%s\"!\n", cmd, val, text);
            delete[] text;
            return false;
        }
        dstAddr.SetPort(dstPort);
        ProtoAddress norpAddr;
        if (NULL != norpAddrPtr)
        {
            if (!norpAddr.ResolveFromString(norpAddrPtr))
            {
                PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: invalid <norpAddr> \"%s\"!\n", cmd, val, norpAddrPtr);
                delete[] text;
                return false;
            }
        }
        if (!norp.AddPreset(tcpPort, dstAddr, norpAddr))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: unable to add preset!\n", cmd, val);
            delete[] text;
            return false;
        }
        delete[] text;
    }
    else if (!strncmp("correspondent", cmd, len))
    {
        // <remoteNorpAddr>
        ProtoAddress norpAddr;
        if (!norpAddr.ResolveFromString(val))
        {
            PLOG(PL_ERROR, "NorpApp::OnCommand(%s, %s) error: invalid <remoteNorpAddr>!\n", cmd, val);
            return false;
        }
        norp.SetRemoteNorpAddress(norpAddr);
    }
    else
    {
        PLOG(PL_ERROR, "NorpApp::OnCommand(%s) error: unimplemented command!\n", cmd);
        return false;
    }
    return true;
}  // end NorpApp::OnCommand()


       
