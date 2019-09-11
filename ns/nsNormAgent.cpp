/*********************************************************************
 *
 * Authorization TO USE AND DISTRIBUTE
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: 
 *
 * (1) source code distributions retain this paragraph in its entirety, 
 *  
 * (2) distributions including binary code include this paragraph in
 *     its entirety in the documentation or other materials provided 
 *     with the distribution, and 
 *
 * (3) all advertising materials mentioning features or use of this 
 *     software display the following acknowledgment:
 * 
 *      "This product includes software written and developed 
 *       by Brian Adamson and Joe Macker of the Naval Research 
 *       Laboratory (NRL)." 
 *         
 *  The name of NRL, the name(s) of NRL  employee(s), or any entity
 *  of the United States Government may not be used to endorse or
 *  promote  products derived from this software, nor does the 
 *  inclusion of the NRL written and developed software  directly or
 *  indirectly suggest NRL or United States  Government endorsement
 *  of this product.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ********************************************************************/
 
#include "ip.h"  	// for hdr_ip def
#include "flags.h"  // for hdr_flags def

#include "nsMgenAgent.h"
#include "nsNormAgent.h" 
static class NsNormAgentClass : public TclClass
{
	public:
		NsNormAgentClass() : TclClass("Agent/NORM") {}
	 	TclObject* create(int argc, const char*const* argv) 
			{return (new NsNormAgent());}
} class_norm_agent;	


NsNormAgent::NsNormAgent()
  : NormSimAgent(GetTimerMgr(), GetSocketNotifier())
{
 
}  

NsNormAgent::~NsNormAgent()
{    
}

bool NsNormAgent::OnStartup(int argc, const char*const* argv)
{

    if (ProcessCommands(argc, argv))
    {
        return true;
    }
    else
    {
        fprintf(stderr, "NsNormAgent::OnStartup() error processing commands\n");
        return false;
    }
} // end NsNormAgent::OnStartup()

void NsNormAgent::OnShutdown()
{
    NormSimAgent::Stop(); 
}  // end NsNormAgent::OnShutdown()

bool NsNormAgent::ProcessCommands(int argc, const char*const* argv) 
{   
    // Process commands, passing unknown commands to base Agent class
    int i = 1;
    while (i < argc)
    {
        // Intercept ns-specific commands
        if (!strcmp(argv[i], "attach-mgen"))
        {
            // (TBD) this could be done as a generic NormSimAgent command
            // Attach Agent/MGEN to this NormSimAgent 
            if (++i >= argc)
            {
                DMSG(0, "NsNormAgent::ProcessCommands() ProcessCommand(attach-mgen) error: insufficent arguments\n");
                return false;
            } 
            Tcl& tcl = Tcl::instance();  
            msg_sink = dynamic_cast<ProtoMessageSink*>(tcl.lookup(argv[i]));

            if (msg_sink)
            {
                i++;
                continue;
            }
            else
            {
                DMSG(0, "NsNormAgent::ProcessCommands() ProcessCommand(attach-mgen) error: invalid mgen agent\n");
                return false;
            }
        }
        else if (!strcmp(argv[i], "active"))
        {
            // query agent's current state of activity
            Tcl& tcl = Tcl::instance(); 
            if (IsActive())
                Tcl_SetResult(tcl.interp(), "on", TCL_STATIC);
                //sprintf(tcl.result(), "on");
            else
                Tcl_SetResult(tcl.interp(), "off", TCL_STATIC);
            i++;
            continue;
        }
        // Other commands are interpreted by the NormSimAgent base class
        NormSimAgent::CmdType cmdType = CommandType(argv[i]);
        switch (cmdType)
        {
            case NormSimAgent::CMD_NOARG:
                if (!ProcessCommand(argv[i], NULL))
                {
                    DMSG(0, "NsNormAgent::ProcessCommands() ProcessCommand(%s) error\n", 
                            argv[i]);
                    return false;
                }
                i++;
                break;
                
            case NormSimAgent::CMD_ARG:
                if (!ProcessCommand(argv[i], argv[i+1]))
                {
                    DMSG(0, "NsNormAgent::ProcessCommands() ProcessCommand(%s, %s) error\n", 
                            argv[i], argv[i+1]);
                    return false;
                }
                i += 2;
                break;
                
            case NormSimAgent::CMD_INVALID:
                return false;
        }
    }
    return true; 
}  // end NsNormAgent::ProcessCommands()

