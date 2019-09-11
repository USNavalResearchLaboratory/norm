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
 
#ifndef _NORM_ENCODER_MDP
#define _NORM_ENCODER_MDP

#include "normEncoder.h"

class NormEncoderMDP : public NormEncoder
{
    // Methods
    public:
	    NormEncoderMDP();
	    ~NormEncoderMDP();
	    bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize);
        void Destroy();
        bool IsReady(){return (bool)(gen_poly != NULL);}
        // "Encode" MUST be called in order of source vector0, vector1, vector2, etc
	    void Encode(unsigned int segmentId, const char *dataVector, char **parityVectorList);
	
    private:
	    bool CreateGeneratorPolynomial();
    
    // Members
	    unsigned int    npar;	      // No. of parity packets (n-k)
	    UINT16		    vector_size;  // Size of biggest vector to encode
	    unsigned char*  gen_poly;     // Ptr to generator polynomial
	    unsigned char*  scratch;      // scratch space for encoding
        
};  // end class NormEncoderMDP


class NormDecoderMDP : public NormDecoder
{
    // Methods
    public:
	    NormDecoderMDP();
	    ~NormDecoderMDP();
	    bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize);
	    int Decode(char** vectorList, unsigned int numData,  unsigned int erasureCount, unsigned int* erasureLocs);
        int NumParity() {return npar;}
	    int VectorSize() {return vector_size;}
        void Destroy();
        
    // Members
    private:
	    unsigned int    npar;        // No. of parity packets (n-k)
	    UINT16          vector_size; // Size of biggest vector to encode  			
	    unsigned char*  lambda;      // Erasure location polynomial ("2*npar" ints)
	    unsigned char** s_vec;       // Syndrome vectors (pointers to "npar" vectors)
	    unsigned char** o_vec;       // Omega vectors (pointers to "npar" vectors)
	    unsigned char*  scratch;
    
};  // end class NormDecoderMDP


#endif // _NORM_ENCODER_MDP
