/*********************************************************************
 *
 * AUTHORIZATION TO USE AND DISTRIBUTE
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

#include "nsNormAgent.h" 

static class NsNormAgentClass : public TclClass
{
	public:
		NsNormAgentClass() : TclClass("Agent/NORM") {}
	 	TclObject *create(int argc, const char*const* argv) 
			{return (new NsNormAgent());}
} class_norm_agent;	


NsNormAgent::NsNormAgent()
{
    NormSimAgent::Init(ProtoSimAgent::TimerInstaller, 
                       static_cast<ProtoSimAgent*>(this),
                       ProtoSimAgent::SocketInstaller, 
                       static_cast<ProtoSimAgent*>(this));
}  

NsNormAgent::~NsNormAgent()
{    
}

int NsNormAgent::command(int argc, const char*const* argv) 
{   
    // Process commands, passing unknown commands to base Agent class
    int i = 1;
    while (i < argc)
    {
        NormSimAgent::CmdType cmdType = CommandType(argv[i]);
        switch (cmdType)
        {
            case NormSimAgent::CMD_NOARG:
                if (!ProcessCommand(argv[i], NULL))
                {
                    DMSG(0, "NsNormAgent::command() ProcessCommand(%s) error\n", 
                            argv[i]);
                    return TCL_ERROR;
                }
                i++;
                break;
                
            case NormSimAgent::CMD_ARG:
                if (!ProcessCommand(argv[i], argv[i+1]))
                {
                    DMSG(0, "NsNormAgent::command() ProcessCommand(%s, %s) error\n", 
                            argv[i], argv[i+1]);
                    return TCL_ERROR;
                }
                i += 2;
                break;
                
            case NormSimAgent::CMD_INVALID:
                return NsProtoAgent::command(argc, argv);
        }
    }
    return TCL_OK; 
}  // end NsNormAgent::command()




