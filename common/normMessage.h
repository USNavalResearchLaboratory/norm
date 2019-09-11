#ifndef _NORM_MESSAGE
#define _NORM_MESSAGE

// PROTOLIB includes
#include "protokit.h"

// standard includes
#include <string.h>  // for memcpy(), etc
#include <math.h>
#include <stdlib.h>  // for rand(), etc

#ifdef _WIN32_WCE
#include <stdio.h>
typedef fpos_t off_t;
#else
#include <sys/types.h>  // for off_t
#endif // if/else _WIN32_WCE

#ifdef SIMULATE
#define SIM_PAYLOAD_MAX (36+8)   // MGEN message size + StreamPayloadHeaderLen()
#endif // SIMULATE

const UINT8 NORM_PROTOCOL_VERSION = 1;

// (TBD) Alot of the "memcpy()" calls could be eliminated by
//       taking advantage of the alignment of NORM messsages

const int NORM_ROBUST_FACTOR = 20;  // default robust factor

// Pick a random number from 0..max
inline double UniformRand(double max)
{
  return (max * ((double)rand() / (double)RAND_MAX));
}

// Pick a random number from 0..max
// (truncated exponential dist. lambda = log(groupSize) + 1)
inline double ExponentialRand(double max, double groupSize)
{
    double lambda = log(groupSize) + 1;
    double x = UniformRand(lambda/max)+lambda/(max*(exp(lambda)-1));
    return ((max/lambda)*log(x*(exp(lambda)-1)*(max/lambda)));   
}

// These are the GRTT estimation bounds set for the current
// NORM protocol.  (Note that our Grtt quantization routines
// are good for the range of 1.0e-06 <= 1000.0)
const double NORM_GRTT_MIN = 0.001;  // 1 msec
const double NORM_GRTT_MAX = 15.0;   // 15 sec
const double NORM_RTT_MIN = 1.0e-06;
const double NORM_RTT_MAX = 1000.0;        
inline double NormUnquantizeRtt(unsigned char qrtt)
{
	return ((qrtt < 31) ? 
			(((double)(qrtt+1))*(double)NORM_RTT_MIN) :
		    (NORM_RTT_MAX/exp(((double)(255-qrtt))/(double)13.0)));
}
unsigned char NormQuantizeRtt(double rtt);

inline double NormUnquantizeGroupSize(unsigned char gsize)
{
    double exponent = (double)((gsize & 0x07) + 1);
    double mantissa = (0 != (gsize & 0x08)) ? 5.0 : 1.0;
    return (mantissa * pow(10.0, exponent)); 
}
inline unsigned char NormQuantizeGroupSize(double gsize)
{
    unsigned char ebits = (unsigned char)log10(gsize);
    int mantissa = (int)((gsize/pow(10.0, (double)ebits)) + 0.5);
    // round up quantized group size
    unsigned char mbit = (mantissa > 1) ? ((mantissa > 5) ? 0x00 : 0x08) : 0x00;
    ebits = (mantissa > 5) ? ebits : ebits+1;
    mbit = (ebits > 0x07) ? 0x08 : mbit;
    ebits = (ebits > 0x07) ? 0x07 : ebits;
    return (mbit | ebits);
}

inline unsigned short NormQuantizeLoss(double lossFraction)
{
    lossFraction = MAX(lossFraction, 0.0);
    lossFraction = lossFraction*65535.0 + 0.5;
    lossFraction = MIN(lossFraction, 65535.0);
    return (unsigned short)lossFraction;
}  // end NormQuantizeLossFraction()
inline double NormUnquantizeLoss(unsigned short lossQuantized)
{
    return (((double)lossQuantized) / 65535.0);
}  // end NormUnquantizeLossFraction()

inline unsigned short NormQuantizeRate(double rate)
{
    unsigned char exponent = (unsigned char)log10(rate);
    unsigned short mantissa = (unsigned short)((256.0/10.0) * rate / pow(10.0, (double)exponent));
    return ((mantissa << 8) | exponent);
}
inline double NormUnquantizeRate(unsigned short rate)
{
    double mantissa = ((double)(rate >> 8)) * (10.0/256.0);
    double exponent = (double)(rate & 0xff);
    return mantissa * pow(10.0, exponent);   
}

// This class is used to describe object "size" and/or "offset"
// (TBD) This hokey implementation should just use "off_t" as it's
// native storage format and get rid of this custom crap !!!
// (We should just get rid of this class !!!!!!!!)
class NormObjectSize
{
    public:
        NormObjectSize() : msb(0), lsb(0) {}
        NormObjectSize(UINT16 upper, UINT32 lower) : msb(upper), lsb(lower) {}
        NormObjectSize(UINT32 size) : msb(0), lsb(size) {}
        NormObjectSize(UINT16 size) : msb(0), lsb(size) {}
        NormObjectSize(const NormObjectSize& size) : msb(size.msb), lsb(size.lsb) {}
        NormObjectSize(off_t size) 
        {
            lsb = (unsigned long)(size & 0x00000000ffffffff);
            size >>= 32;
            ASSERT(0 == (size & 0xffff0000));   
            msb = (unsigned short)(size & 0x0000ffff);
            msb = 0;
        }
        
        UINT16 MSB() const {return msb;}
        UINT32 LSB() const {return lsb;}
        bool operator==(const NormObjectSize& size) const
            {return ((msb == size.msb) && (lsb == size.lsb));}
        bool operator>(const NormObjectSize& size) const
        {
            return ((msb == size.msb) ? 
                        (lsb > size.lsb) : 
                        (msb > size.msb));
        }
        bool operator<(const NormObjectSize& size) const
        {
            return ((msb == size.msb) ? 
                        (lsb < size.lsb) : 
                        (msb < size.msb));     
        }
        bool operator<=(const NormObjectSize& size) const
            {return ((*this == size) || (*this<size));}
        bool operator>=(const NormObjectSize& size) const
            {return ((*this == size) || (*this>size));}
        bool operator!=(const NormObjectSize& size) const
            {return ((lsb != size.lsb) || (msb != size.msb));}
        NormObjectSize operator+(const NormObjectSize& size) const;
        NormObjectSize operator-(const NormObjectSize& size) const;
        void operator+=(UINT32 increment)
        {
            UINT32 temp32 = lsb + increment;
            if (temp32 < lsb) msb++;
            lsb = temp32;
        }
        NormObjectSize operator+(UINT32 increment) const
        {
            NormObjectSize total;
            total.msb = msb;
            total.lsb = lsb + increment;
            if (total.lsb < lsb) total.msb++;
            return total;
        }
        NormObjectSize& operator++(int) 
        {
            lsb++;
            if (0 == lsb) msb++;
            return (*this);
        }
        NormObjectSize operator*(const NormObjectSize& b) const;
        NormObjectSize operator/(const NormObjectSize& b) const;
        off_t GetOffset() const
        {

           off_t offset = msb;
           offset <<= 32;
           offset += lsb;
           return offset;   
        }
        
    private:
        UINT16  msb;
        UINT32  lsb;  
};  // end class NormObjectSize

#ifndef _NORM_API
typedef unsigned long NormNodeId;
const NormNodeId NORM_NODE_NONE = 0x00000000;
const NormNodeId NORM_NODE_ANY  = 0xffffffff;
#endif // !_NORM_API

class NormObjectId
{
    public:
        NormObjectId() {};
        NormObjectId(UINT16 id) {value = id;}
        NormObjectId(const NormObjectId& id) {value = id.value;}
        operator UINT16() const {return value;}
        INT16 operator-(const NormObjectId& id) const
            {return ((INT16)(value - id.value));}
        bool operator<(const NormObjectId& id) const
            {return (((short)(value - id.value)) < 0);}
        bool operator>(const NormObjectId& id) const
            {return (((INT16)(value - id.value)) > 0);}
        bool operator<=(const NormObjectId& id) const
            {return ((value == id.value) || (*this<id));}
        bool operator>=(const NormObjectId& id) const
            {return ((value == id.value) || (*this>id));}
        bool operator==(const NormObjectId& id) const
            {return (value == id.value);}
        bool operator!=(const NormObjectId& id) const
            {return (value != id.value);}
        NormObjectId& operator++(int) {value++; return *this;}
        NormObjectId& operator--(int) {value--; return *this;}
        
    private:
        UINT16  value;  
};  // end class NormObjectId

class NormBlockId
{
    public:
        NormBlockId() {};
        NormBlockId(UINT32 id) {value = id;}
        NormBlockId(const NormObjectId& id) {value = (UINT32)id;}
        operator UINT32() const {return value;}
        bool operator==(const NormBlockId& id) const
            {return (value == (UINT32)id);}
        bool operator!=(const NormBlockId& id) const
            {return (value != (UINT32)id);}
        INT32 operator-(const NormBlockId& id) const
            {return ((INT32)value - id.value);}
        bool operator<(const NormBlockId& id) const
            {return (((INT32)(value-id.value)) < 0);}
        bool operator>(const NormBlockId& id) const
            {return (((INT32)(value-id.value)) > 0);}
        NormBlockId& operator++(int ) {value++; return *this;}
        
    private:
        UINT32  value;  
};  // end class NormBlockId

typedef UINT16 NormSymbolId;
typedef NormSymbolId NormSegmentId;

// Base class for NORM header extensions
class NormHeaderExtension
{
    public:
        enum Type
        {
            INVALID     =   0,
            FTI         =   1,  // FEC Object Transmission Information (FTI) extension
            CC_FEEDBACK =   2,  // NORM-CC Feedback extension
            CC_RATE     = 128   // NORM-CC Rate extension
        }; 
            
        NormHeaderExtension();
        virtual void Init(UINT32* theBuffer) 
        {
            buffer = theBuffer;
            SetType(INVALID);
            SetWords(0);
        }
        void SetType(Type type) 
            {((UINT8*)buffer)[TYPE_OFFSET] = (UINT8)type;}
        void SetWords(UINT8 words) 
            {((UINT8*)buffer)[LENGTH_OFFSET] = words;}
        
        void AttachBuffer(const UINT32* theBuffer) {buffer = (UINT32*)theBuffer;}
        const UINT32* GetBuffer() {return buffer;}
         
        Type GetType() const
        {
            return buffer ? (Type)(((UINT8*)buffer)[TYPE_OFFSET]) : INVALID;  
        }
        UINT16 GetLength() const
        {
            return (buffer ? 
                        ((GetType() < 128) ? 
                            ((((UINT8*)buffer)[LENGTH_OFFSET]) << 2) : 4) : 
                        0);  
        }
                  
    protected:   
        enum
        {
            TYPE_OFFSET     = 0,
            LENGTH_OFFSET   = TYPE_OFFSET + 1
        };
        UINT32*   buffer;
};  // end class NormHeaderExtension

    
class NormMsg
{
    friend class NormMessageQueue;
    
    public:
        // (TBD) Use this "Type" enumeration instead of NormMsgType
        enum Type
        {
            INVALID  = 0,
            INFO     = 1, 
            DATA     = 2, 
            CMD      = 3,
            NACK     = 4,
            ACK      = 5,
            REPORT   = 6
        };    
        enum {MAX_SIZE = 8192};
               
        NormMsg();
        
        // Message building routines
        void SetVersion(UINT8 version) 
        {
            ((UINT8*)buffer)[VERSION_OFFSET] = 
                (((UINT8*)buffer)[VERSION_OFFSET] & 0x0f) | (version << 4);
        }
        void SetType(NormMsg::Type type) 
        {
            ((UINT8*)buffer)[TYPE_OFFSET] = 
                (((UINT8*)buffer)[VERSION_OFFSET] & 0xf0) | (type & 0x0f);
        }
        void SetSequence(UINT16 sequence) 
        {
            ((UINT16*)buffer)[SEQUENCE_OFFSET] = htons(sequence);
        }   
        void SetSourceId(NormNodeId sourceId)
        {
            buffer[SOURCE_ID_OFFSET] = htonl(sourceId); 
        } 
        void SetDestination(const ProtoAddress& dst) {addr = dst;}
        
        void AttachExtension(NormHeaderExtension& extension)
        {
            extension.Init(buffer+(header_length/4));
            ExtendHeaderLength(extension.GetLength());
        }
        
        // Message processing routines
        bool InitFromBuffer(UINT16 msgLength);
        UINT8 GetVersion() const {return ((UINT8*)buffer)[VERSION_OFFSET];}
        NormMsg::Type GetType() const {return (Type)(((UINT8*)buffer)[TYPE_OFFSET] & 0x0f);}
        UINT16 GetHeaderLength() {return ((UINT8*)buffer)[HDR_LEN_OFFSET] << 2;}
        UINT16 GetSequence() const
        {
            return (ntohs((((UINT16*)buffer)[SEQUENCE_OFFSET])));   
        }
        NormNodeId GetSourceId() const
        {
            return (ntohl(buffer[SOURCE_ID_OFFSET]));    
        }
        const ProtoAddress& GetDestination() const {return addr;}
        const ProtoAddress& GetSource() const {return addr;}
        const char* GetBuffer() {return ((char*)buffer);}
        UINT16 GetLength() const {return length;}     
        
        // To retrieve any attached header extensions
        bool HasExtensions() const {return (header_length > header_length_base);}
        bool GetNextExtension(NormHeaderExtension& ext) const
        {
            const UINT32* currentBuffer =  ext.GetBuffer();
            UINT16 nextOffset = 
                currentBuffer ? (currentBuffer - buffer + (ext.GetLength()/4)) : (header_length_base/4);
            bool result = (nextOffset < (header_length/4));
            ext.AttachBuffer(result ? (buffer+nextOffset) : (UINT32*)NULL);
            return result;
        }
        
        // For message reception and misc.
        char* AccessBuffer() {return ((char*)buffer);} 
        ProtoAddress& AccessAddress() {return addr;} 
        
    protected:
        // Common message header offsets
        // All of our offsets reflect their offset based on the field size!
        // (So we can efficiently dereference msg fields with proper alignment)
        enum
        {
            VERSION_OFFSET      = 0,
            TYPE_OFFSET         = VERSION_OFFSET,
            HDR_LEN_OFFSET      = VERSION_OFFSET+1,
            SEQUENCE_OFFSET     = (HDR_LEN_OFFSET+1)/2,
            SOURCE_ID_OFFSET    = ((SEQUENCE_OFFSET*2)+2)/4,
            MSG_OFFSET          = (SOURCE_ID_OFFSET*4)+4 
        }; 
            
        void SetBaseHeaderLength(UINT16 len) 
        {
            ((UINT8*)buffer)[HDR_LEN_OFFSET] = len >> 2;
            length = header_length_base = header_length = len;
        }
        void ExtendHeaderLength(UINT16 len) 
        {
            header_length += len;
            length += len;
            ((UINT8*)buffer)[HDR_LEN_OFFSET] = header_length >> 2;
        }
           
        UINT32          buffer[MAX_SIZE / sizeof(UINT32)]; 
        UINT16          length;         // in bytes
        UINT16          header_length;  
        UINT16          header_length_base;
        ProtoAddress    addr;  // src or dst address
        
        NormMsg*        prev;
        NormMsg*        next;
};  // end class NormMsg

// "NormObjectMsg" is a base class for the similar "NormInfoMsg"
// and "NormDataMsg" types

class NormObjectMsg : public NormMsg
{
    friend class NormMsg;
    public:
        enum Flag
        { 
            FLAG_REPAIR     = 0x01,
            FLAG_EXPLICIT   = 0x02,
            FLAG_INFO       = 0x04,
            FLAG_UNRELIABLE = 0x08,
            FLAG_FILE       = 0x10,
            FLAG_STREAM     = 0x20,
            FLAG_MSG_START  = 0x40
        }; 
        UINT16 GetSessionId() const
        {
            return (ntohs(((UINT16*)buffer)[SESSION_ID_OFFSET]));
        }
        UINT8 GetGrtt() const {return ((UINT8*)buffer)[GRTT_OFFSET];} 
        UINT8 GetBackoffFactor() const {return ((((UINT8*)buffer)[GSIZE_OFFSET] >> 4) & 0x0f);}
        UINT8 GetGroupSize() const {return (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f);} 
        bool FlagIsSet(NormObjectMsg::Flag flag) const
            {return (0 != (flag & ((UINT8*)buffer)[FLAGS_OFFSET]));}
        bool IsStream() const {return FlagIsSet(FLAG_STREAM);}
        UINT8 GetFecId() const {return ((UINT8*)buffer)[FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
        {
            return (ntohs(((UINT16*)buffer)[OBJ_ID_OFFSET]));   
        }
        
        // Message building routines
        void SetSessionId(UINT16 sessionId)
        {
            ((UINT16*)buffer)[SESSION_ID_OFFSET] = htons(sessionId);
        }
        void SetGrtt(UINT8 grtt) {((UINT8*)buffer)[GRTT_OFFSET] = grtt;}
        void SetBackoffFactor(UINT8 backoff)
        {
            ((UINT8*)buffer)[BACKOFF_OFFSET] =  (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f) | (backoff << 4);
        }
        void SetGroupSize(UINT8 gsize) 
        {
            ((UINT8*)buffer)[GSIZE_OFFSET] =  (((UINT8*)buffer)[GSIZE_OFFSET] & 0xf0) | gsize;
        }
        void ResetFlags() {((UINT8*)buffer)[FLAGS_OFFSET] = 0;}
        void SetFlag(NormObjectMsg::Flag flag) {((UINT8*)buffer)[FLAGS_OFFSET] |= flag;}
        void SetFecId(UINT8 fecId) {((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;}        
        void SetObjectId(const NormObjectId& objectId)
        {
            ((UINT16*)buffer)[OBJ_ID_OFFSET] = htons((UINT16)objectId);
        }
        
    protected: 
        enum
        {
            SESSION_ID_OFFSET   = MSG_OFFSET/2,
            GRTT_OFFSET         = (SESSION_ID_OFFSET*2)+2,
            BACKOFF_OFFSET      = GRTT_OFFSET+1,
            GSIZE_OFFSET        = BACKOFF_OFFSET,
            FLAGS_OFFSET        = GSIZE_OFFSET+1,
            FEC_ID_OFFSET       = FLAGS_OFFSET+1,
            OBJ_ID_OFFSET       = (FEC_ID_OFFSET+1)/2,
            OBJ_MSG_OFFSET      = (OBJ_ID_OFFSET*2)+2
        };          
};  // end class NormObjectMsg


// This FEC Object Transmission Information assumes "fec_id" == 129
class NormFtiExtension : public NormHeaderExtension
{
    public:
        // To build the FTI Header Extension
        // (TBD) allow for different "fec_id" types in the future
        virtual void Init(UINT32* theBuffer)
        {
            AttachBuffer(theBuffer);
            SetType(FTI);
            SetWords(4);
        }        
        void SetFecInstanceId(UINT16 instanceId)
        {
            ((UINT16*)buffer)[FEC_INSTANCE_OFFSET] = htons(instanceId);
        }
        void SetFecMaxBlockLen(UINT16 ndata)
        {
            ((UINT16*)buffer)[FEC_NDATA_OFFSET] = htons(ndata);
        }
        void SetFecNumParity(UINT16 nparity)
        {
            ((UINT16*)buffer)[FEC_NPARITY_OFFSET] = htons(nparity);
        }
        void SetSegmentSize(UINT16 segmentSize)
        {
            ((UINT16*)buffer)[SEG_SIZE_OFFSET] = htons(segmentSize);
        }
        void SetObjectSize(const NormObjectSize& objectSize)
        {
            ((UINT16*)buffer)[OBJ_SIZE_MSB_OFFSET] = htons(objectSize.MSB());
            buffer[OBJ_SIZE_LSB_OFFSET] = htonl(objectSize.LSB());
        }
        
        // FTI Extension parsing methods
        UINT16 GetFecInstanceId() const
        {
            return (ntohs(((UINT16*)buffer)[FEC_INSTANCE_OFFSET]));
        }
        UINT16 GetFecMaxBlockLen() const
        {
            return (ntohs(((UINT16*)buffer)[FEC_NDATA_OFFSET])); 
        }
        UINT16 GetFecNumParity() const
        {
            return (ntohs(((UINT16*)buffer)[FEC_NPARITY_OFFSET]));
        }
        UINT16 GetSegmentSize() const
        {
            return (ntohs(((UINT16*)buffer)[SEG_SIZE_OFFSET]));
        }
        NormObjectSize GetObjectSize() const
        {
            return NormObjectSize(ntohs(((UINT16*)buffer)[OBJ_SIZE_MSB_OFFSET]), 
                                  ntohl(buffer[OBJ_SIZE_LSB_OFFSET]));   
        }
    
    private:
        enum
        {
            OBJ_SIZE_MSB_OFFSET = (LENGTH_OFFSET + 1)/2,
            OBJ_SIZE_LSB_OFFSET = ((OBJ_SIZE_MSB_OFFSET*2)+2)/4,
            FEC_INSTANCE_OFFSET = ((OBJ_SIZE_LSB_OFFSET*4)+4)/2,
            SEG_SIZE_OFFSET     = ((FEC_INSTANCE_OFFSET*2)+2)/2,
            FEC_NDATA_OFFSET    = ((SEG_SIZE_OFFSET*2)+2)/2,
            FEC_NPARITY_OFFSET  = ((FEC_NDATA_OFFSET*2)+2)/2
        };
};  // end class NormFtiExtension


class NormInfoMsg : public NormObjectMsg
{
    public:
        void Init()
        {
            SetType(INFO);
            SetBaseHeaderLength(INFO_HEADER_LEN);
            // Default "fec_id" = 129
            SetFecId(129);
            ResetFlags();
        }
                
        UINT16 GetInfoLen() const {return (length - header_length);}
        const char* GetInfo() const {return (((char*)buffer) + header_length);}
        
        // Note: apply any header extensions first
        void SetInfo(const char* data, UINT16 size)
        {
            memcpy(((char*)buffer)+header_length, data, size);
            length = size + header_length;
        }
    private:
        enum {INFO_HEADER_LEN = OBJ_MSG_OFFSET};
};  // end class NormInfoMsg

class NormDataMsg : public NormObjectMsg
{
    public:
        void Init()
        {
            SetType(DATA);
            // Default "fec_id" = 129 w/ "fec_payload_id" length = 8 bytes
            SetBaseHeaderLength(DATA_HEADER_LEN);
            SetFecId(129);
            ResetFlags();
        }
        // Message building methods (in addition to NormObjectMsg fields)
        void SetFecBlockId(const NormBlockId& blockId)
        {
            buffer[BLOCK_ID_OFFSET] = htonl((UINT32)blockId);
        }
        void SetFecBlockLen(UINT16 blockLen)
        {
            ((UINT16*)buffer)[BLOCK_LEN_OFFSET] = htons(blockLen);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            ((UINT16*)buffer)[SYMBOL_ID_OFFSET] = htons(symbolId);
        }
        
        // Two ways to set payload content:
        // 1) Directly access payload to copy segment, then set data message length
        //    (Note NORM_STREAM_OBJECT segments must already include "payload_len"
        //    and "payload_offset" with the "payload_data"
        char* AccessPayload() {return (((char*)buffer)+header_length);}
        // For NORM_STREAM_OBJECT segments, "dataLength" must include the PAYLOAD_HEADER_LENGTH
        void SetPayloadLength(UINT16 payloadLength)
        {
            length = header_length + payloadLength;
        }
        // Set "payload" directly (useful for FEC parity segments)
        void SetPayload(char* payload, UINT16 payloadLength)
        {
            memcpy(((char*)buffer)+header_length, payload, payloadLength);
            length = header_length + payloadLength; 
        }
        // AccessPayloadData() (useful for setting ZERO padding)
        char* AccessPayloadData() 
        {
            UINT16 payloadIndex = IsStream() ? header_length+PAYLOAD_DATA_OFFSET : header_length;
            return (((char*)buffer)+payloadIndex);
        }
               
        // Message processing methods
        NormBlockId GetFecBlockId() const
            {return (ntohl(buffer[BLOCK_ID_OFFSET]));}
        UINT16 GetFecBlockLen() const
            {return (ntohs(((UINT16*)buffer)[BLOCK_LEN_OFFSET]));}
        UINT16 GetFecSymbolId()  const
            {return (ntohs(((UINT16*)buffer)[SYMBOL_ID_OFFSET]));}
        bool IsData() const 
            {return (GetFecSymbolId() < GetFecBlockLen());}
       
        // Note: For NORM_OBJECT_STREAM, "payload" includes "payload_reserved",  
        //       "payload_len", "payload_offset", and "payload_data" fields
        //       For NORM_OBJECT_FILE and NORM_OBJECT_DATA, "payload" includes
        //       "payload_data" only
        const char* GetPayload() const {return (((char*)buffer)+header_length);}
        UINT16 GetPayloadLength() const {return (length - header_length);}
        
        const char* GetPayloadData() const 
        {
            UINT16 dataIndex = IsStream() ? header_length+PAYLOAD_DATA_OFFSET : header_length;
            return (((char*)buffer)+dataIndex);
        }
        UINT16 GetPayloadDataLength() const 
        {
            UINT16 dataIndex = IsStream() ? header_length+PAYLOAD_DATA_OFFSET : header_length;
            return (length - dataIndex);
        }
        
        
        // These routines are only applicable to messages containing NORM_OBJECT_STREAM content
        // Some static helper routines for reading/writing embedded payload length/offsets
        static UINT16 GetStreamPayloadHeaderLength() {return (PAYLOAD_DATA_OFFSET);}
        static void WriteStreamPayloadLength(char* payload, UINT16 len)
        {
            UINT16 temp16 = htons(len);
            memcpy(payload+PAYLOAD_LENGTH_OFFSET, &temp16, 2);
        }
        static void WriteStreamPayloadOffset(char* payload, UINT32 offset)
        {
            UINT32 temp32 = htonl(offset);
            memcpy(payload+PAYLOAD_OFFSET_OFFSET, &temp32, 4);
        }
        static UINT16 ReadStreamPayloadLength(const char* payload)
        {
            UINT16 temp16;
            memcpy(&temp16, payload+PAYLOAD_LENGTH_OFFSET, 2);
            return (ntohs(temp16));
        }
        static UINT32 ReadStreamPayloadOffset(const char* payload)
        {
            UINT32 temp32;
            memcpy(&temp32, payload+PAYLOAD_OFFSET_OFFSET, 4);
            return (ntohl(temp32));
        }
          
    private:    
        enum
        {
            BLOCK_ID_OFFSET     = OBJ_MSG_OFFSET/4,
            BLOCK_LEN_OFFSET    = ((BLOCK_ID_OFFSET*4)+4)/2,
            SYMBOL_ID_OFFSET    = ((BLOCK_LEN_OFFSET*2)+2)/2,
            DATA_HEADER_LEN     = (SYMBOL_ID_OFFSET*2)+2
        };
        enum
        {
            PAYLOAD_RESERVED_OFFSET = 0,
            PAYLOAD_LENGTH_OFFSET   = PAYLOAD_RESERVED_OFFSET+2,
            PAYLOAD_OFFSET_OFFSET   = PAYLOAD_LENGTH_OFFSET+2,
            PAYLOAD_DATA_OFFSET     = PAYLOAD_OFFSET_OFFSET+4   
        };
};  // end class NormDataMsg


class NormCmdMsg : public NormMsg
{
    public:
        enum Flavor
        {
            INVALID     = 0,
            FLUSH       = 1,
            EOT         = 2,
            SQUELCH     = 3,
            CC          = 4,
            REPAIR_ADV  = 5,
            ACK_REQ     = 6,
            APPLICATION = 7
        };
        
        // Message building
        void SetSessionId(UINT16 sessionId)
        {
            ((UINT16*)buffer)[SESSION_ID_OFFSET] = htons(sessionId);   
        }
        void SetGrtt(UINT8 quantizedGrtt) 
            {((UINT8*)buffer)[GRTT_OFFSET] = quantizedGrtt;}
        void SetBackoffFactor(UINT8 backoff)
        {
            ((UINT8*)buffer)[BACKOFF_OFFSET] = (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f) | (backoff << 4);
        }
        void SetGroupSize(UINT8 gsize) 
        {
            ((UINT8*)buffer)[GSIZE_OFFSET] = (((UINT8*)buffer)[GSIZE_OFFSET] & 0xf0) | gsize;
        }
        void SetFlavor(NormCmdMsg::Flavor flavor)
            {((UINT8*)buffer)[FLAVOR_OFFSET] = (UINT8)flavor;}
        
        // Message processing
        UINT16 GetSessionId() const {return (ntohs(((UINT16*)buffer)[SESSION_ID_OFFSET]));}
        UINT8 GetGrtt() const {return ((UINT8*)buffer)[GRTT_OFFSET];}
        UINT8 GetBackoffFactor() const {return ((((UINT8*)buffer)[GSIZE_OFFSET] >> 4) & 0x0f);}
        UINT8 GetGroupSize() const {return (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f);} 
        NormCmdMsg::Flavor GetFlavor() const {return (Flavor)((UINT8*)buffer)[FLAVOR_OFFSET];} 
            
    protected:
        friend class NormMsg;
        enum
        {
            SESSION_ID_OFFSET    = MSG_OFFSET/2,
            GRTT_OFFSET          = (SESSION_ID_OFFSET*2)+2,
            BACKOFF_OFFSET       = GRTT_OFFSET + 1,
            GSIZE_OFFSET         = BACKOFF_OFFSET,
            FLAVOR_OFFSET        = GSIZE_OFFSET + 1
        }; 
};  // end class NormCmdMsg

class NormCmdFlushMsg : public NormCmdMsg
{
    friend class NormMsg;
    
    public:
        void Init()
        {
            SetType(CMD);
            SetFlavor(FLUSH);
            SetBaseHeaderLength(FLUSH_HEADER_LEN);
            SetFecId(129);  // default "fec_id"
        }    
            
        void SetFecId(UINT8 fecId) {((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;}
        void SetObjectId(const NormObjectId& objectId)
        {
            ((UINT16*)buffer)[OBJ_ID_OFFSET] = htons((UINT16)objectId);
        }
        // "fec_payload_id" fields, assuming "fec_id" = 129
        void SetFecBlockId(const NormBlockId& blockId)
        {
            buffer[BLOCK_ID_OFFSET] = htonl((UINT32)blockId);
        }
        void SetFecBlockLen(UINT16 blockLen)
        {
            ((UINT16*)buffer)[BLOCK_LEN_OFFSET] = htons((UINT16)blockLen);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            ((UINT16*)buffer)[SYMBOL_ID_OFFSET] = htons((UINT16)symbolId);
        }
        void ResetAckingNodeList() {length = header_length;}
        bool AppendAckingNode(NormNodeId nodeId, UINT16 segmentSize)
        {
            if ((length-header_length+ 4) > segmentSize) return false;
            buffer[length/4] = htonl((UINT32)nodeId);
            length += 4;
            return true;
        }
        
        // Message processing
        UINT8 GetFecId() {return ((UINT8*)buffer)[FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
        {
            return (ntohs(((UINT16*)buffer)[OBJ_ID_OFFSET]));
        }        
        NormBlockId GetFecBlockId() const
        {
            return (ntohl(buffer[BLOCK_ID_OFFSET]));
        }
        UINT16 GetFecBlockLen() const
        {
            return (ntohs(((UINT16*)buffer)[BLOCK_LEN_OFFSET]));
        }
        UINT16 GetFecSymbolId() const
        {
            return (ntohs(((UINT16*)buffer)[SYMBOL_ID_OFFSET]));  
        }
        UINT16 GetAckingNodeCount() const {return ((length - header_length) >> 2);}
        const UINT32* GetAckingNodeList() const {return (buffer+(header_length/4));}
        NormNodeId GetAckingNodeId(UINT16 index) const 
        {
            return (ntohl(buffer[(header_length/4)+index]));   
        }
            
    private:
        enum
        {
            FEC_ID_OFFSET     = FLAVOR_OFFSET + 1,     
            OBJ_ID_OFFSET     = (FEC_ID_OFFSET + 1)/2,     
            BLOCK_ID_OFFSET   = ((OBJ_ID_OFFSET*2)+2)/4,  
            BLOCK_LEN_OFFSET  = ((BLOCK_ID_OFFSET*4)+4)/2,   
            SYMBOL_ID_OFFSET  = ((BLOCK_LEN_OFFSET*2)+2)/2,  
            FLUSH_HEADER_LEN  = (SYMBOL_ID_OFFSET*2)+2 
        };
};  // end class NormCmdFlushMsg

class NormCmdEotMsg : public NormCmdMsg
{
    public:
        void Init()
        {
            SetType(CMD);
            SetFlavor(EOT);
            SetBaseHeaderLength(EOT_HEADER_LEN);
            memset(((char*)buffer)+RESERVED_OFFSET, 0, 3);
        }    
    private:
        enum 
        {
            RESERVED_OFFSET = FLAVOR_OFFSET + 1,
            EOT_HEADER_LEN  = RESERVED_OFFSET + 3
        };
};  // end class NormCmdEotMsg


class NormCmdSquelchMsg : public NormCmdMsg
{
    public:
        // Message building
        void Init()
        {
            SetType(CMD);
            SetFlavor(SQUELCH);
            SetBaseHeaderLength(SQUELCH_HEADER_LEN);
            SetFecId(129);  // default "fec_id"
        }    
        void SetFecId(UINT8 fecId) 
            {((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;}
        void SetObjectId(const NormObjectId& objectId)
        {
            ((UINT16*)buffer)[OBJ_ID_OFFSET] = htons((UINT16)objectId);
        }
        // "fec_payload_id" fields, assuming "fec_id" = 129
        void SetFecBlockId(const NormBlockId& blockId)
        {
            buffer[BLOCK_ID_OFFSET] = htonl((UINT32)blockId);
        }
        void SetFecBlockLen(UINT16 blockLen)
        {
            ((UINT16*)buffer)[BLOCK_LEN_OFFSET] = htons((UINT16)blockLen);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            ((UINT16*)buffer)[SYMBOL_ID_OFFSET] = htons((UINT16)symbolId);
        }
        
        void ResetInvalidObjectList() {length = header_length;}
        bool AppendInvalidObject(NormObjectId objectId, UINT16 segmentSize)
        {
            if ((length-header_length+2) > segmentSize) return false;
            ((UINT16*)buffer)[length/2] = htons((UINT16)objectId);
            length += 2;
            return true;   
        }
        
        // Message processing
        UINT8 GetFecId() {return ((UINT8*)buffer)[FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
        {
            return (ntohs(((UINT16*)buffer)[OBJ_ID_OFFSET]));
        }        
        NormBlockId GetFecBlockId() const
        {
            return (ntohl(buffer[BLOCK_ID_OFFSET]));
        }
        UINT16 GetFecBlockLen() const
        {
            return (ntohs(((UINT16*)buffer)[BLOCK_LEN_OFFSET]));
        }
        UINT16 GetFecSymbolId() const
        {
            return (ntohs(((UINT16*)buffer)[SYMBOL_ID_OFFSET]));  
        }
        
        // Use these to parse invalid object list
        UINT16 GetInvalidObjectCount() const {return ((length - header_length) >> 1);} 
        UINT16* GetInvalidObjectList() const {return (UINT16*)(buffer+header_length);}
        NormObjectId GetInvalidObjectId(UINT16 index) const
        {
            return (ntohs(((UINT16*)buffer)[(header_length/2)+index]));
        }
        
    private:
        enum
        {
            FEC_ID_OFFSET      = FLAVOR_OFFSET + 1,      
            OBJ_ID_OFFSET      = (FEC_ID_OFFSET + 1)/2,      
            BLOCK_ID_OFFSET    = ((OBJ_ID_OFFSET*2)+2)/4,   
            BLOCK_LEN_OFFSET   = ((BLOCK_ID_OFFSET*4)+4)/2,    
            SYMBOL_ID_OFFSET   = ((BLOCK_LEN_OFFSET*2)+2)/2,    
            SQUELCH_HEADER_LEN = (SYMBOL_ID_OFFSET*2)+2
        };           
};  // end class NormCmdSquelchMsg


// These flag values are used  "cc_flags" field of NORM_CMD(CC) CC_NODE_LIST 
// items and NORM_NACK and NORM_ACK messages
class NormCC
{
    public:
        enum Flag
        {
            CLR   = 0x01,
            PLR   = 0x02,
            RTT   = 0x04,
            START = 0x08,
            LEAVE = 0x10
        };
};  // end class NormCC
    
class NormCmdCCMsg : public NormCmdMsg
{
    public:
        void Init()
        {
            SetType(CMD);
            SetFlavor(CC);
            SetBaseHeaderLength(CC_HEADER_LEN);
            ((UINT8*)buffer)[RESERVED_OFFSET] = 0;
        }
        
        void SetCCSequence(UINT16 ccSequence) 
        {
            ((UINT16*)buffer)[CC_SEQUENCE_OFFSET] = htons(ccSequence);
        } 
        void SetSendTime(const struct timeval& sendTime)
        {
            buffer[SEND_TIME_SEC_OFFSET] = htonl(sendTime.tv_sec);
            buffer[SEND_TIME_USEC_OFFSET] = htonl(sendTime.tv_usec);
        }
         
        UINT16 GetCCSequence() const 
        {
            return (ntohs(((UINT16*)buffer)[CC_SEQUENCE_OFFSET]));
        }
        void GetSendTime(struct timeval& sendTime) const
        {
            sendTime.tv_sec = ntohl(buffer[SEND_TIME_SEC_OFFSET]); 
            sendTime.tv_usec = ntohl(buffer[SEND_TIME_USEC_OFFSET]);
        }
        
        bool AppendCCNode(UINT16 segMax, NormNodeId nodeId, UINT8 flags, 
                          UINT8 rtt, UINT16 rate)
        {
            if ((length-header_length+CC_ITEM_SIZE)> segMax) return false;
            UINT32* ptr = buffer + length/4;
            ptr[CC_NODE_ID_OFFSET] = htonl(nodeId);
            ((UINT8*)ptr)[CC_FLAGS_OFFSET] = flags;
            ((UINT8*)ptr)[CC_RTT_OFFSET] = rtt;
            ((UINT16*)ptr)[CC_RATE_OFFSET] = htons(rate);
            length += CC_ITEM_SIZE;
            return true;
        } 
        bool GetCCNode(NormNodeId nodeId, UINT8& flags, UINT8& rtt, UINT16& rate) const;
        
        
        class Iterator;
        friend class Iterator;
        
        class Iterator
        {
            public:
                Iterator(const NormCmdCCMsg& msg); 
                void Reset() {offset = 0;}
                bool GetNextNode(NormNodeId& nodeId, UINT8& flags, UINT8& rtt, UINT16& rate);
                
            private:
                const NormCmdCCMsg& cc_cmd;
                UINT16              offset;  
        };
                        
            
    private:
        enum
        {
            RESERVED_OFFSET       = FLAVOR_OFFSET + 1,
            CC_SEQUENCE_OFFSET    = (RESERVED_OFFSET + 1)/2,
            SEND_TIME_SEC_OFFSET  = ((CC_SEQUENCE_OFFSET*2)+2)/4,
            SEND_TIME_USEC_OFFSET = ((SEND_TIME_SEC_OFFSET*4)+4)/4,
            CC_HEADER_LEN         = (SEND_TIME_USEC_OFFSET*4)+4
        };  
            
        enum
        {
            CC_NODE_ID_OFFSET   = 0,
            CC_FLAGS_OFFSET     = CC_NODE_ID_OFFSET + 4,
            CC_RTT_OFFSET       = CC_FLAGS_OFFSET + 1,
            CC_RATE_OFFSET      = (CC_RTT_OFFSET + 1)/2,
            CC_ITEM_SIZE        = (CC_RATE_OFFSET*2)+2
        };
                        
};  // end NormCmdCCMsg

class NormCCRateExtension : public NormHeaderExtension
{
    public:
            
        virtual void Init(UINT32* theBuffer)
        {
            AttachBuffer(theBuffer);
            SetType(CC_RATE);
            ((UINT8*)buffer)[RESERVED_OFFSET] = 0;
        }
        void SetSendRate(UINT16 sendRate)
            {((UINT16*)buffer)[SEND_RATE_OFFSET] = htons(sendRate);}
        UINT16 GetSendRate() 
            {return (ntohs(((UINT16*)buffer)[SEND_RATE_OFFSET]));}
            
    private:
        enum 
        {
            RESERVED_OFFSET  = TYPE_OFFSET + 1,
            SEND_RATE_OFFSET = (RESERVED_OFFSET + 1)/2
        };
};  // end class NormCCRateExtension

// This implementation currently assumes "fec_id"= 129
class NormRepairRequest
{
    public:
        class Iterator;
        friend class NormRepairRequest::Iterator;
        enum Form 
        {
            INVALID  = 0,
            ITEMS    = 1,
            RANGES   = 2,
            ERASURES = 3
        };
            
        enum Flag 
        {
            SEGMENT  = 0x01,
            BLOCK    = 0x02,
            INFO     = 0x04,
            OBJECT   = 0x08
        };
            
        // Construction
        NormRepairRequest();
        void Init(UINT32* bufferPtr, UINT16 bufferLen)
        {
            buffer =  bufferPtr;
            buffer_len = bufferLen;
            length = 0;
        }
        // (TBD) these could be an enumeration for optimization
        static UINT16 RepairItemLength()  {return 12;}
        static UINT16 RepairRangeLength() {return 24;}
        static UINT16 ErasureItemLength() {return 12;}
        
        // Repair request building
        void SetForm(NormRepairRequest::Form theForm) {form = theForm;}
        void ResetFlags() {flags = 0;}
        void SetFlag(NormRepairRequest::Flag theFlag) {flags |= theFlag;} 
        void ClearFlag(NormRepairRequest::Flag theFlag) {flags &= ~theFlag;}       
        
        // Returns length (each repair item requires 8 bytes of space)
        bool AppendRepairItem(const NormObjectId& objectId, 
                              const NormBlockId&  blockId,
                              UINT16              blockLen,
                              UINT16              symbolId);
        
        bool AppendRepairRange(const NormObjectId& startObjectId, 
                               const NormBlockId&  startBlockId,
                               UINT16              startBlockLen,
                               UINT16              startSymbolId,
                               const NormObjectId& endObjectId, 
                               const NormBlockId&  endBlockId,
                               UINT16              endBlockLen,
                               UINT16              endSymbolId);  
        
        bool AppendErasureCount(const NormObjectId& objectId, 
                                const NormBlockId&  blockId,
                                UINT16              blockLen,
                                UINT16              erasureCount);     
        
        UINT16 Pack();       
        
        // Repair request processing
        UINT16 Unpack(const UINT32* bufferPtr, UINT16 bufferLen);
        NormRepairRequest::Form GetForm() const {return form;}
        bool FlagIsSet(NormRepairRequest::Flag theFlag) const
            {return (0 != (theFlag & flags));}
        UINT16 GetLength() const {return length;}
        
        class Iterator
        {
            public:
                Iterator(NormRepairRequest& theRequest);
                void Reset() {offset = 0;}
                bool NextRepairItem(NormObjectId* objectId,
                                    NormBlockId*  blockId,
                                    UINT16*       blockLen,
                                    UINT16*       symbolId);
            private:
                NormRepairRequest& request;
                UINT16             offset;    
        };  // end class NormRepairRequest::Iterator
          
    private:
        bool RetrieveRepairItem(UINT16        offset,
                                NormObjectId* objectId, 
                                NormBlockId*  blockId,
                                UINT16*       blockLen,
                                UINT16*       symbolId) const;
        enum
        {    
            FORM_OFFSET      = 0,
            FLAGS_OFFSET     = FORM_OFFSET + 1,   
            LENGTH_OFFSET    = (FLAGS_OFFSET + 1)/2,
            ITEM_LIST_OFFSET = (LENGTH_OFFSET*2)+2
        }; 
            
        // These are the offsets for "fec_id" = 129 NormRepairRequest items
        enum
        {
            FEC_ID_OFFSET       = 0,
            RESERVED_OFFSET     = FEC_ID_OFFSET + 1,
            OBJ_ID_OFFSET       = (RESERVED_OFFSET + 1)/2,
            BLOCK_ID_OFFSET     = ((OBJ_ID_OFFSET*2)+2)/4,
            BLOCK_LEN_OFFSET    = ((BLOCK_ID_OFFSET*4)+4)/2,
            SYMBOL_ID_OFFSET    = ((BLOCK_LEN_OFFSET*2)+2)/2
        };
            
        Form        form;
        int         flags;
        UINT16      length;      // length of repair items
        UINT32*     buffer;
        UINT16      buffer_len;  // in bytes
        
};  // end class NormRepairRequest

class NormCmdRepairAdvMsg : public NormCmdMsg
{
    public:
        enum Flag {NORM_REPAIR_ADV_FLAG_LIMIT = 0x01};
    
        void Init()
        {
            SetType(CMD);
            SetFlavor(REPAIR_ADV);
            SetBaseHeaderLength(REPAIR_ADV_HEADER_LEN);
            ResetFlags();
            ((UINT16*)buffer)[RESERVED_OFFSET] =  0;
        }
        
        // Message building
        void ResetFlags() {((UINT8*)buffer)[FLAGS_OFFSET] = 0;}
        void SetFlag(NormCmdRepairAdvMsg::Flag flag) 
            {((UINT8*)buffer)[FLAGS_OFFSET] |= (UINT8)flag;}        
        void AttachRepairRequest(NormRepairRequest& request,
                                 UINT16             segmentMax)
        {
            int buflen = segmentMax - (length - header_length);
            buflen = (buflen>0) ? buflen : 0;
            request.Init(buffer+length/4, buflen);
        }
        UINT16 PackRepairRequest(NormRepairRequest& request)
        {
            UINT16 requestLength = request.Pack();
            length += requestLength;
            return requestLength;
        }
                        
        // Message processing 
        bool FlagIsSet(NormCmdRepairAdvMsg::Flag flag) const 
        {
            return (0 != ((UINT8)flag | ((UINT8*)buffer)[FLAGS_OFFSET]));
        }       
        //char* AccessRepairContent() {return (buffer + header_length);}
        const UINT32* GetRepairContent() const {return (buffer + header_length/4);}
        UINT16 GetRepairContentLength() const {return (length - header_length);}
                  
    private:
        enum
        {
            FLAGS_OFFSET            = FLAVOR_OFFSET + 1,
            RESERVED_OFFSET         = FLAGS_OFFSET + 1,
            REPAIR_ADV_HEADER_LEN   = RESERVED_OFFSET + 2
        };    
        
};  // end class NormCmdRepairAdvMsg


class NormCCFeedbackExtension : public NormHeaderExtension
{
    public:
        virtual void Init(UINT32* theBuffer)
        {
            AttachBuffer(theBuffer);
            SetType(CC_FEEDBACK);
            SetWords(3);
            ((UINT8*)buffer)[CC_FLAGS_OFFSET] = 0;
            ((UINT16*)buffer)[CC_RESERVED_OFFSET] = 0;
        }
        void SetCCSequence(UINT16 ccSequence)
        {
            ((UINT16*)buffer)[CC_SEQUENCE_OFFSET] = htons(ccSequence);   
        }
        void ResetCCFlags() 
            {((UINT8*)buffer)[CC_FLAGS_OFFSET] = 0;}
        void SetCCFlag(NormCC::Flag flag) 
            {((UINT8*)buffer)[CC_FLAGS_OFFSET] |= (UINT8)flag;}
        void SetCCRtt(UINT8 ccRtt) 
            {((UINT8*)buffer)[CC_RTT_OFFSET] = ccRtt;}
        void SetCCLoss(UINT16 ccLoss)
            {((UINT16*)buffer)[CC_LOSS_OFFSET] = htons(ccLoss);}
        void SetCCRate(UINT16 ccRate) 
            {((UINT16*)buffer)[CC_RATE_OFFSET] = htons(ccRate);}
        
        UINT16 GetCCSequence() const 
            {return (ntohs(((UINT16*)buffer)[CC_SEQUENCE_OFFSET]));}
        UINT8 GetCCFlags() 
            {return ((UINT8*)buffer)[CC_FLAGS_OFFSET];}
        bool CCFlagIsSet(NormCC::Flag flag) const
        {
            return (0 != ((UINT8)flag & ((UINT8*)buffer)[CC_FLAGS_OFFSET]));
        }
        UINT8 GetCCRtt() 
            {return ((UINT8*)buffer)[CC_RTT_OFFSET];}
        UINT16 GetCCLoss() 
            {return (ntohs(((UINT16*)buffer)[CC_LOSS_OFFSET]));} 
        UINT16 GetCCRate()
            {return (ntohs(((UINT16*)buffer)[CC_RATE_OFFSET]));} 
            
    private:
        enum
        {
            CC_SEQUENCE_OFFSET  = (LENGTH_OFFSET+1)/2,
            CC_FLAGS_OFFSET     = (CC_SEQUENCE_OFFSET*2)+2,
            CC_RTT_OFFSET       = CC_FLAGS_OFFSET + 1,
            CC_LOSS_OFFSET      = (CC_RTT_OFFSET + 1)/2,
            CC_RATE_OFFSET      = ((CC_LOSS_OFFSET*2)+2)/2,
            CC_RESERVED_OFFSET  = ((CC_RATE_OFFSET*2)+2)/2
        };
                            
};  // end class NormCCFeedbackExtension

// Note: Support for application-defined AckFlavors (32-255) 
//       may be provided, but 0-31 are reserved values
class NormAck
{
    public:
        enum Type
        {
            INVALID     =  0,
            CC          =  1,
            FLUSH       =  2,
            APP_BASE    = 16
        };
};

class NormCmdAckReqMsg : public NormCmdMsg
{
    public: 
            
        void Init()
        {
            SetType(CMD);
            SetFlavor(ACK_REQ);
            SetBaseHeaderLength(ACK_REQ_HEADER_LEN);
            ((UINT8*)buffer)[RESERVED_OFFSET] =  0;
        }
        
        // Message building
        void SetAckType(NormAck::Type ackType) {((UINT8*)buffer)[ACK_TYPE_OFFSET] = (UINT8)ackType;}
        void SetAckId(UINT8 ackId) {((UINT8*)buffer)[ACK_ID_OFFSET] = ackId;}       
        void ResetAckingNodeList() {length = header_length;}
        bool AppendAckingNode(NormNodeId nodeId, UINT16 segmentSize)
        {
            if ((length - header_length + 4) > segmentSize) return false;
            buffer[length/4] = htonl(nodeId);
            length += 4;
            return true;
        }        
        
        // Message processing
        NormAck::Type GetAckType() const 
            {return (NormAck::Type)(((UINT8*)buffer)[ACK_TYPE_OFFSET]);}
        UINT8 GetAckId() const 
            {return ((UINT8*)buffer)[ACK_ID_OFFSET];}        
        UINT16 GetAckingNodeCount() const 
            {return ((length - header_length) >> 2);}
        NormNodeId GetAckingNodeId(UINT16 index) const 
        {
            return (ntohl(buffer[(header_length/4)+index]));
        }
        
            
    private:
        enum 
        {
            RESERVED_OFFSET    = FLAVOR_OFFSET + 1,
            ACK_TYPE_OFFSET    = RESERVED_OFFSET + 1,
            ACK_ID_OFFSET      = ACK_TYPE_OFFSET + 1,
            ACK_REQ_HEADER_LEN = ACK_ID_OFFSET + 1
        };       
};  // end class NormCmdAckReqMsg


class NormCmdApplicationMsg : public NormCmdMsg
{
    public:
        void Init()
        {
            SetType(CMD);
            SetFlavor(APPLICATION);
            SetBaseHeaderLength(APPLICATION_HEADER_LEN);
            memset(((UINT8*)buffer)+RESERVED_OFFSET, 0, 3);
        }
            
        bool SetContent(const char* content, UINT16 contentLen, UINT16 segmentSize)
        {
            UINT16 len = MIN(contentLen, segmentSize);
            memcpy(((char*)buffer)+header_length, content, len);
            return (contentLen <= segmentSize);
        }
        
        UINT16 GetContentLength() {return (length - header_length);}
        const char* GetContent() {return (((char*)buffer)+header_length);}
            
    private:
        enum 
        {
            RESERVED_OFFSET         = FLAVOR_OFFSET + 1,
            APPLICATION_HEADER_LEN  = RESERVED_OFFSET + 3
        };
};  // end class NormCmdApplicationMsg
 

// Receiver Messages

class NormNackMsg : public NormMsg
{
    public:
        void Init()
        {
            SetType(NACK);
            SetBaseHeaderLength(NACK_HEADER_LEN);
        }
        // Message building
        void SetServerId(NormNodeId serverId)
        {
            buffer[SERVER_ID_OFFSET] = htonl(serverId);
        }
        void SetSessionId(UINT16 sessionId)
        {
            ((UINT16*)buffer)[SESSION_ID_OFFSET] = htons(sessionId);
        }
        void SetGrttResponse(const struct timeval& grttResponse)
        {
            buffer[GRTT_RESPONSE_SEC_OFFSET] = htonl(grttResponse.tv_sec);
            buffer[GRTT_RESPONSE_USEC_OFFSET] = htonl(grttResponse.tv_usec);
        }
        void AttachRepairRequest(NormRepairRequest& request,
                                 UINT16             segmentMax)
        {
            int buflen = segmentMax - (length - header_length);
            buflen = (buflen>0) ? buflen : 0;
            request.Init(buffer+length/4, buflen);
        }
        UINT16 PackRepairRequest(NormRepairRequest& request)
        {
            UINT16 requestLength = request.Pack();
            length += requestLength;
            return requestLength;
        }
                        
        // Message processing 
        NormNodeId GetServerId() const
        {
            return (ntohl(buffer[SERVER_ID_OFFSET]));
        }
        UINT16 GetSessionId() const
        {
            return (ntohs(((UINT16*)buffer)[SESSION_ID_OFFSET]));
        }
        void GetGrttResponse(struct timeval& grttResponse) const
        {
            grttResponse.tv_sec = ntohl(buffer[GRTT_RESPONSE_SEC_OFFSET]);
            grttResponse.tv_usec = ntohl(buffer[GRTT_RESPONSE_USEC_OFFSET]);
        }      
        //char* AccessRepairContent() {return (buffer + header_length);}
        const UINT32* GetRepairContent() const {return (buffer + header_length/4);}
        UINT16 GetRepairContentLength() const
            {return ((length > header_length) ? length - header_length : 0);}        
        UINT16 UnpackRepairRequest(NormRepairRequest& request,
                                   UINT16             requestOffset)
        {
            int buflen = length - header_length - requestOffset;
            buflen = (buflen > 0) ? buflen : 0;
            return request.Unpack(buffer+(header_length+requestOffset)/4, buflen);
        }
           
    private:
        enum
        {
            SERVER_ID_OFFSET          = MSG_OFFSET/4,
            SESSION_ID_OFFSET         = ((SERVER_ID_OFFSET*4)+4)/2,
            RESERVED_OFFSET           = ((SESSION_ID_OFFSET*2)+2)/2,
            GRTT_RESPONSE_SEC_OFFSET  = ((RESERVED_OFFSET*2)+2)/4,
            GRTT_RESPONSE_USEC_OFFSET = ((GRTT_RESPONSE_SEC_OFFSET*4)+4)/4,
            NACK_HEADER_LEN           = (GRTT_RESPONSE_USEC_OFFSET*4)+4
        };    
};  // end class NormNackMsg

class NormAckMsg : public NormMsg
{
    public:
        
        // Message building
        void Init()
        {
            SetType(ACK);
            SetBaseHeaderLength(ACK_HEADER_LEN);
            SetAckType(NormAck::INVALID);
        }
        void SetServerId(NormNodeId serverId)
        {
            buffer[SERVER_ID_OFFSET] = htonl(serverId);
        }
        void SetSessionId(UINT16 sessionId)
        {
            ((UINT16*)buffer)[SESSION_ID_OFFSET] = htons(sessionId);
        }
        void SetAckType(NormAck::Type ackType) {((UINT8*)buffer)[ACK_TYPE_OFFSET] = (UINT8)ackType;}
        void SetAckId(UINT8 ackId) {((UINT8*)buffer)[ACK_ID_OFFSET] = ackId;}
        void SetGrttResponse(const struct timeval& grttResponse)
        {
            buffer[GRTT_RESPONSE_SEC_OFFSET] = htonl(grttResponse.tv_sec);
            buffer[GRTT_RESPONSE_USEC_OFFSET] = htonl(grttResponse.tv_usec);
        }
        bool SetAckPayload(const char* payload, UINT16 payloadLen, UINT16 segmentSize)
        {
            UINT16 len = MIN(payloadLen, segmentSize);
            memcpy(((char*)buffer)+header_length, payload, len);
            length += len;
            return (payloadLen <= segmentSize);   
        }
        
        // Message processing 
        NormNodeId GetServerId() const
        {
            return (ntohl(buffer[SERVER_ID_OFFSET]));
        }
        UINT16 GetSessionId() const
        {
            return (ntohs(((UINT16*)buffer)[SESSION_ID_OFFSET]));
        }
        void GetGrttResponse(struct timeval& grttResponse) const
        {
            grttResponse.tv_sec = ntohl(buffer[GRTT_RESPONSE_SEC_OFFSET]);
            grttResponse.tv_usec = ntohl(buffer[GRTT_RESPONSE_USEC_OFFSET]);
        }             
        NormAck::Type GetAckType() const {return (NormAck::Type)((UINT8*)buffer)[ACK_TYPE_OFFSET];}
        UINT8 GetAckId() const {return ((UINT8*)buffer)[ACK_ID_OFFSET];}
        UINT16 GetPayloadLength() const {return (length - header_length);}
        const char* GetPayload() const {return (((char*)buffer) + header_length);}
        
    protected:
        enum
        {
            SERVER_ID_OFFSET          = MSG_OFFSET/4,
            SESSION_ID_OFFSET         = ((SERVER_ID_OFFSET*4)+4)/2,
            ACK_TYPE_OFFSET           = (SESSION_ID_OFFSET*2)+2,
            ACK_ID_OFFSET             = ACK_TYPE_OFFSET + 1,
            GRTT_RESPONSE_SEC_OFFSET  = (ACK_ID_OFFSET + 1)/4,
            GRTT_RESPONSE_USEC_OFFSET = ((GRTT_RESPONSE_SEC_OFFSET*4)+4)/4,
            ACK_HEADER_LEN            = (GRTT_RESPONSE_USEC_OFFSET*4)+4
        };
};  // end class NormAckMsg

class NormAckFlushMsg : public NormAckMsg
{
    public:
        void Init()
        {
            SetType(ACK);
            SetBaseHeaderLength(ACK_HEADER_LEN);
            SetAckType(NormAck::FLUSH);
            SetFecId(129); // only one supported for the moment
            ((UINT8*)buffer)[RESERVED_OFFSET] = 0;
            length = ACK_HEADER_LEN+PAYLOAD_LENGTH;
        }
        
        // Note: must apply any header exts _before_ the payload is set
        void SetFecId(UINT8 fecId) 
            {((UINT8*)buffer)[header_length+FEC_ID_OFFSET] = fecId;}
        void SetObjectId(NormObjectId objectId)
        {
            ((UINT16*)buffer)[(header_length/2)+OBJ_ID_OFFSET] = htons((UINT16)objectId);
        }
        void SetFecBlockId(const NormBlockId& blockId)
        {
            buffer[(header_length/4)+BLOCK_ID_OFFSET] = htonl((UINT32)blockId);
        }
        void SetFecBlockLen(UINT16 blockLen)
        {
            ((UINT16*)buffer)[(header_length/2)+BLOCK_LEN_OFFSET] = htons(blockLen);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            ((UINT16*)buffer)[(header_length/2)+SYMBOL_ID_OFFSET] = htons(symbolId);
        }
        
        // Message processing
        UINT8 GetFecId() {return ((UINT8*)buffer)[header_length+FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
        {
            return (ntohs(((UINT16*)buffer)[(header_length/2)+OBJ_ID_OFFSET]));
        }        
        NormBlockId GetFecBlockId() const
        {
            return (ntohl(buffer[(header_length/4)+BLOCK_ID_OFFSET]));
        }
        UINT16 GetFecBlockLen() const
        {
            return (ntohs(((UINT16*)buffer)[(header_length/2)+BLOCK_LEN_OFFSET]));
        }
        UINT16 GetFecSymbolId() const
        {
            return (ntohs(((UINT16*)buffer)[(header_length/2)+SYMBOL_ID_OFFSET]));  
        }
        
    private:
        // These are the payload offsets for "fec_id" = 129 
        // "fec_payload_id" field
        enum
        {
            FEC_ID_OFFSET       = 0,
            RESERVED_OFFSET     = FEC_ID_OFFSET + 1,
            OBJ_ID_OFFSET       = (RESERVED_OFFSET+1)/2,
            BLOCK_ID_OFFSET     = ((OBJ_ID_OFFSET*2)+2)/4,
            BLOCK_LEN_OFFSET    = ((BLOCK_ID_OFFSET*4)+4)/2,
            SYMBOL_ID_OFFSET    = ((BLOCK_LEN_OFFSET*2)+2)/2,
            PAYLOAD_LENGTH      = (SYMBOL_ID_OFFSET*2)+2
        };
            
};  // end class NormAckFlushMsg

class NormReportMsg : public NormMsg
{
    // (TBD)
    
};  // end class NormReportMsg

// One we've defined our basic message types, we
// do some unions so we can easily use these
// via casting or dereferencing the union members

class NormMessageQueue
{
    public:
        NormMessageQueue();
        ~NormMessageQueue();
        void Destroy();
        void Prepend(NormMsg* msg);
        void Append(NormMsg* msg);
        void Remove(NormMsg* msg);
        NormMsg* RemoveHead();
        NormMsg* RemoveTail();
        bool IsEmpty() {return ((NormMsg*)NULL == head);}
    
    private:
        NormMsg*    head;
        NormMsg*    tail;
};  // end class NormMessageQueue

#endif // _NORM_MESSAGE
