#ifndef _NORM_ENCODER_RS8
#define _NORM_ENCODER_RS8

#include "protoDebug.h"
#include "protoDefs.h"  // for UINT16

// (TBD) We're going to need to have a "NormEncoder" base class and then allow for variants

class NormEncoder
{
    // Methods
    public:
	    NormEncoder();
	    ~NormEncoder();
	    bool Init(int numData, int numParity, int vectorSize);
        void Destroy();
        
        // "Encode" must be called in order of source vector0, vector1, vector2, etc
	    void Encode(unsigned char *dataVector, unsigned char **parityVectorList);
        int GetNumData() {return ndata;}
	    int GetNumParity() {return npar;}
	    int GetVectorSize() {return vector_size;}
	
    // Members
    private:
        int             ndata;        // max data pkts per block (k)
	    int		        npar;	      // No. of parity packets (n-k)
	    int		        vector_size;  // Size of biggest vector to encode
        unsigned char*  enc_matrix;
        int             enc_index;
        
};  // end class NormEncoder



class NormDecoder
{
    public:
	    NormDecoder();
	    ~NormDecoder();
	    bool Init(int numData, int numParity, int vectorSize);
        
        // Note: "erasureLocs" must be in order from lowest to highest!
	    int Decode(unsigned char** vectorList, int ndata,  UINT16 erasureCount, UINT16* erasureLocs);
        int NumParity() {return npar;}
	    int VectorSize() {return vector_size;}
        void Destroy();
        
    // Members
    private:
        bool InvertDecodingMatrix();   // used in Decode() method
            
            
        int             ndata;        // max data pkts per block (k)
	    int		        npar;	      // No. of parity packets (n-k)
	    int		        vector_size;  // Size of biggest vector to encode
        unsigned char*  enc_matrix;
        unsigned char*  dec_matrix;
        int*            parity_loc;
        
        // These "inv_" members are used in InvertDecodingMatrix()
        int*            inv_ndxc;
        int*            inv_ndxr;
        int*            inv_pivt;   
        unsigned char*  inv_id_row;
        unsigned char*  inv_temp_row;
             
};  // end class NormDecoder

#endif // _NORM_ENCODER_RS8
