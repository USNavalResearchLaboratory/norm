#ifndef _NORM_ENCODER_RS16
#define _NORM_ENCODER_RS16

#include "normEncoder.h"
#include "protoDefs.h"  // for UINT16

class NormEncoderRS16 : public NormEncoder
{
    public:
	    NormEncoderRS16();
	    ~NormEncoderRS16();
        
        virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize);
        virtual void Destroy();
        virtual void Encode(unsigned int segmentId, const char* dataVector, char** parityVectorList);    
        
        unsigned int GetNumData() 
            {return ndata;}
	    unsigned int GetNumParity() 
            {return npar;}
	    unsigned int GetVectorSize() 
            {return vector_size;}
	
    private:
        unsigned int    ndata;        // max data pkts per block (k)
	    unsigned int    npar;	      // No. of parity packets (n-k)
	    unsigned int    vector_size;  // Size of biggest vector to encode
        UINT8*          enc_matrix;
        unsigned int    enc_index;
        
};  // end class NormEncoderRS16


class NormDecoderRS16 : public NormDecoder
{
    public:
	    NormDecoderRS16();
        virtual ~NormDecoderRS16();
        virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize);
        virtual void Destroy();
        virtual int Decode(char** vectorList, unsigned int numData,  unsigned int erasureCount, unsigned int* erasureLocs);
        
        unsigned int GetNumParity() 
            {return npar;}
	    unsigned int GetVectorSize() 
            {return vector_size;}
        
    private:
        bool InvertDecodingMatrix();   // used in Decode() method
            
        unsigned int    ndata;        // max data pkts per block (k)
	    unsigned int    npar;	      // No. of parity packets (n-k)
	    UINT16          vector_size;  // Size of biggest vector to encode
        UINT8*          enc_matrix;
        UINT8*          dec_matrix;
        unsigned int*   parity_loc;
        
        // These "inv_" members are used in InvertDecodingMatrix()
        unsigned int*   inv_ndxc;
        unsigned int*   inv_ndxr;
        unsigned int*   inv_pivt;   
        UINT8*          inv_id_row;
        UINT8*          inv_temp_row;
             
};  // end class NormDecoderRS16


#endif // _NORM_ENCODER_RS16
