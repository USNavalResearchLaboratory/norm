/*
 * This includes forward error correction code based on Vandermonde matrices
 * 980624
 * (C) 1997-98 Luigi Rizzo (luigi@iet.unipi.it)
 *
 * Portions derived from code by Phil Karn (karn@ka9q.ampr.org),
 * Robert Morelos-Zaragoza (robert@spectra.eng.hawaii.edu) and Hari
 * Thirumoorthy (harit@spectra.eng.hawaii.edu), Aug 1995
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */


#include "normEncoderRS8.h"
#include "protoDebug.h"

#ifdef SIMULATE
#include "normMessage.h" 
#endif // SIMULATE

/*
 * The first part of the file here implements linear algebra in GF.
 *
 * gf is the type used to store an element of the Galois Field.
 * Must constain at least GF_BITS bits.
 *
 * Note: unsigned char will work up to GF(256) but int seems to run
 * faster on the Pentium. We use int whenever have to deal with an
 * index, since they are generally faster.
 */
 
#define GF_BITS  8	                    // 8-bit RS code
#if (GF_BITS < 2)  || (GF_BITS > 16)
#error "GF_BITS must be 2 .. 16"
#endif
#if (GF_BITS <= 8)
typedef UINT8 gf;
#else
typedef UINT16 gf;
#endif
#define	GF_SIZE ((1 << GF_BITS) - 1)	// powers of alpha 


/*
 * Primitive polynomials - see Lin & Costello, Appendix A,
 * and  Lee & Messerschmitt, p. 453.
 */

static const char *allPp[] = 
{                        // GF_BITS   Polynomial	
    NULL,		         //   0      no code                 
    NULL,		         //   1      no code                 
    "111",		         //   2      1+x+x^2                 
    "1101",		         //   3      1+x+x^3                 
    "11001",		     //   4      1+x+x^4                 
    "101001",		     //   5      1+x^2+x^5               
    "1100001",		     //   6      1+x+x^6                 
    "10010001",		     //   7      1 + x^3 + x^7           
    "101110001",	     //   8      1+x^2+x^3+x^4+x^8       
    "1000100001",	     //   9      1+x^4+x^9               
    "10010000001",	     //  10      1+x^3+x^10              
    "101000000001",	     //  11      1+x^2+x^11              
    "1100101000001",	 //  12      1+x+x^4+x^6+x^12        
    "11011000000001",	 //  13      1+x+x^3+x^4+x^13        
    "110000100010001",	 //  14      1+x+x^6+x^10+x^14       
    "1100000000000001",  //  15      1+x+x^15                
    "11010000000010001"  //  16      1+x+x^3+x^12+x^16       
};


/*
 * To speed up computations, we have tables for logarithm, exponent
 * and inverse of a number. If GF_BITS <= 8, we use a table for
 * multiplication as well (it takes 64K, no big deal even on a PDA,
 * especially because it can be pre-initialized an put into a ROM!),
 * otherwhise we use a table of logarithms.
 * In any case the macro gf_mul(x,y) takes care of multiplications.
 */

static gf gf_exp[2*GF_SIZE];	// index->poly form conversion table	
static int gf_log[GF_SIZE + 1];	// Poly->index form conversion table	
static gf inverse[GF_SIZE+1];	// inverse of field elem.		
				                // inv[\alpha**i]=\alpha**(GF_SIZE-i-1)	

// modnn(x) computes x % GF_SIZE, where GF_SIZE is 2**GF_BITS - 1,
// without a slow divide.
static inline gf modnn(int x)
{
    while (x >= GF_SIZE) 
    {
	    x -= GF_SIZE;
	    x = (x >> GF_BITS) + (x & GF_SIZE);
    }
    return x;
}  // end modnn()

#define SWAP(a,b,t) {t tmp; tmp=a; a=b; b=tmp;}

/*
 * gf_mul(x,y) multiplies two numbers. If GF_BITS<=8, it is much
 * faster to use a multiplication table.
 *
 * USE_GF_MULC, GF_MULC0(c) and GF_ADDMULC(x) can be used when multiplying
 * many numbers by the same constant. In this case the first
 * call sets the constant, and others perform the multiplications.
 * A value related to the multiplication is held in a local variable
 * declared with USE_GF_MULC . See usage in addmul1().
 */
#if (GF_BITS <= 8)

static gf gf_mul_table[GF_SIZE + 1][GF_SIZE + 1];

#define gf_mul(x,y) gf_mul_table[x][y]
#define USE_GF_MULC gf * __gf_mulc_
#define GF_MULC0(c) __gf_mulc_ = gf_mul_table[c]
#define GF_ADDMULC(dst, x) dst ^= __gf_mulc_[x]

static void init_mul_table()
{
    for (int i = 0; i <= GF_SIZE; i++)
    {
	    for (int j = 0; j <= GF_SIZE; j++)
	        gf_mul_table[i][j] = gf_exp[modnn(gf_log[i] + gf_log[j]) ] ;
    }
    for (int j = 0; j <= GF_SIZE; j++)
	    gf_mul_table[0][j] = gf_mul_table[j][0] = 0;
}

#else	/* GF_BITS > 8 */

inline gf gf_mul(int x, int y)
{
    if ((0 == x) || (0 == y)) return 0;
    return gf_exp[gf_log[x] + gf_log[y] ] ;
}

#define init_mul_table()
#define USE_GF_MULC register gf * __gf_mulc_
#define GF_MULC0(c) __gf_mulc_ = &gf_exp[ gf_log[c] ]
#define GF_ADDMULC(dst, x) { if (x) dst ^= __gf_mulc_[ gf_log[x] ] ; }

#endif  // if/else (GF_BITS <= 8)

/*
 * Generate GF(2**m) from the irreducible polynomial p(X) in p[0]..p[m]
 * Lookup tables:
 *     index->polynomial form		gf_exp[] contains j= \alpha^i;
 *     polynomial form -> index form	gf_log[ j = \alpha^i ] = i
 * \alpha=x is the primitive element of GF(2^m)
 *
 * For efficiency, gf_exp[] has size 2*GF_SIZE, so that a simple
 * multiplication of two numbers can be resolved without calling modnn
 */
         
#define NEW_GF_MATRIX(rows, cols) (new gf[rows*cols])

/*
 * initialize the data structures used for computations in GF.
 */
static void generate_gf()
{
    const char *Pp =  allPp[GF_BITS] ;

    gf mask = 1;	/* x ** 0 = 1 */
    gf_exp[GF_BITS] = 0; /* will be updated at the end of the 1st loop */
    /*
     * first, generate the (polynomial representation of) powers of \alpha,
     * which are stored in gf_exp[i] = \alpha ** i .
     * At the same time build gf_log[gf_exp[i]] = i .
     * The first GF_BITS powers are simply bits shifted to the left.
     */
    for (int i = 0; i < GF_BITS; i++, mask <<= 1 ) 
    {
	    gf_exp[i] = mask;
	    gf_log[gf_exp[i]] = i;
	    /*
	     * If Pp[i] == 1 then \alpha ** i occurs in poly-repr
	     * gf_exp[GF_BITS] = \alpha ** GF_BITS
	     */
	    if ( Pp[i] == '1' )
	        gf_exp[GF_BITS] ^= mask;
    }
    /*
     * now gf_exp[GF_BITS] = \alpha ** GF_BITS is complete, so can als
     * compute its inverse.
     */
    gf_log[gf_exp[GF_BITS]] = GF_BITS;
    /*
     * Poly-repr of \alpha ** (i+1) is given by poly-repr of
     * \alpha ** i shifted left one-bit and accounting for any
     * \alpha ** GF_BITS term that may occur when poly-repr of
     * \alpha ** i is shifted.
     */
    mask = 1 << (GF_BITS - 1 ) ;
    for (int i = GF_BITS + 1; i < GF_SIZE; i++) 
    {
	    if (gf_exp[i - 1] >= mask)
	        gf_exp[i] = gf_exp[GF_BITS] ^ ((gf_exp[i - 1] ^ mask) << 1);
	    else
	        gf_exp[i] = gf_exp[i - 1] << 1;
	    gf_log[gf_exp[i]] = i;
    }
    /*
     * log(0) is not defined, so use a special value
     */
    gf_log[0] =	GF_SIZE ;
    /* set the extended gf_exp values for fast multiply */
    for (int i = 0 ; i < GF_SIZE ; i++)
	    gf_exp[i + GF_SIZE] = gf_exp[i] ;

    /*
     * again special cases. 0 has no inverse. This used to
     * be initialized to GF_SIZE, but it should make no difference
     * since noone is supposed to read from here.
     */
    inverse[0] = 0 ;
    inverse[1] = 1;
    for (int i = 2; i <= GF_SIZE; i++)
	    inverse[i] = gf_exp[GF_SIZE-gf_log[i]];
}  // end generate_gf()       
         
         
/*
 * Various linear algebra operations that i use often.
 */

/*
 * addmul() computes dst[] = dst[] + c * src[]
 * This is used often, so better optimize it! Currently the loop is
 * unrolled 16 times, a good value for 486 and pentium-class machines.
 * The case c=0 is also optimized, whereas c=1 is not. These
 * calls are unfrequent in my typical apps so I did not bother.
 * 
 * Note that gcc on
 */
#define addmul(dst, src, c, sz) \
    if (c != 0) addmul1(dst, src, c, sz)
#define UNROLL 16 /* 1, 4, 8, 16 */

static void addmul1(gf* dst1, gf* src1, gf c, int sz)
{
    USE_GF_MULC ;
    gf* dst = dst1;
    gf* src = src1 ;
    gf* lim = &dst[sz - UNROLL + 1] ;
    
    GF_MULC0(c) ;

#if (UNROLL > 1) /* unrolling by 8/16 is quite effective on the pentium */
    for (; dst < lim ; dst += UNROLL, src += UNROLL ) 
    {
        GF_ADDMULC( dst[0] , src[0] );
	    GF_ADDMULC( dst[1] , src[1] );
	    GF_ADDMULC( dst[2] , src[2] );
	    GF_ADDMULC( dst[3] , src[3] );
#if (UNROLL > 4)
	    GF_ADDMULC( dst[4] , src[4] );
	    GF_ADDMULC( dst[5] , src[5] );
	    GF_ADDMULC( dst[6] , src[6] );
	    GF_ADDMULC( dst[7] , src[7] );
#endif
#if (UNROLL > 8)
	    GF_ADDMULC( dst[8] , src[8] );
	    GF_ADDMULC( dst[9] , src[9] );
	    GF_ADDMULC( dst[10] , src[10] );
	    GF_ADDMULC( dst[11] , src[11] );
	    GF_ADDMULC( dst[12] , src[12] );
	    GF_ADDMULC( dst[13] , src[13] );
	    GF_ADDMULC( dst[14] , src[14] );
        GF_ADDMULC( dst[15] , src[15] );
#endif
    }
#endif
    lim += UNROLL - 1 ;
    for (; dst < lim; dst++, src++ )		/* final components */
	    GF_ADDMULC( *dst , *src );
}  // end addmul1()


// computes C = AB where A is n*k, B is k*m, C is n*m
static void matmul(gf* a, gf* b, gf* c, int n, int k, int m)
{
    int row, col, i ;

    for (row = 0; row < n ; row++) 
    {
	    for (col = 0; col < m ; col++) 
        {
	        gf* pa = &a[ row * k ];
	        gf* pb = &b[ col ];
	        gf acc = 0 ;
	        for (i = 0; i < k ; i++, pa++, pb += m)
		        acc ^= gf_mul( *pa, *pb ) ;
	        c[row * m + col] = acc ;
	    }
    }
}  // end matmul()


static int invert_vdm(gf* src, int k)
{
    gf t, xx;
    
    if (k == 1)return 0; // degenerate case, matrix must be p^0 = 1
	    
    /*
     * c holds the coefficient of P(x) = Prod (x - p_i), i=0..k-1
     * b holds the coefficient for the matrix inversion
     */
    gf* c = NEW_GF_MATRIX(1, k);
    gf* b = NEW_GF_MATRIX(1, k);
    gf* p = NEW_GF_MATRIX(1, k);
   
    int i, j;
    for (j = 1, i = 0 ; i < k ; i++, j+=k ) 
    {
	    c[i] = 0 ;
	    p[i] = src[j] ;    /* p[i] */
    }
    /*
     * construct coeffs. recursively. We know c[k] = 1 (implicit)
     * and start P_0 = x - p_0, then at each stage multiply by
     * x - p_i generating P_i = x P_{i-1} - p_i P_{i-1}
     * After k steps we are done.
     */
    c[k-1] = p[0] ;	/* really -p(0), but x = -x in GF(2^m) */
    for (i = 1 ; i < k ; i++) 
    {
	    gf p_i = p[i] ; /* see above comment */
	    for (j = k - 1  - ( i - 1 ) ; j < k-1 ; j++ )
	        c[j] ^= gf_mul( p_i, c[j+1] ) ;
	    c[k-1] ^= p_i ;
    }
    for (int row = 0 ; row < k ; row++) 
    {
	    /*
	     * synthetic division etc.
	     */
	    xx = p[row] ;
	    t = 1 ;
	    b[k-1] = 1 ; /* this is in fact c[k] */
	    for (i = k - 2 ; i >= 0 ; i-- ) 
        {
	        b[i] = c[i+1] ^ gf_mul(xx, b[i+1]) ;
	        t = gf_mul(xx, t) ^ b[i] ;
	    }
	    for (int col = 0 ; col < k ; col++ )
	        src[col*k + row] = gf_mul(inverse[t], b[col] );
    }
    delete[] c;
    delete[] b;
    delete[] p;
    return 0 ;
}  // end invert_vdm()


static bool fec_initialized = false;
static void init_fec()
{
    if (!fec_initialized)
    {
        generate_gf();
        init_mul_table();
        fec_initialized = true;
    }
}

NormEncoderRS8::NormEncoderRS8()
 : enc_matrix(NULL)
{
}

NormEncoderRS8::~NormEncoderRS8()
{
    Destroy();
}

bool NormEncoderRS8::Init(unsigned int numData, unsigned int numParity, UINT16 vecSizeMax)
{
#ifdef SIMULATE
    vecSizeMax = MIN(SIM_PAYLOAD_MAX, vecSizeMax);
#endif // SIMULATE
    if ((numData + numParity) > GF_SIZE)
    {
        PLOG(PL_FATAL, "NormEncoderRS8::Init() error: numData/numParity exceeds code limits\n");
        return false;
    }
    
    if (NULL != enc_matrix) 
    {
        delete[] enc_matrix;
        enc_matrix = NULL;
    }
    init_fec();
    int n = numData + numParity;
    int k = numData;
    enc_matrix = (UINT8*)NEW_GF_MATRIX(n, k);
    if (NULL != enc_matrix)
    {
        gf* tmpMatrix = NEW_GF_MATRIX(n, k);
        if (NULL == tmpMatrix)
        {
            PLOG(PL_FATAL, "NormEncoderRS8::Init() error: new  tmpMatrix error: %s\n", GetErrorString());
            delete[] enc_matrix;
            enc_matrix = NULL;
            return false;
        }
        // Fill the matrix with powers of field elements, starting from 0.
        // The first row is special, cannot be computed with exp. table.
        tmpMatrix[0] = 1 ;
        for (int col = 1; col < k ; col++)
	        tmpMatrix[col] = 0 ;
        for (gf* p = tmpMatrix + k, row = 0; row < n-1 ; row++, p += k) 
        {
	        for (int col = 0 ; col < k ; col ++ )
	            p[col] = gf_exp[modnn(row*col)];
        }
        
        
        // Quick code to build systematic matrix: invert the top
        // k*k vandermonde matrix, multiply right the bottom n-k rows
        // by the inverse, and construct the identity matrix at the top.
        invert_vdm(tmpMatrix, k); /* much faster than invert_mat */
        matmul(tmpMatrix + k*k, tmpMatrix, ((gf*)enc_matrix) + k*k, n - k, k, k);
        // the upper matrix is I so do not bother with a slow multiply
        memset(enc_matrix, 0, k*k*sizeof(gf));
        for (gf* p = (gf*)enc_matrix, col = 0 ; col < k ; col++, p += k+1 )
	        *p = 1 ;
        delete[] tmpMatrix;
        ndata = numData;
        npar = numParity;
        vector_size = vecSizeMax;
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormEncoderRS8::Init() error: new enc_matrix error: %s\n", GetErrorString());
        return false;
    }
}  // end NormEncoderRS8::Init()

void NormEncoderRS8::Destroy()
{
    if (NULL != enc_matrix)
    {
        delete[] enc_matrix;
        enc_matrix = NULL;
    }
}  // end NormEncoderRS8::Destroy()

void NormEncoderRS8::Encode(unsigned int segmentId, const char* dataVector, char** parityVectorList)
{
    for (unsigned int i = 0; i < npar; i++)
    {
        // Update each parity vector   
        gf* fec = (gf*)parityVectorList[i];
        gf* p = ((gf*)enc_matrix) + ((i+ndata)*ndata);
        unsigned int nelements = (GF_BITS > 8) ? vector_size / 2 : vector_size;
        addmul(fec, (gf*)dataVector, p[segmentId], nelements);
    }
}  // end NormEncoderRS8::Encode()


NormDecoderRS8::NormDecoderRS8()
 : enc_matrix(NULL), dec_matrix(NULL), 
   parity_loc(NULL), inv_ndxc(NULL), inv_ndxr(NULL), 
   inv_pivt(NULL), inv_id_row(NULL), inv_temp_row(NULL)
{
}

NormDecoderRS8::~NormDecoderRS8()
{
    Destroy();
}

void NormDecoderRS8::Destroy()
{
    if (NULL != enc_matrix)
    {
        delete[] enc_matrix;
        enc_matrix = NULL;
    }
    if (NULL != dec_matrix)
    {
        delete[] dec_matrix;
        dec_matrix = NULL;
    }
    if (NULL != parity_loc)
    {
        delete[] parity_loc;
        parity_loc = NULL;
    }
    if (NULL != inv_ndxc)
    {
        delete[] inv_ndxc;
        inv_ndxc = NULL;
    }
    if (NULL != inv_ndxr)
    {
        delete[] inv_ndxr;
        inv_ndxr = NULL;
    }
    if (NULL != inv_pivt)
    {
        delete[] inv_pivt;
        inv_pivt = NULL;
    }
    if (NULL != inv_id_row)
    {
        delete[] inv_id_row;
        inv_id_row = NULL;
    }
    if (NULL != inv_temp_row)
    {
        delete[] inv_temp_row;
        inv_temp_row = NULL;
    }
}  // end NormDecoderRS8::Destroy()

bool NormDecoderRS8::Init(unsigned int numData, unsigned int numParity, UINT16 vecSizeMax)
{
#ifdef SIMULATE
    vecSizeMax = MIN(SIM_PAYLOAD_MAX, vecSizeMax);
#endif // SIMULATE    
    if ((numData + numParity) > GF_SIZE)
    {
        PLOG(PL_FATAL, "NormEncoderRS8::Init() error: numData/numParity exceeds code limits\n");
        return false;
    }
    
    init_fec();
    Destroy();
    
    int n = numData + numParity;
    int k = numData;
    
    if (NULL == (inv_ndxc = new unsigned int[k]))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() new inv_indxc error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (inv_ndxr = new unsigned int[k]))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() new inv_inv_ndxr error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (inv_pivt = new unsigned int[k]))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() new inv_pivt error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (inv_id_row = (UINT8*)NEW_GF_MATRIX(1, k)))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() new inv_id_row error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (inv_temp_row = (UINT8*)NEW_GF_MATRIX(1, k)))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() new inv_temp_row error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (parity_loc = new unsigned int[numParity]))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() error: new parity_loc error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (dec_matrix = (UINT8*)NEW_GF_MATRIX(k, k)))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() error: new dec_matrix error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    if (NULL == (enc_matrix = (UINT8*)NEW_GF_MATRIX(n, k)))
    {
        PLOG(PL_FATAL, "NormDecoderRS8::Init() error: new enc_matrix error: %s\n", GetErrorString());
        Destroy();
        return false;
    }
    
    
    gf* tmpMatrix = NEW_GF_MATRIX(n, k);
    if (NULL == tmpMatrix)
    {
        PLOG(PL_FATAL, "NormEncoderRS8::Init() error: new  tmpMatrix error: %s\n", GetErrorString());
        delete[] enc_matrix;
        enc_matrix = NULL;
        return false;
    }
    // Fill the matrix with powers of field elements, starting from 0.
    // The first row is special, cannot be computed with exp. table.
    tmpMatrix[0] = 1 ;
    for (int col = 1; col < k ; col++)
	    tmpMatrix[col] = 0 ;
    for (gf* p = tmpMatrix + k, row = 0; row < n-1 ; row++, p += k) 
    {
	    for (int col = 0 ; col < k ; col ++ )
	        p[col] = gf_exp[modnn(row*col)];
    }

    // Quick code to build systematic matrix: invert the top
    // k*k vandermonde matrix, multiply right the bottom n-k rows
    // by the inverse, and construct the identity matrix at the top.
    invert_vdm(tmpMatrix, k); /* much faster than invert_mat */
    matmul(tmpMatrix + k*k, tmpMatrix, ((gf*)enc_matrix) + k*k, n - k, k, k);
    // the upper matrix is I so do not bother with a slow multiply
    memset(enc_matrix, 0, k*k*sizeof(gf));
    for (gf* p = (gf*)enc_matrix, col = 0 ; col < k ; col++, p += k+1 )
	    *p = 1 ;
    delete[] tmpMatrix;
    ndata = numData;
    npar = numParity;
    vector_size = vecSizeMax;
    return true;
}  // end NormDecoderRS8::Init()


int NormDecoderRS8::Decode(char** vectorList, unsigned int numData,  unsigned int erasureCount, unsigned int* erasureLocs)
{
    unsigned int bsz = ndata + npar;
    // 1) Build decoding matrix for the given set of segments & erasures
    unsigned int nextErasure = 0;
    unsigned int ne = 0;
    unsigned int sourceErasureCount = 0;
    unsigned int parityCount = 0;
    for (unsigned int i = 0;  i < bsz; i++)
    {   
        if (i < numData)
        {
            if ((nextErasure < erasureCount) && (i == erasureLocs[nextErasure]))
            {
                nextErasure++;
                sourceErasureCount++;
            }     
            else
            {
                // set identity row for segments we have
                gf* p = ((gf*)dec_matrix) + ndata*i;
                memset(p, 0, ndata*sizeof(gf));
                p[i] = 1;
            }
        }
        else if (i < ndata)
        {
            // set identity row for assumed zero segments (shortened code)
            gf* p = ((gf*)dec_matrix) + ndata*i;
            memset(p, 0, ndata*sizeof(gf));
            p[i] = 1;      
            // Also keep track of where the non-erased parity segment are
            // for the shortened code
            if ((nextErasure < erasureCount) && (i == erasureLocs[nextErasure]))
            {
                nextErasure++;
            }
            else if (parityCount < sourceErasureCount)
            {
                parity_loc[parityCount++] = i;
                // Copy appropriate enc_matric parity row to dec_matrix erasureRow
                gf* p = ((gf*)dec_matrix) + ndata*erasureLocs[ne++];  
                memcpy(p, ((gf*)enc_matrix) + (ndata-numData+i)*ndata, ndata*sizeof(gf)); 
            }
                
        }
        else if (parityCount < sourceErasureCount)
        {
            if ((nextErasure < erasureCount) && (i == erasureLocs[nextErasure]))
            {
                nextErasure++;
            }
            else
            {
                ASSERT(parityCount < npar);
                parity_loc[parityCount++] = i;
                // Copy appropriate enc_matric parity row to dec_matrix erasureRow
                gf* p = ((gf*)dec_matrix) + ndata*erasureLocs[ne++];  
                memcpy(p, ((gf*)enc_matrix) + (ndata-numData+i)*ndata, ndata*sizeof(gf)); 
            }
        }
        else
        {
            break;
        }
        
    }
    ASSERT(ne == sourceErasureCount);
    // 2) Invert the decoding matrix
    if (!InvertDecodingMatrix()) 
    {
	    PLOG(PL_FATAL, "NormDecoderRS8::Decode() error: couldn't invert dec_matrix ?!\n");
        return 0;
    }
    
    // 3) Decode
    for (unsigned int e = 0; e < erasureCount; e++)
    {
        // Calculate missing segments (erasures) using dec_matrix and non-erasures
        unsigned int row = erasureLocs[e];
        if (row >= numData) break; // don't bother filling in parity segments
        unsigned int col = 0;
        unsigned int nextErasure = 0;
        unsigned int nelements = (GF_BITS > 8) ? vector_size/2 : vector_size;
        for (unsigned int i  = 0; i < numData; i++)
        {
            if ((nextErasure < erasureCount) && (i == erasureLocs[nextErasure]))
            {
                // Use parity segments in place of erased vector in decoding
                addmul((gf*)vectorList[row], (gf*)vectorList[parity_loc[nextErasure]], ((gf*)dec_matrix)[row*ndata + col], nelements);
                col++;
                nextErasure++;  // point to next erasure
            }
            else if (i < numData)
            {
                addmul((gf*)vectorList[row], (gf*)vectorList[i], ((gf*)dec_matrix)[row*ndata + col], nelements);
                col++;
            }
            else
            {
                ASSERT(0);
            }
        }
    } 
    return erasureCount ; 
}  // end NormDecoderRS8::Decode()



/*
 * NormDecoderRS8::InvertDecodingMatrix() takes a matrix and produces its inverse
 * k is the size of the matrix. (Gauss-Jordan, adapted from Numerical Recipes in C)
 * Return non-zero if singular.
 */
bool NormDecoderRS8::InvertDecodingMatrix()
{
    gf* src = (gf*)dec_matrix;
    unsigned int k = ndata;
    
    memset(inv_id_row, 0, k*sizeof(gf));
    // inv_pivt marks elements already used as pivots.
    memset(inv_pivt, 0, k*sizeof(unsigned int));

    for (unsigned int col = 0; col < k ; col++) 
    {
	    /*
	     * Zeroing column 'col', look for a non-zero element.
	     * First try on the diagonal, if it fails, look elsewhere.
	     */
	    int irow = -1;
        int icol = -1 ;
	    if (inv_pivt[col] != 1 && src[col*k + col] != 0) 
        {
	        irow = col ;
	        icol = col ;
	        goto found_piv ;
	    }
	    for (unsigned int row = 0 ; row < k ; row++) 
        {
	        if (inv_pivt[row] != 1) 
            {
		        for (unsigned int ix = 0 ; ix < k ; ix++) 
                {
		            if (inv_pivt[ix] == 0) 
                    {
			            if (src[row*k + ix] != 0) 
                        {
			                irow = row ;
			                icol = ix ;
			                goto found_piv ;
			            }
		            } 
                    else if (inv_pivt[ix] > 1) 
                    {
			            PLOG(PL_FATAL, "NormDecoderRS8::InvertDecodingMatrix() error: singular matrix!\n");
			            return false; 
		            }
		        }
	        }
	    }
	    if (icol == -1) 
        {
            PLOG(PL_FATAL, "NormDecoderRS8::InvertDecodingMatrix() error: pivot not found!\n");
	        return false;
	    }
    found_piv:
	    ++(inv_pivt[icol]) ;
	    /*
	     * swap rows irow and icol, so afterwards the diagonal
	     * element will be correct. Rarely done, not worth
	     * optimizing.
	    */
	    if (irow != icol) 
        {
	        for (unsigned int ix = 0 ; ix < k ; ix++ ) 
		        SWAP(src[irow*k + ix], src[icol*k + ix], gf);
	    }
	    inv_ndxr[col] = irow ;
	    inv_ndxc[col] = icol ;
	    gf* pivotRow = &src[icol*k] ;
	    gf c = pivotRow[icol] ;
	    if (c == 0) 
        {
	        PLOG(PL_FATAL, "NormDecoderRS8::InvertDecodingMatrix() error: singular matrix!\n");
	        return false; 
	    }
	    if (c != 1 ) /* otherwhise this is a NOP */
        { 
	        /*
	         * this is done often , but optimizing is not so
	         * fruitful, at least in the obvious ways (unrolling)
	         */
	        c = inverse[ c ] ;
	        pivotRow[icol] = 1 ;
	        for (unsigned int ix = 0 ; ix < k ; ix++ )
		        pivotRow[ix] = gf_mul(c, pivotRow[ix] );
	    }
	    /*
	     * from all rows, remove multiples of the selected row
	     * to zero the relevant entry (in fact, the entry is not zero
	     * because we know it must be zero).
	     * (Here, if we know that the pivot_row is the identity,
	     * we can optimize the addmul).
	     */
	    inv_id_row[icol] = 1;
	    if (0 != memcmp(pivotRow, inv_id_row, k*sizeof(gf))) 
        {
	        for (gf* p = src, ix = 0 ; ix < k ; ix++, p += k ) 
            {
		        if (ix != icol) 
                {
		            c = p[icol] ;
		            p[icol] = 0 ;
		            addmul(p, pivotRow, c, k );
		        }
	        }
	    }
	    inv_id_row[icol] = 0;
    }  // end for (col = 0; col < k ; col++) 
    
    for (int col = k - 1 ; col >= 0 ; col-- ) 
    {
	    if (inv_ndxr[col] >= k)
        {
	        PLOG(PL_ERROR, "NormDecoderRS8::InvertDecodingMatrix() error: AARGH, inv_ndxr[col] %d\n", inv_ndxr[col]);
        }
	    else if (inv_ndxc[col] >= k)
        {
	        PLOG(PL_ERROR, "NormDecoderRS8::InvertDecodingMatrix() error: AARGH, indxc[col] %d\n", inv_ndxc[col]);
        }
	    else if (inv_ndxr[col] != inv_ndxc[col] ) 
        {
	        for (unsigned int row = 0 ; row < k ; row++ ) 
		        SWAP( src[row*k + inv_ndxr[col]], src[row*k + inv_ndxc[col]], gf) ;
	    }
    }
    return true;
}  // end NormDecoderRS8::InvertDecodingMatrix()

