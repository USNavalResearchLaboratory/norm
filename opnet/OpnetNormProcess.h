#ifndef _OPNET_NORM_PROCESS
#define _OPNET_NORM_PROCESS

// JPH NORM 11/21/2005

#include "opnet.h"
#include "opnetProtoSimProcess.h"
#include "normSimAgent.h" // this includes protokit.h


class OpnetNormProcess : public OpnetProtoSimProcess, public NormSimAgent, public MgenSink
{
    public:
		OpnetNormProcess();
		~OpnetNormProcess();
	
		// OpnetProtoSimProcess's base class overrides
		bool OnStartup(int argc, const char*const* argv);
		bool ProcessCommands(int argc, const char*const* argv);
		void OnShutdown();
        void ReceivePacketMonitor(Ici* ici, Packet* pkt);
        void TransmitPacketMonitor(Ici* ici, Packet* pkt);
		            
// JPH 5/25/06 - backoff from mgen 42b8 to 42b6
//        bool SendMgenMessage(MgenMsg& theMsg,   
//				     bool checksum_enable,
//				     char* txBuffer);
        bool SendMgenMessage(const char*           txBuffer,
                                     unsigned int          len,
                                     const ProtoAddress&   dstAddr);
	

		
	    // NormSimAgent override   
        unsigned long GetAgentId() {return (unsigned long)addr();} 

	private:
		IpT_Address addr();

		
};  // end class OpnetNormProcess


#endif // _OPNET_NORM_PROCESS


