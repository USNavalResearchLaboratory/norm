
// normSimAgent.h - Generic (base class) NORM simulation agent

#include "protoLib.h"
#include "normSession.h"

// Base class for Norm simulation agents (e.g. ns-2, OPNET, etc)
class NormSimAgent : public NormController
{
    public:
        NormSimAgent();
        virtual ~NormSimAgent();
        void Init(ProtocolTimerInstallFunc* timerInstaller, 
                  const void*               timerInstallData,
                  UdpSocketInstallFunc*     socketInstaller,
                  void*                     socketInstallData)
        {
            session_mgr.Init(timerInstaller,  timerInstallData,
                             socketInstaller, socketInstallData,
                             this);
        }
        
        bool ProcessCommand(const char* cmd, const char* val);
        
        // Note: don't allow client _and_ server operation at same time
		bool StartServer();
        bool StartClient();
        void Stop();
        
               
    protected:
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        CmdType CommandType(const char* cmd);
        virtual unsigned long GetAgentId() = 0;
    
    private:
        virtual void Notify(NormController::Event event,
                            class NormSessionMgr* sessionMgr,
                            class NormSession*    session,
                            class NormServerNode* server,
                            class NormObject*     object);
        
        void InstallTimer(ProtocolTimer& theTimer)
            {session_mgr.InstallTimer(&theTimer);}
        
        bool OnIntervalTimeout();
    
        static const char* const cmd_list[];

        NormSessionMgr      session_mgr;
        NormSession*        session;
        NormStreamObject*   stream;
           
        // session parameters
        char*               address;        // session address
        UINT16              port;           // session port number
        UINT8               ttl;
        double              tx_rate;        // bits/sec
        double              backoff_factor;
        UINT16              segment_size;
        UINT8               ndata;
        UINT8               nparity;
        UINT8               auto_parity;
        double              group_size;
        unsigned long       tx_buffer_size; // bytes
        unsigned long       rx_buffer_size; // bytes
        
        // for simulated transmission (streams or files)
        unsigned long       tx_object_size;
        double              tx_object_interval;
        int                 tx_repeat_count;
        double              tx_repeat_interval;
        
        ProtocolTimer       interval_timer;  
        
        // protocol debug parameters
        bool                tracing;
        double              tx_loss;
        double              rx_loss;
    
}; // end class NormSimAgent

