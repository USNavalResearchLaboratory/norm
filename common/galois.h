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
 
#ifndef _NORM_GALOIS
#define _NORM_GALOIS

namespace Norm
{
    extern const unsigned char GINV[256];
    extern const unsigned char GEXP[512];
    extern const unsigned char GMULT[256][256];
}
inline unsigned char gexp(unsigned int x) {return Norm::GEXP[x];}
inline unsigned char gmult(unsigned int x, unsigned int y) {return Norm::GMULT[x][y];}
inline unsigned char ginv(unsigned int x) {return Norm::GINV[x];}
#endif // _NORM_GALOIS
