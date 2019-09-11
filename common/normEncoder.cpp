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
#include <errno.h>
        
#include "normEncoder.h"
#include "galois.h"  // for Galois math routines
#include "debug.h"

#ifdef SIMULATE
#include "normMessage.h"
#endif // SIMULATE

NormEncoder::NormEncoder()
    : npar(0), vector_size(0), 
      genPoly(NULL), scratch(NULL)
{

}  // end NormEncoder::NormEncoder()

NormEncoder::~NormEncoder()
{
	if (genPoly) Destroy();
}

bool NormEncoder::Init(int numParity, int vecSizeMax)
{
    // Debugging assertions
    ASSERT((numParity>=0)&&(numParity<129));
    ASSERT(vecSizeMax >= 0);    
    if (genPoly) Destroy();    
    npar = numParity;
    vector_size = vecSizeMax;    
    // Create generator polynomial 
    if(!CreateGeneratorPolynomial())
    {
	    DMSG(0, "NormEncoder: Error creating generator polynomial!\n");
	    return false;
    }    
    // Allocate scratch space for encoding
    if(!(scratch = new unsigned char[vecSizeMax]))
    {
	    DMSG(0, "NormEncoder: Error allocating memory for encoder scratch space: %s\n",
                    strerror(errno));
	    Destroy();
	    return false;
    }
	return true;
}  // end NormEncoder::Init()

// Free memory allocated for encoder state (Encoder must be re-inited before use)
void NormEncoder::Destroy()
{
	if(scratch)
    {
		delete []scratch;
		scratch = NULL;
    }
    if (genPoly) 
    {
	    delete genPoly;
	    genPoly = NULL;
    }
}  // end NormEncoder::Destroy()


bool NormEncoder::CreateGeneratorPolynomial()
{
    unsigned char *tp, *tp1, *tp2;
    int degree = 2*npar;    
    if(genPoly) delete genPoly;
    
    if(!(genPoly = new unsigned char[npar+1]))
    {
	    DMSG(0, "NormEncoder: Error allocating memory for generator polynomial: %s\n",
                 strerror(errno));
	    return false;
    }    
    /* Allocate memory for temporary polynomial arrays */
    if(!(tp = new unsigned char[2*degree]))
    {
	    DMSG(0, "NormEncoder: Error allocating memory while computing genpoly: %s\n",
                strerror(errno));
        delete genPoly;
	    return false;
    }    
    if(!(tp1 = new unsigned char[2*degree]))
    {
	    delete tp;
	    delete genPoly;
	    DMSG(0, "NormEncoder: Error allocating memory while computing genpoly: %s\n",
                strerror(errno));
	    return false;
    }    
    if(!(tp2 = new unsigned char[2*degree]))
    {
	    delete tp1;
	    delete tp;
	    delete genPoly;
	    DMSG(0, "NormEncoder: Error allocating memory while computing genpoly: %s\n",
                strerror(errno));
	    return false;
    }
    // multiply (x + a^n) for n = 1 to npar 
    memset(tp1, 0, degree*sizeof(unsigned char));
    tp1[0] = 1;    
    for (int n = 1; n <= npar; n++)
    {
	    memset(tp, 0, degree*sizeof(unsigned char));
	    tp[0] = gexp(n);  // set up x+a^n 
	    tp[1] = 1;
	    // Polynomial multiplication 
	    memset(genPoly, 0, (npar+1)*sizeof(unsigned char));	
	    for (int i = 0; i < degree; i++)
	    {
	        memset(&tp2[degree], 0, degree*sizeof(unsigned char));
            int j;
	        // Scale tp2 by p1[i] 
	        for(j=0; j<degree; j++) tp2[j]=gmult(tp1[j], tp[i]);
	        // Mult(shift) tp2 right by i 
	        for (j = (degree*2)-1; j >= i; j--) tp2[j] = tp2[j-i];
	        memset(tp2, 0, i*sizeof(unsigned char));
	        // Add into partial product 
	        for(j=0; j < (npar+1); j++) genPoly[j] ^= tp2[j];
	    }    
	    memcpy(tp1, genPoly, (npar+1)*sizeof(unsigned char));
	    memset(&tp1[npar+1], 0, (2*degree)-(npar+1));
    }    
    delete tp2;
    delete tp1;
    delete tp; 
    return true;  
}  // end NormEncoder::CreateGeneratorPolynomial()



// Encode data vectors one at a time.  The user of this function
// must keep track of when parity is ready for transmission
// Parity data is written to list of parity vectors supplied by caller
void NormEncoder::Encode(const char *data, char **pVec)
{
    int i, j;
    unsigned char *userData, *LSFR1, *LSFR2, *pVec0;
    int npar_minus_one = npar - 1;
    unsigned char *gen_poly = &genPoly[npar_minus_one];
    ASSERT(scratch);  // Make sure it's been init'd first    
    // Assumes parity vectors are zero-filled at block start !!! 
    // Copy pVec[0] for use in calculations 
    
#ifdef SIMULATE    
    UINT16  vecSize = MIN(SIM_PAYLOAD_MAX, vector_size);
    vecSize = MAX(vecSize, NormDataMsg::PayloadHeaderLength());
#else
    UINT16 vecSize = vector_size;
#endif // if/else SIMULATE
    
    memcpy(scratch, pVec[0], vecSize);
    if (npar > 1)
    {
	    for(i = 0; i < npar_minus_one; i++)
	    {
	        pVec0 = scratch;
	        userData = (unsigned char *) data;
	        LSFR1 = (unsigned char *) pVec[i];
	        LSFR2 = (unsigned char *) pVec[i+1];
	        for(j = 0; j < vecSize; j++)
		        *LSFR1++ = *LSFR2++ ^
			        gmult(*gen_poly, (*userData++ ^ *pVec0++));
	        gen_poly--;
	    }
    }    
    pVec0 = scratch;
    userData = (unsigned char *) data;
    LSFR1 = (unsigned char *) pVec[npar_minus_one];
    for(j = 0; j < vecSize; j++)
    	*LSFR1++ = gmult(*gen_poly, (*userData++ ^ *pVec0++));
}  // end NormEncoder::Encode()


/********************************************************************************
 *  NormDecoder implementation routines
 */

NormDecoder::NormDecoder()
    : npar(0), vector_size(0), 
      Lambda(NULL), sVec(NULL), oVec(NULL)
{
    
}

NormDecoder::~NormDecoder()
{
    if (Lambda) Destroy();
}

bool NormDecoder::Init(int numParity, int vecSizeMax)
{ 
    // Debugging assertions
    ASSERT((numParity>=0)&&(numParity<=128));
    ASSERT(vecSizeMax >= 0);
    
    if (Lambda) Destroy();  // Check if already inited ...
    
    npar = numParity;
    vector_size = vecSizeMax;
    
    if(!(Lambda = new unsigned char[2*npar]))
    {
	    DMSG(0, "NormDecoder: Error allocating memory for Lambda: %s\n",
                strerror(errno));
	    return(false);
    }
    
    /* Allocate memory for sVec ptr and the syndrome vectors */
    if(!(sVec = new unsigned char*[npar]))
    {
	    DMSG(0, "NormDecoder: Error allocating memory for sVec ptr: %s\n",
                strerror(errno));
	    Destroy();
	    return(false);
    }
    int i;
    for(i=0; i < npar; i++)
    {
	    if(!(sVec[i] = new unsigned char[vecSizeMax]))
	    {
	        DMSG(0, "NormDecoder: Error allocating memory for new sVec: %s\n",
                    strerror(errno));
	        Destroy();
	        return(false);
	    }
    }
    
    /* Allocate memory for the oVec ptr and the Omega vectors */
    if(!(oVec = new unsigned char*[npar]))
    {
	    DMSG(0, "NormDecoder: Error allocating memory for new oVec ptr: %s\n",
                strerror(errno));
	    Destroy();
	    return(false);
    }
    
    for(i=0; i < npar; i++)
    {
	    if(!(oVec[i] = new unsigned char[vecSizeMax]))
	    {
	        DMSG(0, "NormDecoder: Error allocating memory for new oVec: %s",
                    strerror(errno));
	        Destroy();
	        return(false);
	    }
    }
    
    if (!(scratch = new unsigned char[vecSizeMax]))
    {
        DMSG(0, "NormDecoder: Error allocating memory for scratch space: %s",
                 strerror(errno));  
    }
    memset(scratch, 0, vecSizeMax*sizeof(unsigned char));
    return(true);
}  // end NormDecoder::Init()


void NormDecoder::Destroy()
{
    if (scratch)
    {
        delete scratch;
        scratch = NULL;
    }
    if(oVec)
    {
	    for(int i=0; i<npar; i++)
	        if (oVec[i]) delete oVec[i];
	    delete oVec;
        oVec = NULL;
    }	
    if(sVec)
    {
	    for(int i = 0; i < npar; i++)
	        if (sVec[i]) delete sVec[i];
	    delete sVec;
        sVec = NULL;
    }
    if (Lambda)
    {
        
        delete Lambda;
        Lambda = NULL;
    }
}  // end NormDecoder::Destroy()


// This will crash & burn if (erasureCount > npar)
int NormDecoder::Decode(char** dVec, int ndata, UINT16 erasureCount, UINT16* erasureLocs)
{
    // Debugging assertions
    ASSERT(Lambda);     
    ASSERT(erasureCount && (erasureCount<=npar));
    
    
    // (A) Compute syndrome vectors 
    
    // First zero out erasure vectors (MDP provides zero-filled vecs) 
	
	// Then calculate syndrome (based on zero value erasures) 
    int nvecs = npar + ndata;
#ifdef SIMULATE
    int vecSize = MIN(SIM_PAYLOAD_MAX, vector_size);
    vecSize = MAX(NormDataMsg::PayloadHeaderLength(), vecSize);
#else
    int vecSize = vector_size;
#endif // if/else SIMUATE
    
    int i;
    for (i = 0; i < npar; i++)
    {
	    int X = gexp(i+1);
	    unsigned char* synVec = sVec[i];
	    memset(synVec, 0, vecSize*sizeof(char));
	    for(int j = 0; j < nvecs; j++)
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
    
    // (B) Init Lambda (the erasure locator polynomial) 
    int degree = 2*npar;
    int nvecsMinusOne = nvecs - 1;
    memset(Lambda, 0, degree*sizeof(char));
    Lambda[0] = 1;
    for (i = 0; i < erasureCount; i++)
    {
	    int X = gexp(nvecsMinusOne - erasureLocs[i]);
	    for(int j = (degree-1); j > 0; j--)
	        Lambda[j] = Lambda[j] ^ gmult(X, Lambda[j-1]);
    }

    // (C) Compute modified Omega using Lambda 
    for(i = 0; i < npar; i++)
    {
	    int k = i;
	    memset(oVec[i], 0, vecSize*sizeof(char));
        int m = i + 1;
	    for(int j = 0; j < m; j++)
	    { 
	        unsigned char* Omega = oVec[i];
	        unsigned char* S = sVec[j];
	        int Lk = Lambda[k--];
	        for(int n = 0; n < vecSize; n++)
		        *Omega++ ^= gmult(*S++, Lk);
	    }
    }

    // (D) Finally, fill in the erasures 
    for (i = 0; i < erasureCount; i++)
    {       
        // Only fill _data_ erasures
        if (erasureLocs[i] >= ndata) break;//return erasureCount;
        
        // evaluate Lambda' (derivative) at alpha^(-i) 
	    // ( all odd powers disappear) 
	    int k = nvecsMinusOne - erasureLocs[i];
	    int denom = 0;
        int j;
	    for (j = 1; j < degree; j += 2)
	        denom ^= gmult(Lambda[j], gexp(((255-k)*(j-1)) % 255));
	    // Invert for use computing errror value below 
	    denom = ginv(denom);

	    // Now evaluate Omega at alpha^(-i) (numerator) 
	    unsigned char* eVec = (unsigned char*)dVec[erasureLocs[i]];
	    for (j = 0; j < npar; j++)
	    {
            unsigned char* data = eVec;
	        unsigned char* Omega = oVec[j];
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
}  // end NormDecoder::Decode()



