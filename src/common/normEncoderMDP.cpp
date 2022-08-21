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
 

#include <stdlib.h>
#include <string.h>
        
#include "normEncoderMDP.h"
#include "galois.h"  // for Galois math routines

#ifdef SIMULATE
#include "normMessage.h"
#endif // SIMULATE

NormEncoderMDP::NormEncoderMDP()
    : npar(0), vector_size(0), 
      gen_poly(NULL), scratch(NULL)
{

}  // end NormEncoderMDP::NormEncoderMDP()

NormEncoderMDP::~NormEncoderMDP()
{
	if (gen_poly) Destroy();
}

bool NormEncoderMDP::Init(unsigned int numData, unsigned int numParity, UINT16 vecSizeMax)
{
    // Debugging assertions
    ASSERT((numData + numParity) <= 255);
    if ((numData + numParity) > 255) return false;  // (TBD) printout error message
    if (gen_poly) Destroy();    
    npar = numParity;
    
#ifdef SIMULATE
    vecSizeMax = MIN(SIM_PAYLOAD_MAX, vecSizeMax);
#endif // SIMULATE
    
    vector_size = vecSizeMax;    
    // Create gen_poly polynomial 
    if(!CreateGeneratorPolynomial())
    {
	    PLOG(PL_FATAL, "NormEncoderMDP: Error creating gen_poly polynomial!\n");
	    return false;
    }    
    // Allocate scratch space for encoding
    if(!(scratch = new unsigned char[vecSizeMax]))
    {
	    PLOG(PL_FATAL, "NormEncoderMDP: Error allocating memory for encoder scratch space: %s\n",
                    GetErrorString());
	    Destroy();
	    return false;
    }
	return true;
}  // end NormEncoderMDP::Init()

// Free memory allocated for encoder state (Encoder must be re-inited before use)
void NormEncoderMDP::Destroy()
{
	if(NULL != scratch)
    {
		delete[] scratch;
		scratch = NULL;
    }
    if (NULL != gen_poly) 
    {
	    delete[] gen_poly;
	    gen_poly = NULL;
    }
}  // end NormEncoderMDP::Destroy()


bool NormEncoderMDP::CreateGeneratorPolynomial()
{
    unsigned char *tp, *tp1, *tp2;
    int degree = 2*npar;    
    if(gen_poly) delete[] gen_poly;
    
    if(!(gen_poly = new unsigned char[npar+1]))
    {
	    PLOG(PL_FATAL, "NormEncoderMDP: Error allocating memory for gen_poly polynomial: %s\n",
                 GetErrorString());
	    return false;
    }    
    /* Allocate memory for temporary polynomial arrays */
    if(!(tp = new unsigned char[2*degree]))
    {
	    PLOG(PL_FATAL, "NormEncoderMDP: Error allocating memory while computing gen_poly: %s\n",
                GetErrorString());
        delete[] gen_poly;
	    return false;
    }    
    if(!(tp1 = new unsigned char[2*degree]))
    {
	    delete[] tp;
	    delete[] gen_poly;
	    PLOG(PL_FATAL, "NormEncoderMDP: Error allocating memory while computing gen_poly: %s\n",
                GetErrorString());
	    return false;
    }    
    if(!(tp2 = new unsigned char[2*degree]))
    {
	    delete[] tp1;
	    delete[] tp;
	    delete[] gen_poly;
	    PLOG(PL_FATAL, "NormEncoderMDP: Error allocating memory while computing gen_poly: %s\n",
                GetErrorString());
	    return false;
    }
    // multiply (x + a^n) for n = 1 to npar 
    memset(tp1, 0, degree*sizeof(unsigned char));
    tp1[0] = 1;    
    for (unsigned int n = 1; n <= npar; n++)
    {
	    memset(tp, 0, degree*sizeof(unsigned char));
	    tp[0] = gexp(n);  // set up x+a^n 
	    tp[1] = 1;
	    // Polynomial multiplication 
	    memset(gen_poly, 0, (npar+1)*sizeof(unsigned char));	
	    for (int i = 0; i < degree; i++)
	    {
	        memset(&tp2[degree], 0, degree*sizeof(unsigned char));
            //unsigned int j;
	        // Scale tp2 by p1[i] 
            int j;
	        for(j=0; j<degree; j++) tp2[j]=gmult(tp1[j], tp[i]);
	        // Mult(shift) tp2 right by i 
	        for (j = (degree*2)-1; j >= i; j--) 
                tp2[j] = tp2[j-i];
	        memset(tp2, 0, i*sizeof(unsigned char));
	        // Add into partial product 
	        for(unsigned int x=0; x < (npar+1); x++) gen_poly[x] ^= tp2[x];
	    }    
	    memcpy(tp1, gen_poly, (npar+1)*sizeof(unsigned char));
	    memset(&tp1[npar+1], 0, (2*degree)-(npar+1));
    }    
    delete[] tp2;
    delete[] tp1;
    delete[] tp; 
    return true;  
}  // end NormEncoderMDP::CreateGeneratorPolynomial()



// Encode data vectors one at a time.  The user of this function
// must keep track of when parity is ready for transmission
// Parity data is written to list of parity vectors supplied by caller
// MUST be called w/ "data" vectors in-order by segmentId (caller's responsibility)
void NormEncoderMDP::Encode(unsigned int /*segmentId*/, const char* data, char** pVec)
{
    int i, j;
    unsigned char *userData, *LSFR1, *LSFR2, *pVec0;
    int npar_minus_one = npar - 1;
    unsigned char* genPoly = &gen_poly[npar_minus_one];
    ASSERT(NULL != scratch);  // Make sure it's been init'd first    
    // Assumes parity vectors are zero-filled at block start !!! 
    // Copy pVec[0] for use in calculations 
    
    UINT16 vecSize = vector_size;
    
    memcpy(scratch, pVec[0], vecSize);
    if (npar > 1)
    {
	    for(i = 0; i < npar_minus_one; i++)
	    {
	        pVec0 = scratch;
	        userData = (unsigned char*) data;
	        LSFR1 = (unsigned char*) pVec[i];
	        LSFR2 = (unsigned char*) pVec[i+1];
	        for(j = 0; j < vecSize; j++)
		        *LSFR1++ = *LSFR2++ ^
			        gmult(*genPoly, (*userData++ ^ *pVec0++));
            genPoly--;
	    }
        
    }    
    pVec0 = scratch;
    userData = (unsigned char*) data;
    LSFR1 = (unsigned char*) pVec[npar_minus_one];
    for(j = 0; j < vecSize; j++)
    	*LSFR1++ = gmult(*genPoly, (*userData++ ^ *pVec0++));
}  // end NormEncoderMDP::Encode()


/********************************************************************************
 *  NormDecoderMDP implementation routines
 */

NormDecoderMDP::NormDecoderMDP()
    : npar(0), vector_size(0), lambda(NULL), 
      s_vec(NULL), o_vec(NULL), scratch(NULL)
{
    
}

NormDecoderMDP::~NormDecoderMDP()
{
    if (NULL != lambda) Destroy();
}

bool NormDecoderMDP::Init(unsigned int numData, unsigned int numParity, UINT16 vecSizeMax)
{ 
    // Debugging assertions
    ASSERT((numData + numParity) <= 255);
    if ((numData + numParity) > 255) return false;
    
#ifdef SIMULATE
    vecSizeMax = MIN(SIM_PAYLOAD_MAX, vecSizeMax);
#endif // SIMUATE  
    
    if (lambda) Destroy();  // Check if already inited ...
    
    npar = numParity;
    vector_size = vecSizeMax;
    
    if(!(lambda = new unsigned char[2*npar]))
    {
	    PLOG(PL_FATAL, "NormDecoderMDP: Error allocating memory for lambda: %s\n",
                GetErrorString());
	    return(false);
    }
    
    /* Allocate memory for s_vec ptr and the syndrome vectors */
    if(!(s_vec = new unsigned char*[npar]))
    {
	    PLOG(PL_FATAL, "NormDecoderMDP: Error allocating memory for s_vec ptr: %s\n",
                GetErrorString());
	    Destroy();
	    return(false);
    }
    unsigned int i;
    for(i=0; i < npar; i++)
    {
	    if(!(s_vec[i] = new unsigned char[vecSizeMax]))
	    {
	        PLOG(PL_FATAL, "NormDecoderMDP: Error allocating memory for new s_vec: %s\n",
                    GetErrorString());
	        Destroy();
	        return(false);
	    }
    }
    
    /* Allocate memory for the o_vec ptr and the Omega vectors */
    if(!(o_vec = new unsigned char*[npar]))
    {
	    PLOG(PL_FATAL, "NormDecoderMDP: Error allocating memory for new o_vec ptr: %s\n",
                GetErrorString());
	    Destroy();
	    return(false);
    }
    
    for(i=0; i < npar; i++)
    {
	    if(!(o_vec[i] = new unsigned char[vecSizeMax]))
	    {
	        PLOG(PL_FATAL, "NormDecoderMDP: Error allocating memory for new o_vec: %s",
                    GetErrorString());
	        Destroy();
	        return(false);
	    }
    }
    
    if (!(scratch = new unsigned char[vecSizeMax]))
    {
        PLOG(PL_FATAL, "NormDecoderMDP: Error allocating memory for scratch space: %s",
                 GetErrorString());  
    }
    memset(scratch, 0, vecSizeMax*sizeof(unsigned char));
    return(true);
}  // end NormDecoderMDP::Init()


void NormDecoderMDP::Destroy()
{
    if (scratch)
    {
        delete[] scratch;
        scratch = NULL;
    }
    if(o_vec)
    {
	    for(unsigned int i=0; i<npar; i++)
	        if (o_vec[i]) delete[] o_vec[i];
	    delete[] o_vec;
        o_vec = NULL;
    }	
    if(s_vec)
    {
	    for(unsigned int i = 0; i < npar; i++)
	        if (s_vec[i]) delete[] s_vec[i];
	    delete[] s_vec;
        s_vec = NULL;
    }
    if (lambda)
    {
        
        delete[] lambda;
        lambda = NULL;
    }
}  // end NormDecoderMDP::Destroy()


// This will crash & burn if (erasureCount > npar)
int NormDecoderMDP::Decode(char** dVec, unsigned int numData, unsigned int erasureCount, unsigned int* erasureLocs)
{
    // Debugging assertions
    ASSERT(NULL != lambda);     
    ASSERT(erasureCount && (erasureCount <= npar));
      
    // (A) Compute syndrome vectors 
    
    // First zero out erasure vectors (NORM provides zero-filled vecs) 
	
	// Then calculate syndrome (based on zero value erasures) 
    unsigned int nvecs = npar + numData;
    UINT16 vecSize = vector_size;
    
    unsigned int i;
    for (i = 0; i < npar; i++)
    {
	    int X = gexp(i+1);
	    unsigned char* synVec = s_vec[i];
        memset(synVec, 0, vecSize*sizeof(char));
        for(unsigned int j = 0; j < nvecs; j++)
        {
	        unsigned char* data = dVec[j] ? (unsigned char*)dVec[j] : scratch;
            unsigned char* S = synVec;
            for (int n = 0; n < vecSize; n++)
	        {
                *S = *data++ ^ gmult(X, *S);
	            S++;
	        }
        }
    }
    
    // (B) Init lambda (the erasure locator polynomial) 
    unsigned int degree = 2*npar;
    unsigned int nvecsMinusOne = nvecs - 1;
    memset(lambda, 0, degree*sizeof(char));
    lambda[0] = 1;
    for (i = 0; i < erasureCount; i++)
    {
	    int X = gexp(nvecsMinusOne - erasureLocs[i]);
	    for(int j = (degree-1); j > 0; j--)
	        lambda[j] = lambda[j] ^ gmult(X, lambda[j-1]);
    }

    // (C) Compute modified Omega using lambda 
    for(i = 0; i < npar; i++)
    {
	    int k = i;
	    memset(o_vec[i], 0, vecSize*sizeof(char));
        //int m = i + 1;
	    for(unsigned int j = 0; j <= i; j++)
	    { 
	        unsigned char* Omega = o_vec[i];
	        unsigned char* S = s_vec[j];
	        int Lk = lambda[k--];
	        for(UINT16 n = 0; n < vecSize; n++)
		        *Omega++ ^= gmult(*S++, Lk);
	    }
    }

    // (D) Finally, fill in the erasures 
    for (i = 0; i < erasureCount; i++)
    {       
        // Only fill _data_ erasures
        if (erasureLocs[i] >= numData) break;//return erasureCount;
        
        // evaluate lambda' (derivative) at alpha^(-i) 
	    // (all odd powers disappear) 
	    unsigned int k = nvecsMinusOne - erasureLocs[i];
	    int denom = 0;
        unsigned int j;
	    for (j = 1; j < degree; j += 2)
	        denom ^= gmult(lambda[j], gexp(((255-k)*(j-1)) % 255));
	    // Invert for use computing error value below 
	    denom = ginv(denom);

	    // Now evaluate Omega at alpha^(-i) (numerator) 
	    unsigned char* eVec = (unsigned char*)dVec[erasureLocs[i]];
	    for (j = 0; j < npar; j++)
	    {
            unsigned char* data = eVec;
	        unsigned char* Omega = o_vec[j];
	        int X = gexp(((255-k)*j) % 255);
	        for(int n = 0; n < vecSize; n++)
		        *data++ ^= gmult(*Omega++, X);
            
	    }

	    // Scale numerator with denominator 
	    unsigned char* data = eVec;
	    for(int n = 0; n < vecSize; n++)
	    {
	        *data = gmult(*data, denom);
	        data++;
	    }
    }
    return erasureCount;
}  // end NormDecoderMDP::Decode()



