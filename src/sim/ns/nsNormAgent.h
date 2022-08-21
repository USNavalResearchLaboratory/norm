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

#ifndef _NS_NORM_AGENT
#define _NS_NORM_AGENT

#include "nsProtoSimAgent.h" // from Protolib
#include "normSimAgent.h"

// The "NsProtoAgent" is based on the ns Agent class.
// This lets us have a send/recv attachment to the NS
// simulation environment

// IMPORTANT NOTE! NsProtoAgent must be listed _first_ here
// (because we can't dynamic_cast install_data void* pointers)
class NsNormAgent : public NsProtoSimAgent, public NormSimAgent
{
  public:
    NsNormAgent();
    ~NsNormAgent();
    
    // NsProtoSimAgent base class overrides
    virtual bool OnStartup(int argc, const char*const* argv);
    virtual bool ProcessCommands(int argc, const char*const* argv);
    virtual void OnShutdown();

    // NormSimAgent overrides   
    unsigned long GetAgentId() {return (unsigned long)addr();} 
    bool HandleMessage(const char* txBuffer, unsigned int len, const ProtoAddress& srcAddr)
    {
        return NormSimAgent::SendMessage(len,txBuffer);
    }


};  // end class NsNormAgent


#endif // _NS_NORM_AGENT
