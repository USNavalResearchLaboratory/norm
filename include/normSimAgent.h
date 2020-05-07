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
        
        // Note: don't allow receiver _and_ sender operation at same time
		bool StartSender();  // start sender
        bool StartReceiver();  // start receiver
        bool IsActive() {return (NULL != session);}
        void Stop();
        
        
       bool SendMessage(unsigned int len, const char* txBuffer);
       
       enum NormCC
       {
           NORM_FIXED, // fixed-rate (no congestion control)
           NORM_CC,    // "normal" TCP-friendly congestion control
           NORM_CCE,   // strict ECN-based congestion control
           NORM_CCL    // "loss-tolerant" congestion control 
       };
           
       // These functions support ACK-based flow-controlled streaming using
       // some additional state variables (stream_buffer_max, stream_buffer_count, etc)
       static unsigned int ComputeStreamBufferSegmentCount(unsigned int bufferBytes, UINT16 segmentSize, UINT16 blockSize);
       unsigned int WriteToStream(const char* buffer, unsigned int numBytes);
       bool AddAckingNode(NormNodeId nodeId);
               
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
        bool FlushStream(bool eom);// = true);
        virtual void Notify(NormController::Event event,
                            class NormSessionMgr* sessionMgr,
                            class NormSession*    session,
                            class NormSenderNode* sender,
                            class NormObject*     object);
        
        void ActivateTimer(ProtoTimer& theTimer)
            {session_mgr.ActivateTimer(theTimer);}
        
        bool OnIntervalTimeout(ProtoTimer& theTimer);
        
        void SetCCMode(NormCC ccMode);
    
        static const char* const cmd_list[];

        NormSessionMgr              session_mgr;
        NormSession*                session;

        // session parameters
        char*                       address;        // session address
        UINT16                      port;           // session port number
        UINT8                       ttl;
        double                      tx_rate;        // bits/sec
        unsigned int                probe_count;
        bool                        cc_enable;
        bool                        ecn_enable;
        NormCC                      cc_mode;
        bool                        unicast_nacks;
        bool                        silent_receiver;
        double                      backoff_factor;
        UINT16                      segment_size;
        UINT8                       ndata;
        UINT8                       nparity;
        UINT8                       auto_parity;
        UINT8                       extra_parity;
        double                      group_size;
        double                      grtt_estimate;
        unsigned long               tx_buffer_size; // bytes
        unsigned long               tx_cache_min;
        unsigned long               tx_cache_max;
        NormObjectSize              tx_cache_size;   
        
        unsigned long               rx_buffer_size; // bytes
        unsigned long               rx_cache_max;   // rx object max_pending count
        
        // for simulated transmission (streams or files)
        unsigned long               tx_object_size;
        double                      tx_object_interval;
        unsigned long               tx_object_size_min;
        unsigned long               tx_object_size_max;
        int                         tx_repeat_count;
        double                      tx_repeat_interval;
        int                         tx_requeue;    
        int                         tx_requeue_count;

        NormStreamObject*           stream;
        bool                        auto_stream;
        unsigned int                stream_buffer_max;
        unsigned int                stream_buffer_count;
        unsigned int                stream_bytes_remain;
        bool                        watermark_pending;
        bool                        flow_control;
        
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
        FILE*                       log_file_ptr;
        double                      tx_loss;
        double                      rx_loss;
    
}; // end class NormSimAgent

#endif // NORM_SIM_AGENT
