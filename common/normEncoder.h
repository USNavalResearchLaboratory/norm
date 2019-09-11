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

#include "sysdefs.h"  // protolib stuff

class NormEncoder
{
    // Members
    private:
	    int		        npar;	      // No. of parity packets (N-k)
	    int		        vector_size;  // Size of biggest vector to encode
	    unsigned char*  genPoly;      // Ptr to generator polynomial
	    unsigned char*  scratch;      // scratch space for encoding
	
    // Methods
    public:
	    NormEncoder();
	    ~NormEncoder();
	    bool Init(int numParity, int vectorSize);
        void Destroy();
        bool IsReady(){return (bool)(genPoly != NULL);}
	    void Encode(const char *dataVector, char **parityVectorList);
	    int NumParity() {return npar;}
	    int VectorSize() {return vector_size;}
	
    private:
	    bool CreateGeneratorPolynomial();
};


class NormDecoder
{
    // Members
    private:
	    int             npar;        // No. of parity packets (n-K)
	    int             vector_size; // Size of biggest vector to encode  			
	    unsigned char*  Lambda;      // Erasure location polynomial ("2*npar" ints)
	    unsigned char** sVec;        // Syndrome vectors (pointers to "npar" vectors)
	    unsigned char** oVec;        // Omega vectors (pointers to "npar" vectors)
	    unsigned char*  scratch;
    // Methods
    public:
	    NormDecoder();
	    ~NormDecoder();
	    bool Init(int numParity, int vectorSize);
	    int Decode(char **vectorList, int ndata,  UINT16 erasureCount, UINT16* erasureLocs);
        int NumParity() {return npar;}
	    int VectorSize() {return vector_size;}
        void Destroy();
};



#endif // _NORM_ENCODER
