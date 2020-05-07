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
 
#ifndef _NORM_ENCODER
#define _NORM_ENCODER

#include "protokit.h"  // protolib stuff

class NormEncoder
{
    public:
        virtual ~NormEncoder();
        virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize) = 0;
        virtual void Destroy() = 0;
        virtual void Encode(unsigned int segmentId, const char *dataVector, char **parityVectorList) = 0;    
};  // end class NormEncoder

class NormDecoder
{
    public:
        virtual ~NormDecoder();
        virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize) = 0;
        virtual void Destroy() = 0;
        virtual int Decode(char** vectorList, unsigned int numData,  unsigned int erasureCount, unsigned int* erasureLocs) = 0;    
};  // end class NormDecoder

#endif // _NORM_ENCODER
