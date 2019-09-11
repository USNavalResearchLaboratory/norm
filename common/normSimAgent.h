#ifndef _NORM_SIM_AGENT
#define _NORM_SIM_AGENT

// normSimAgent.h - Generic (base class) NORM simulation agent

#include "normSession.h"
#include "protokit.h"
#include "protoSimAgent.h"

// Base class for Norm simulation agents (e.g. ns-2, OPNET, etc)
class NormSimAgent : public NormController, public ProtoMessageSink
{
    public:
        virtual ~NormSimAgent();
        
        bool ProcessCommand(const char* cmd, const char* val);
        
        // Note: don't allow client _and_ server operation at same time
		bool StartServer();  // start sender
        bool StartClient();  // start receiver
        bool IsActive() {return (NULL != session);}
        void Stop();
        
        
       bool SendMessage(unsigned int len, const char* txBuffer);
               
    protected:
        NormSimAgent(ProtoTimerMgr&         timerMgr,
                     ProtoSocket::Notifier& socketNotifier);
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        CmdType CommandType(const char* cmd);
        virtual unsigned long GetAgentId() = 0;
        ProtoMessageSink*      msg_sink; 

#ifdef OPNET
        void HandleMessage(char*             buffer, 
			unsigned int          len, 
			const ProtoAddress& srcAddr);
		void SetSink(ProtoMessageSink* sink){msg_sink=sink;}
#endif //OPNET     

    private:
        void OnInputReady();
        bool FlushStream();
        virtual void Notify(NormController::Event event,
                            class NormSessionMgr* sessionMgr,
                            class NormSession*    session,
                            class NormServerNode* server,
                            class NormObject*     object);
        
        void ActivateTimer(ProtoTimer& theTimer)
            {session_mgr.ActivateTimer(theTimer);}
        
        bool OnIntervalTimeout(ProtoTimer& theTimer);
    
        static const char* const cmd_list[];

        NormSessionMgr              session_mgr;
        NormSession*                session;

        // session parameters
        char*                       address;        // session address
        UINT16                      port;           // session port number
        UINT8                       ttl;
        double                      tx_rate;        // bits/sec
        bool                        cc_enable;
        bool                        unicast_nacks;
        bool                        silent_client;
        double                      backoff_factor;
        UINT16                      segment_size;
        UINT8                       ndata;
        UINT8                       nparity;
        UINT8                       auto_parity;
        UINT8                       extra_parity;
        double                      group_size;
        double                      grtt_estimate;
        unsigned long               tx_buffer_size; // bytes
        unsigned long               rx_buffer_size; // bytes

        // for simulated transmission (streams or files)
        unsigned long               tx_object_size;
        double                      tx_object_interval;
        unsigned long               tx_object_size_min;
        unsigned long               tx_object_size_max;
        int                         tx_repeat_count;
        double                      tx_repeat_interval;

        NormStreamObject*           stream;
        bool                        auto_stream;
        bool                        push_mode;
        NormStreamObject::FlushMode flush_mode;
        char*                       tx_msg_buffer;
        unsigned int                tx_msg_len;
        unsigned int                tx_msg_index;
        char                        mgen_buffer[64];
        bool                        msg_sync;
        unsigned int                mgen_bytes;
        unsigned int                mgen_pending_bytes;
        ProtoTimer                  interval_timer;  
        
        // protocol debug parameters
        bool                        tracing;
        double                      tx_loss;
        double                      rx_loss;
    
}; // end class NormSimAgent

#endif // NORM_SIM_AGENT
