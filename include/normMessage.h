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
#else
#include <sys/types.h>  // for off_t
#endif // if/else _WIN32_WCE

#ifdef SIMULATE
// IMPORTANT! This _assumes_ that the message header of interest _will_ be
//            aligned with a NormSegment (i.e. flush mode of active or passive
//            is used with flushing after _each_ message written to a
//            NORM_OBJECT_STREAM!
#define SIM_PAYLOAD_MAX (36+8)   // MGEN message size + StreamPayloadHeaderLen()
#endif // SIMULATE

const UINT8 NORM_PROTOCOL_VERSION = 1;

// This value is used in a couple places in the code as 
// a safety check where some critical timeouts may be
// less than expected operating system clock resolution
const double NORM_TICK_MIN = 0.100; // in seconds

// Pick a random number from 0..max
inline double UniformRand(double max)
    {return (max * ((double)rand() / (double)RAND_MAX));}

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
// are good for the range of 1.0e-06 <= 1000.0 seconds)

const double NORM_GRTT_MIN = 0.001;  // 1 msec
const double NORM_GRTT_MAX = 15.0;   // 15 sec
const double NORM_RTT_MIN = 1.0e-06;
const double NORM_RTT_MAX = 1000.0;   
extern const double NORM_RTT[];     
inline double NormUnquantizeRtt(UINT8 qrtt)
    {return NORM_RTT[qrtt];}
UINT8 NormQuantizeRtt(double rtt);

extern const double NORM_GSIZE[];
inline double NormUnquantizeGroupSize(UINT8 gsize)
    {return NORM_GSIZE[gsize];}

UINT8 NormQuantizeGroupSize(double gsize);

inline UINT16 NormQuantizeLoss(double lossFraction)
{
    lossFraction = MAX(lossFraction, 0.0);
    lossFraction = lossFraction*65535.0 + 0.5;
    lossFraction = MIN(lossFraction, 65535.0);
    return (UINT16)lossFraction;
}  // end NormQuantizeLossFraction()
inline double NormUnquantizeLoss(UINT16 lossQuantized)
{
    return (((double)lossQuantized) / 65535.0);
}  // end NormUnquantizeLossFraction()


// Extended precision Norm loss quantize/unquantize with
// 32-bit precision (needed for low BER, high bandwidth*delay) 
inline UINT32 NormQuantizeLoss32(double lossFraction)
{
    const double MAX_SCALE = (double)((unsigned int)0xffffffff);
    lossFraction = MAX(lossFraction, 0.0);
    lossFraction = lossFraction*MAX_SCALE + 0.5;
    lossFraction = MIN(lossFraction, MAX_SCALE);
    return (UINT32)lossFraction;
}  // end NormQuantizeLossFraction32()
inline double NormUnquantizeLoss32(UINT32 lossQuantized)
{
    const double MAX_SCALE = (double)((unsigned int)0xffffffff);
    return (((double)lossQuantized) / MAX_SCALE);
}  // end NormUnquantizeLossFraction32()


inline UINT16 NormQuantizeRate(double rate)
{
    if (rate <= 0.0) return 0x01;  // rate = 0.0
    UINT16 exponent = (UINT16)log10(rate);
    UINT16 mantissa = (UINT16)((4096.0/10.0) * (rate / pow(10.0, (double)exponent)) + 0.5);
    return ((mantissa << 4) | exponent);
}
inline double NormUnquantizeRate(UINT16 rate)
{
    double mantissa = ((double)(rate >> 4)) * (10.0/4096.0);
    double exponent = (double)(rate & 0x000f);
    return mantissa * pow(10.0, exponent);   
}

class NormObjectSize
{
    public:
#ifdef WIN32
#define _FILE_OFFSET_BITS 64
		typedef __int64 Offset;
#else
		typedef off_t Offset;
#endif  // if/else WIN32
        NormObjectSize() : size(0) {}
        NormObjectSize(Offset theSize) : size(theSize) {}
        NormObjectSize(UINT16 msb, UINT32 lsb)
        {
            size = (Offset)lsb;
#if (_FILE_OFFSET_BITS > 32) && !defined(ANDROID)                     
            size |=  ((Offset)msb) << 32;
#endif   
        }
        Offset GetOffset() const {return size;}
#if (_FILE_OFFSET_BITS > 32) && !defined(ANDROID)
	    UINT16 MSB() const {return ((UINT16)((size >> 32) & 0x0000ffff));}
#else
	    UINT16 MSB() const {return 0;} 
#endif
	    UINT32 LSB() const {return ((UINT32)(size & 0xffffffff));}
        // Operators
        bool operator==(const NormObjectSize& b) const
            {return (b.size == size);}
        bool operator!=(const NormObjectSize& b) const
            {return (b.size != size);}
        NormObjectSize operator+(const NormObjectSize& b) const
        {
            NormObjectSize result(size);
            result.size += b.size;
            return result;
        }
        void operator+=(const NormObjectSize& b)
            {size += b.size;} 
        NormObjectSize operator-(const NormObjectSize& b) const
        {
            NormObjectSize result(size);
            result.size -= b.size;
            return result;
        }
        void operator-=(const NormObjectSize& b)
            {size -= b.size;} 
        void operator+=(Offset increment)
            {size += increment;}    
        bool operator>(const NormObjectSize& b) const
            {return (size > b.size);}
        NormObjectSize operator*(const NormObjectSize& b) const
        {
            NormObjectSize result(size);
            result.size *= b.size;
            return result;
        }
        // Note: this is a "round-upwards" division operator
        NormObjectSize operator/(const NormObjectSize& b) const
        {
            NormObjectSize result(size);
            result.size /= b.size;
            result.size = ((result.size * b.size) < size) ? result.size + 1 : result.size;
            return result;      
        }
    private:
        Offset   size;
};  // end class NormObjectSize

#ifndef _NORM_API
typedef UINT32 NormNodeId;
const NormNodeId NORM_NODE_NONE = 0x00000000;
const NormNodeId NORM_NODE_ANY  = 0xffffffff;
#endif // !_NORM_API

class NormObjectId
{
    public:
        NormObjectId() : value(0) {};
        NormObjectId(UINT16 id) {value = id;}
        NormObjectId(const NormObjectId& id) {value = id.value;}
        operator UINT16() const {return value;}
        //INT16 operator-(const NormObjectId& id) const
        //    {return ((INT16)(value - id.value));}
        
        bool operator<(const NormObjectId& id) const
        {
            UINT16 diff = value - id.value;
            return ((diff > 0x8000) || ((0x8000 == diff) && (value > id.value)));
        }
        
        bool operator>(const NormObjectId& id) const
        {
            UINT16 diff = id.value - value;
            return ((diff > 0x8000) || ((0x8000 == diff) && (id.value > value)));
        }
        
        bool operator<=(const NormObjectId& id) const
            {return ((value == id.value) || (*this < id));}
        bool operator>=(const NormObjectId& id) const
            {return ((value == id.value) || (*this > id));}
        bool operator==(const NormObjectId& id) const
            {return (value == id.value);}
        bool operator!=(const NormObjectId& id) const
            {return (value != id.value);}
        
        void operator-=(UINT16 delta)
            {value -= delta;}
        
        const char* GetValuePtr() const
            {return (const char*)(&value);}
        
        NormObjectId& operator++(int) {value++; return *this;}
        NormObjectId& operator--(int) {value--; return *this;}
        
    private:
        UINT16  value;  
};  // end class NormObjectId

class NormBlockId
{
    public:
        NormBlockId() {};
        NormBlockId(UINT32 id) : value(id) {}
        
        UINT32 GetValue() const 
            {return value;}
        
        const char* GetValuePtr() const
            {return ((const char*)&value);}
        
        bool operator==(const NormBlockId& id) const
            {return (value == id.value);}
        bool operator!=(const NormBlockId& id) const
            {return (value != id.value);}
        
        // These static helper methods provide a "mask" parameter to allow
        // for different bit-length NormBlockId values (depends upon FEC encoding scheme).
        // The "mask" is in the eye-of-the-beholder, i.e., the given NormObject
        // or NormBlockBuffer that is manipulating block id values for protocol purposes
        // provide the FEC block "mask" to these methods.
        // We may reconsider adding the "mask" as a NormBlockId member variable, but since
        // NormBlockId usage is so ubiquitous, it could be added overhead consider within
        // the context of sender, object, etc a common fec_block_mask is used.
        
        // Compute difference of (a - b). If a non-zero bit "mask" is given, the difference
        // is for the masked word size (e.g., mask = 0x00ffffff is a 24-bit integer).
        static INT32 Difference(const NormBlockId& a, const NormBlockId& b, UINT32 mask)
        {
            if (mask)
            {
                UINT32 sign = (mask ^ (mask >> 1));
                UINT32 result = a.value - b.value;
                if (0 == (result & sign))
                    return (INT32)(result & mask);
                else if ((result != sign) || (a.value < b.value))
                    return (INT32)(result | ~mask);
                else
                    return (INT32)(result & mask);
            }
            else
            {
                return ((INT32)(a.value - b.value));
            }
        }
        
        // Compare two block ids.  If a non-zero bit "mask" is given, the comparison
        // is a "sliding window" (signed) over the bit space.  Otherwise, it is 
        // simply an unsigned value comparison.
        // Returns -1, 0, +1 for (a < b), (a == b), and (a > b), respectively
        static int Compare(const NormBlockId& a, const NormBlockId& b, UINT32 mask)
        {
            if (mask)
            {
                // "Sliding window" comparison
                INT32 delta = Difference(a, b, mask);
                if (delta < 0)
                    return -1;
                else if (0 == delta)
                    return 0;
                else  // if delta > 0
                    return 1;
            }
            else if (a.value < b.value)
            {
                return  -1;
            }
            else if (a.value == b.value)
            {
                return 0;
            }
            else // if (a > b)
            {
                return 1;
            }
        }
        
        void Increment(UINT32 i, UINT32 mask)
        {
            value = value + i;
            if (mask) value &= mask;
        }
        
        void Decrement(UINT32 i, UINT32 mask)
        {
            if (mask && (value < i))
                value = (mask - (i - value) + 1);
            else
                value -= i;
        }
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
            FTI         =  64,  // FEC Object Transmission Information (FTI) extension
            CC_FEEDBACK =   3,  // NORM-CC Feedback extension
            CC_RATE     = 128,  // NORM-CC Rate extension
            APP_ACK     =  65   // app-defined ACK extension (see NormSetWatermarkEx())
        }; 
            
        NormHeaderExtension();
        virtual ~NormHeaderExtension() {}
        virtual void Init(UINT32* theBuffer, UINT16 numBytes) 
        {
            // TBD - should we confirm that 'numBytes' is sufficient
            AttachBuffer(theBuffer, numBytes);
            SetType(INVALID);
            SetWords(0);
        }
        void SetType(Type type) 
            {((UINT8*)buffer)[TYPE_OFFSET] = (UINT8)type;}
        void SetWords(UINT8 words) 
            {((UINT8*)buffer)[LENGTH_OFFSET] = words;}
        
        void AttachBuffer(const UINT32* theBuffer, UINT16 bufferLength) 
        {
                buffer = (UINT32*)theBuffer;
                buffer_length = bufferLength;
        }
        const UINT32* GetBuffer() 
            {return buffer;}
         
        Type GetType() const
            {return buffer ? (Type)(((UINT8*)buffer)[TYPE_OFFSET]) : INVALID;}
        
        UINT16 GetLength() const
        {
            return (buffer ? 
                        ((GetType() < 128) ? 
                            ((((UINT8*)buffer)[LENGTH_OFFSET]) << 2) : 4) : 
                        0);  
        }
        // These currently only used for APP_ACK extension
        const char* GetContent() 
            {return (((const char*)buffer) + CONTENT_OFFSET);}
        
        UINT16 GetContentLength() const
        {
            UINT16 totalLen = GetLength();
            return ((totalLen > CONTENT_OFFSET) ? (totalLen - CONTENT_OFFSET) : 0);
        }
        static UINT16 GetContentOffset() 
            {return (UINT16)CONTENT_OFFSET;}
                  
    protected:   
        enum
        {
            TYPE_OFFSET     = 0,                // UINT8 offset
            LENGTH_OFFSET   = TYPE_OFFSET + 1,  // UINT8 offset
            CONTENT_OFFSET  = LENGTH_OFFSET + 1 // UINT8 offset
        };
        UINT32*   buffer;
        UINT16    buffer_length;
};  // end class NormHeaderExtension

    
// This class is what we use to set/get
// FEC Payload Id content.  The FEC Payload
// Id format is dependent upon the "fec_id" (FEC Type)
// and, in some cases, its field size ("m") parameter
class NormPayloadId
{
    public:
        enum FecType
        {
            RS  = 2,   // fully-specified, general purpose Reed-Solomon
            RS8 = 5,   // fully-specified 8-bit Reed-Solmon per RFC 5510
            SB  = 129  // partially-specified "small block" codes
        };
        static bool IsValid(UINT8 fecId) 
        {
            switch (fecId)
            {
                case 2:
                case 5:
                case 129:
                    return true;
                default:
                    return false;
            }
        }
        NormPayloadId(UINT8 fecId, UINT8 m, UINT32* theBuffer)
            : fec_id(fecId), fec_m(m), buffer(theBuffer) {}
        NormPayloadId(UINT8 fecId, UINT8 m, const UINT32* theBuffer)
            : fec_id(fecId), fec_m(m), cbuffer(theBuffer) {}
        
        static UINT16 GetLength(UINT8 fecId)
        {
            switch (fecId)
            {
                case 2:
                case 5:
                    return 4;
                case 129:
                    return 8;
                default:
                    return 0;
            }
        }
        
        static UINT32 GetFecBlockMask(UINT8 fecId, UINT8 fecM)
        {
            switch (fecId)
            {
                case 2:
                    if (8 == fecM)
                        return 0x00ffffff;  // 24-bit blockId, 8-bit symbolId
                    else // (16 == fec_m)
                        return 0x0000ffff;  // 16-bit blockId,, 16-bit symbolId
                case 5:
                    return 0x00ffffff;      // 24-bit blockId
                case 129:
                    return 0xffffffff;      // 32-bit blockId
                default:
                    return 0x00000000;      // invalid fecId
            }
        }
        
        void SetFecPayloadId(UINT32 blockId, UINT16 symbolId, UINT16 blockLen)
        {
            switch (fec_id)
            {
                case 2:
                    if (8 == fec_m)
                    {
                        blockId = (blockId << 8) | (symbolId & 0x00ff);
                        *buffer = htonl(blockId); // 3 + 1 bytes
                    }
                    else // (16 == fec_m)
                    {
                        UINT16* payloadId = (UINT16*)buffer;
                        payloadId[0] = htons(blockId);  // 2 bytes
                        payloadId[1] = htons(symbolId); // 2 bytes
                    }
                    break;
                case 5:
                    blockId = (blockId << 8) | (symbolId & 0x00ff);
                    *buffer = htonl(blockId);  // 3 + 1 bytes
                    break;
                case 129:
                    *buffer = htonl(blockId);  // 4 bytes
                    UINT16* ptr = (UINT16*)(buffer + 1);
                    ptr[0] = htons(blockLen);  // 2 bytes
                    ptr[1] = htons(symbolId);  // 2 bytes
                    break;
            }   
        }
        
        // Message processing methods
        NormBlockId GetFecBlockId() const
        {
            switch (fec_id)
            {
                case 2:
                    if (8 == fec_m)
                    {
                        UINT32 blockId = ntohl(*cbuffer);
                        return (0x00ffffff & (blockId >> 8));
                    }
                    else // (16 == fec_m)
                    {
                        UINT16* blockId = (UINT16*)cbuffer;
                        return ntohs(*blockId);
                    }
                case 5:
                {
                    UINT32 blockId = ntohl(*cbuffer);
                    return (0x00ffffff & (blockId >> 8));
                }
                case 129:
                    return ntohl(*cbuffer);
                default:
                    ASSERT(0);
                    return 0;
            }
        }        
        
        UINT16 GetFecSymbolId()  const
        {
            switch (fec_id)
            {
                case 2:
                    if (8 == fec_m)
                    {
                        UINT32 payloadId = ntohl(*cbuffer);
                        return (0x000000ff & payloadId);  // lsb is symbolId
                    }
                    else // ( 16 == fec_m)
                    {
                        UINT16* payloadId = (UINT16*)cbuffer;
                        return ntohs(payloadId[1]);
                    }
                case 5:
                {
                    UINT32 payloadId = ntohl(*cbuffer);
                    return (0x000000ff & payloadId);  // lsb is symbolId
                }
                case 129:
                {
                    UINT16* ptr = (UINT16*)(cbuffer + 1);
                    return ntohs(ptr[1]);
                }
                default:
                    ASSERT(0);
                    return 0;
            }      
        } 
        
        UINT16 GetFecBlockLength() const
        {
            if (129 == fec_id)
            {
                UINT16* blockLen = (UINT16*)(cbuffer + 1);
                return ntohs(*blockLen);
            }
            else
            {
                return 0;
            }
        }        
                
        
        
    private:
        UINT8   fec_id;
        UINT8   fec_m;
        union
        {
            UINT32*       buffer;   
            const UINT32* cbuffer;
        };
};  // end class NormPayloadId

class NormMsg
{
    friend class NormMessageQueue;
    
    public:
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
        enum {MAX_SIZE = 65536};
               
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
        // For messages to be sent, "addr" is destination
        void SetDestination(const ProtoAddress& dst) {addr = dst;}
        // For message received, "addr" is source
        void SetSource(const ProtoAddress& src) {addr = src;}
        
        void AttachExtension(NormHeaderExtension& extension)
        {
            extension.Init(buffer+(header_length/4), MAX_SIZE - header_length);
            ExtendHeaderLength(extension.GetLength());
        }
        // Only use this for extensions that have content appended after attachment
        // (Fixed-length extensions should set their length upon Init())
        void PackExtension(NormHeaderExtension& extension)
        {
            ExtendHeaderLength(2 + extension.GetContentLength());
        }
        
        // Message processing routines
        bool InitFromBuffer(UINT16 msgLength);
        bool CopyFromBuffer(const char* theBuffer, unsigned int theLength)
        {
            if (theLength > MAX_SIZE) return false;
            memcpy(buffer, theBuffer, theLength);
            return InitFromBuffer(theLength);
        }
        UINT8 GetVersion() const 
            {return (((UINT8*)buffer)[VERSION_OFFSET] >> 4);}
        NormMsg::Type GetType() const 
            {return (Type)(((UINT8*)buffer)[TYPE_OFFSET] & 0x0f);}
        UINT16 GetHeaderLength() const
            {return ((UINT8*)buffer)[HDR_LEN_OFFSET] << 2;}
        UINT16 GetBaseHeaderLength()
            {return header_length_base;}
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
        const char* GetBuffer() const {return ((char*)buffer);}
        UINT16 GetLength() const {return length;}     
        
        void Display() const; // hex output to log
        
        // To retrieve any attached header extensions
        bool HasExtensions() const {return (header_length > header_length_base);}
        bool HasExtension(NormHeaderExtension::Type extType);
        bool GetNextExtension(NormHeaderExtension& ext) const
        {
            const UINT32* currentBuffer = ext.GetBuffer();
            // 'nextOffset' here is a UINT32 offset
            UINT16 nextOffset = 
                (UINT16)(currentBuffer ? (currentBuffer - buffer + (ext.GetLength()/4)) : 
			                             (header_length_base/4));
            bool result = HasExtensions() ? (nextOffset < (header_length/4)) : false;
            if (result)
            {
                UINT16 nextLength = ((UINT8*)(buffer + nextOffset))[1] << 2;
                ext.AttachBuffer(buffer+nextOffset, nextLength);
            }
            else
            {
                ext.AttachBuffer((UINT32*)NULL, 0);
            }
            return result;
        }
        
        // For message reception and misc.
        char* AccessBuffer() {return ((char*)buffer);} 
        ProtoAddress& AccessAddress() {return addr;} 
        
        NormMsg* GetNext() {return next;}
        
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
            FLAG_SYN        = 0x40
            //FLAG_MSG_START  = 0x40 deprecated
        }; 
        UINT16 GetInstanceId() const
            {return (ntohs(((UINT16*)buffer)[INSTANCE_ID_OFFSET]));}
        UINT8 GetGrtt() const 
            {return ((UINT8*)buffer)[GRTT_OFFSET];} 
        UINT8 GetBackoffFactor() const 
            {return ((((UINT8*)buffer)[GSIZE_OFFSET] >> 4) & 0x0f);}
        UINT8 GetGroupSize() const 
            {return (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f);} 
        bool FlagIsSet(NormObjectMsg::Flag flag) const
            {return (0 != (flag & ((UINT8*)buffer)[FLAGS_OFFSET]));}
        bool IsStream() const 
            {return FlagIsSet(FLAG_STREAM);}
        UINT8 GetFecId() const 
            {return ((UINT8*)buffer)[FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
            {return (ntohs(((UINT16*)buffer)[OBJ_ID_OFFSET]));}
        
        // Message building routines
        void SetInstanceId(UINT16 instanceId)
            {((UINT16*)buffer)[INSTANCE_ID_OFFSET] = htons(instanceId);}
        void SetGrtt(UINT8 grtt) {((UINT8*)buffer)[GRTT_OFFSET] = grtt;}
        void SetBackoffFactor(UINT8 backoff)
            {((UINT8*)buffer)[BACKOFF_OFFSET] =  (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f) | (backoff << 4);}
        void SetGroupSize(UINT8 gsize) 
            {((UINT8*)buffer)[GSIZE_OFFSET] =  (((UINT8*)buffer)[GSIZE_OFFSET] & 0xf0) | gsize;}
        void ResetFlags() {((UINT8*)buffer)[FLAGS_OFFSET] = 0;}
        void SetFlag(NormObjectMsg::Flag flag) {((UINT8*)buffer)[FLAGS_OFFSET] |= flag;}
        void SetObjectId(const NormObjectId& objectId)
            {((UINT16*)buffer)[OBJ_ID_OFFSET] = htons((UINT16)objectId);}
        
    protected: 
        enum
        {
            INSTANCE_ID_OFFSET  = MSG_OFFSET/2,
            GRTT_OFFSET         = (INSTANCE_ID_OFFSET*2)+2,
            BACKOFF_OFFSET      = GRTT_OFFSET+1,
            GSIZE_OFFSET        = BACKOFF_OFFSET,
            FLAGS_OFFSET        = GSIZE_OFFSET+1,
            FEC_ID_OFFSET       = FLAGS_OFFSET+1,
            OBJ_ID_OFFSET       = (FEC_ID_OFFSET+1)/2,
            OBJ_MSG_OFFSET      = (OBJ_ID_OFFSET*2)+2
        };          
};  // end class NormObjectMsg


// This FEC Object Transmission Information assumes "fec_id" == 2 (RFC 5510)
// (This is an m-bit Reed-Solomon codec (we use it in NORM for m == 16)
class NormFtiExtension2 : public NormHeaderExtension
{
    public:
        // To build the FTI Header Extension
        virtual void Init(UINT32* theBuffer, UINT16 numBytes)
        {
            AttachBuffer(theBuffer, numBytes);
            SetType(FTI);  // HET = 64
            SetWords(4);
        }    
        void SetObjectSize(const NormObjectSize& objectSize)
        {
            ((UINT16*)buffer)[OBJ_SIZE_MSB_OFFSET] = htons(objectSize.MSB());
            buffer[OBJ_SIZE_LSB_OFFSET] = htonl(objectSize.LSB());
        }
        void SetFecFieldSize(UINT8 numBits)
            {((UINT8*)buffer)[FEC_M_OFFSET] = numBits;}  // usually 16 for this FTI
        void SetFecGroupSize(UINT8 symbolsPerPkt)
            {((UINT8*)buffer)[FEC_G_OFFSET] = symbolsPerPkt;}  // usually one
        void SetSegmentSize(UINT16 segmentSize)
            {((UINT16*)buffer)[SEG_SIZE_OFFSET] = htons(segmentSize);}
        void SetFecMaxBlockLen(UINT16 ndata)
            {((UINT16*)buffer)[FEC_NDATA_OFFSET] = htons(ndata);}
        void SetFecNumParity(UINT16 nparity)
            {((UINT16*)buffer)[FEC_NPARITY_OFFSET] = htons(nparity);}
        
        // FTI Extension parsing methods
        NormObjectSize GetObjectSize() const
        {
            return NormObjectSize(ntohs(((UINT16*)buffer)[OBJ_SIZE_MSB_OFFSET]), 
                                  ntohl(buffer[OBJ_SIZE_LSB_OFFSET]));   
        }
        UINT8 GetFecFieldSize() const
            {return ((UINT8*)buffer)[FEC_M_OFFSET];}  // usually 16 for this FTI
        UINT8 GetFecGroupSize() const
            {return ((UINT8*)buffer)[FEC_G_OFFSET];}  // usually 1
        UINT16 GetSegmentSize() const
            {return (ntohs(((UINT16*)buffer)[SEG_SIZE_OFFSET]));}
        UINT16 GetFecMaxBlockLen() const
            {return (ntohs(((UINT16*)buffer)[FEC_NDATA_OFFSET]));}
        UINT16 GetFecNumParity() const
            {return (ntohs(((UINT16*)buffer)[FEC_NPARITY_OFFSET]));}
        
    private:
        enum
        {
            OBJ_SIZE_MSB_OFFSET = (LENGTH_OFFSET + 1)/2,
            OBJ_SIZE_LSB_OFFSET = ((OBJ_SIZE_MSB_OFFSET*2)+2)/4,
            FEC_M_OFFSET        = ((OBJ_SIZE_LSB_OFFSET*4)+4),
            FEC_G_OFFSET        = FEC_M_OFFSET + 1,   
            SEG_SIZE_OFFSET     = (FEC_G_OFFSET+1)/2,
            FEC_NDATA_OFFSET    = ((SEG_SIZE_OFFSET*2)+2)/2,
            FEC_NPARITY_OFFSET  = ((FEC_NDATA_OFFSET*2)+2)/2
        };  
};  // end class NormFtiExtension2

// Helper class for containing key FTI params
class NormFtiData
{
    public:
        NormFtiData()
          : object_size(NormObjectSize(0)), segment_size(0),
            num_data(0), num_parity(0), fec_m(0), instance_id(0) {}
        ~NormFtiData() {}
        
        bool IsValid() const
            {return (0 != segment_size);}
        
        void Invalidate()
        {
            object_size = 0;
            segment_size = num_data = num_parity = instance_id = 0;
            fec_m = 0;
        }       
        
        void SetObjectSize(const NormObjectSize& objectSize)
            {object_size = objectSize;}
        void SetSegmentSize(UINT16 segmentSize)
            {segment_size = segmentSize;}
        void SetFecMaxBlockLen(UINT16 numData)
            {num_data = numData;}
        void SetFecNumParity(UINT16 numParity)
            {num_parity = numParity;}
        void SetFecFieldSize(UINT8 fecM)
            {fec_m = fecM;}
        void SetFecInstanceId(UINT16 instanceId)
            {instance_id = instanceId;}
        
        const NormObjectSize& GetObjectSize() const 
            {return object_size;}
        UINT16 GetSegmentSize() const
            {return segment_size;}
        UINT16 GetFecMaxBlockLen() const
            {return num_data;}
        UINT16 GetFecNumParity() const
            {return num_parity;}
        UINT8 GetFecFieldSize() const
            {return fec_m;}
        UINT16 GetFecInstanceId() const
            {return instance_id;}
        
    private:
        NormObjectSize  object_size;
        UINT16          segment_size;
        UINT16          num_data;
        UINT16          num_parity;
        UINT8           fec_m;
        UINT16          instance_id;
        
};  // end class NormFtiData

// This FEC Object Transmission Information assumes "fec_id" == 5 (RFC 5510)
// (this is the fully-defined 8-bit Reed-Solomon codec)
class NormFtiExtension5 : public NormHeaderExtension
{
    public:
        // To build the fec_id=5 FTI Header Extension
        virtual void Init(UINT32* theBuffer, UINT16 numBytes)
        {
            AttachBuffer(theBuffer, numBytes);
            SetType(FTI);  // HET = 64
            SetWords(3);
        }    
        void SetObjectSize(const NormObjectSize& objectSize)
        {
            ((UINT16*)buffer)[OBJ_SIZE_MSB_OFFSET] = htons(objectSize.MSB());
            buffer[OBJ_SIZE_LSB_OFFSET] = htonl(objectSize.LSB());
        }
        void SetSegmentSize(UINT16 segmentSize)
            {((UINT16*)buffer)[SEG_SIZE_OFFSET] = htons(segmentSize);}
        void SetFecMaxBlockLen(UINT8 ndata)
            {((UINT8*)buffer)[FEC_NDATA_OFFSET] = ndata;}
        void SetFecNumParity(UINT8 nparity)
            {((UINT8*)buffer)[FEC_NPARITY_OFFSET] = nparity;}
        
        // FTI Extension parsing methods
        NormObjectSize GetObjectSize() const
        {
            return NormObjectSize(ntohs(((UINT16*)buffer)[OBJ_SIZE_MSB_OFFSET]), 
                                  ntohl(buffer[OBJ_SIZE_LSB_OFFSET]));   
        }
        UINT16 GetSegmentSize() const
            {return (ntohs(((UINT16*)buffer)[SEG_SIZE_OFFSET]));}
        UINT8 GetFecMaxBlockLen() const
            {return (((UINT8*)buffer)[FEC_NDATA_OFFSET]);}
        UINT8 GetFecNumParity() const
            {return (((UINT8*)buffer)[FEC_NPARITY_OFFSET]);}
        
    private:
        enum
        {
            OBJ_SIZE_MSB_OFFSET = (CONTENT_OFFSET)/2,       // UINT16 offset
            OBJ_SIZE_LSB_OFFSET = ((OBJ_SIZE_MSB_OFFSET*2)+2)/4,
            SEG_SIZE_OFFSET = ((OBJ_SIZE_LSB_OFFSET*4)+4)/2,
            FEC_NDATA_OFFSET    = ((SEG_SIZE_OFFSET+1)*2),
            FEC_NPARITY_OFFSET  = (FEC_NDATA_OFFSET+1)
        };  
};  // end class NormFtiExtension5

class NormAppAckExtension : public NormHeaderExtension
{
    public:
        virtual void Init(UINT32* theBuffer, UINT16 numBytes)
        {
            AttachBuffer(theBuffer, numBytes);
            SetType(APP_ACK);
            SetWords(0);
        }  
        bool SetContent(const char* data, UINT16 dataLen)
        {
            if (dataLen > (buffer_length - CONTENT_OFFSET)) return false;
            memcpy(((char*)buffer) + CONTENT_OFFSET, data, dataLen);
            if (dataLen > 2)
            {
                // Pad out to get 32-bit alignment of extension
                dataLen += CONTENT_OFFSET;
                UINT16 padLen = dataLen % 4;
                if (padLen)
                    padLen = 4 - padLen;
                dataLen += padLen;
                SetWords(dataLen/4);
            }    
            else
            {
                SetWords(1);
            }       
            return true;       
        }
};  // end class NormAppAckExtension


// This FEC Object Transmission Information assumes "fec_id" == 129
class NormFtiExtension129 : public NormHeaderExtension
{
    public:
        // To build the FTI Header Extension
        // (TBD) allow for different "fec_id" types in the future
        virtual void Init(UINT32* theBuffer, UINT16 numBytes)
        {
            AttachBuffer(theBuffer, numBytes);
            SetType(FTI);
            SetWords(4);
        }        
        void SetFecInstanceId(UINT16 instanceId)
            { ((UINT16*)buffer)[FEC_INSTANCE_OFFSET] = htons(instanceId);}
        void SetFecMaxBlockLen(UINT16 ndata)
            {((UINT16*)buffer)[FEC_NDATA_OFFSET] = htons(ndata);}
        void SetFecNumParity(UINT16 nparity)
            {((UINT16*)buffer)[FEC_NPARITY_OFFSET] = htons(nparity);}
        void SetSegmentSize(UINT16 segmentSize)
            {((UINT16*)buffer)[SEG_SIZE_OFFSET] = htons(segmentSize);}
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
            {return (ntohs(((UINT16*)buffer)[FEC_NDATA_OFFSET]));}
        UINT16 GetFecNumParity() const
            {return (ntohs(((UINT16*)buffer)[FEC_NPARITY_OFFSET]));}
        UINT16 GetSegmentSize() const
            {return (ntohs(((UINT16*)buffer)[SEG_SIZE_OFFSET]));}
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
};  // end class NormFtiExtension129


class NormInfoMsg : public NormObjectMsg
{
    public:
        void Init()
        {
            SetType(INFO);
            SetBaseHeaderLength(INFO_HEADER_LEN);
            ResetFlags();
        }
                
        UINT16 GetInfoLen() const 
            {return (length - header_length);}
        const char* GetInfo() const 
            {return (((char*)buffer) + header_length);}
        
        // Message building methods (in addition to NormObjectMsg fields)
        void SetFecId(UINT8 fecId)
            {((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;}
        
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
            ResetFlags();
            // Note: for NORM_DATA base header length depends on fec_id
        }
        
        // Message building methods (in addition to NormObjectMsg fields)
        void SetFecId(UINT8 fecId)
        {
            ((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;
            SetBaseHeaderLength(OBJ_MSG_OFFSET + NormPayloadId::GetLength(fecId));
        }
        
        void SetFecPayloadId(UINT8 fecId, UINT32 blockId, UINT16 symbolId, UINT16 blockLen, UINT8 m)
        {
            NormPayloadId payloadId(fecId, m, buffer + FEC_PAYLOAD_ID_OFFSET);
            payloadId.SetFecPayloadId(blockId, symbolId, blockLen);
        }
        
        // Two ways to set payload content:
        // 1) Directly access payload to copy segment, then set data message length
        //    (Note NORM_STREAM_OBJECT segments must already include "payload_len"
        //    and "payload_offset" with the "payload_data"
        char* AccessPayload() {return (((char*)buffer)+header_length);}
        // For NORM_STREAM_OBJECT segments, "dataLength" must include the PAYLOAD_HEADER_LENGTH
        void SetPayloadLength(UINT16 payloadLength)
            {length = header_length + payloadLength;}
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
        NormBlockId GetFecBlockId(UINT8 m) const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockId();
        }    
        UINT16 GetFecSymbolId(UINT8 m)  const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecSymbolId();
        }   
        UINT16 GetFecBlockLength() const
        {
            NormPayloadId payloadId(GetFecId(), 8, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockLength();
        }  
            
        // Note: For NORM_OBJECT_STREAM, "payload" includes "payload_reserved",  
        //       "payload_len", "payload_offset", and "payload_data" fields
        //       For NORM_OBJECT_FILE and NORM_OBJECT_DATA, "payload" includes
        //       "payload_data" only
        const char* GetPayload() 
            const {return (((char*)buffer)+header_length);}
        UINT16 GetPayloadLength() 
            const {return (length - header_length);}
        
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
        static UINT16 GetStreamPayloadHeaderLength() 
            {return (PAYLOAD_DATA_OFFSET);}
        
        static void WriteStreamPayloadLength(char* payload, UINT16 len)
        {
            UINT16 temp16 = htons(len);
            memcpy(payload+PAYLOAD_LENGTH_OFFSET, &temp16, 2);
        }
        static void WriteStreamPayloadMsgStart(char* payload, UINT16 msgStartOffset)
        {
            UINT16 temp16 = htons(msgStartOffset);
            memcpy(payload+PAYLOAD_MSG_START_OFFSET, &temp16, 2);
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
        static UINT16 ReadStreamPayloadMsgStart(const char* payload)
        {
            UINT16 temp16;
            memcpy(&temp16, payload+PAYLOAD_MSG_START_OFFSET, 2);
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
            FEC_PAYLOAD_ID_OFFSET = OBJ_MSG_OFFSET/4
        };
              
        // IMPORTANT: These offsets are _relative_ to the NORM_DATA header
        //            (incl. any extensions)   
        enum
        {
            PAYLOAD_LENGTH_OFFSET    = 0,    
            PAYLOAD_MSG_START_OFFSET = PAYLOAD_LENGTH_OFFSET+2,
            PAYLOAD_OFFSET_OFFSET    = PAYLOAD_MSG_START_OFFSET+2,
            PAYLOAD_DATA_OFFSET      = PAYLOAD_OFFSET_OFFSET+4   
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
        
        // Message build
        void SetInstanceId(UINT16 instanceId)
            {((UINT16*)buffer)[INSTANCE_ID_OFFSET] = htons(instanceId);}
        void SetGrtt(UINT8 quantizedGrtt) 
            {((UINT8*)buffer)[GRTT_OFFSET] = quantizedGrtt;}
        void SetBackoffFactor(UINT8 backoff)
            {((UINT8*)buffer)[BACKOFF_OFFSET] = (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f) | (backoff << 4);}
        void SetGroupSize(UINT8 gsize) 
            {((UINT8*)buffer)[GSIZE_OFFSET] = (((UINT8*)buffer)[GSIZE_OFFSET] & 0xf0) | gsize;}
        void SetFlavor(NormCmdMsg::Flavor flavor)
            {((UINT8*)buffer)[FLAVOR_OFFSET] = (UINT8)flavor;}
        
        // Message parse
        UINT16 GetInstanceId() const 
            {return (ntohs(((UINT16*)buffer)[INSTANCE_ID_OFFSET]));}
        UINT8 GetGrtt() const 
            {return ((UINT8*)buffer)[GRTT_OFFSET];}
        UINT8 GetBackoffFactor() const 
            {return ((((UINT8*)buffer)[GSIZE_OFFSET] >> 4) & 0x0f);}
        UINT8 GetGroupSize() const 
            {return (((UINT8*)buffer)[GSIZE_OFFSET] & 0x0f);} 
        NormCmdMsg::Flavor GetFlavor() const 
            {return (Flavor)((UINT8*)buffer)[FLAVOR_OFFSET];} 
            
    protected:
        friend class NormMsg;
        enum
        {
            INSTANCE_ID_OFFSET   = MSG_OFFSET/2,
            GRTT_OFFSET          = (INSTANCE_ID_OFFSET+1)*2,
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
            // base header length depends on fec payload id
        }    
            
        void SetFecId(UINT8 fecId) 
            {((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;}
        void SetObjectId(const NormObjectId& objectId)
            {((UINT16*)buffer)[OBJ_ID_OFFSET] = htons((UINT16)objectId);}
        void SetFecPayloadId(UINT8 fecId, UINT32 blockId, UINT16 symbolId, UINT16 blockLen, UINT8 m)
        {
            SetFecId(fecId);
            SetBaseHeaderLength(4*FEC_PAYLOAD_ID_OFFSET + NormPayloadId::GetLength(fecId));
            NormPayloadId payloadId(fecId, m, buffer + FEC_PAYLOAD_ID_OFFSET);
            payloadId.SetFecPayloadId(blockId, symbolId, blockLen);
            ResetAckingNodeList();
        }
                
        void ResetAckingNodeList() 
            {length = header_length;}
        bool AppendAckingNode(NormNodeId nodeId, UINT16 segmentSize)
        {
            if ((length-header_length + 4) > segmentSize) return false;
            buffer[length/4] = htonl((UINT32)nodeId);
            length += 4;
            return true;
        }
        
        // Message processing
        UINT8 GetFecId() const
            {return ((UINT8*)buffer)[FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
            { return ntohs(((UINT16*)buffer)[OBJ_ID_OFFSET]);}
        NormBlockId GetFecBlockId(UINT8 m) const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockId();
        }        
        UINT16 GetFecSymbolId(UINT8 m)  const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecSymbolId();
        }    
        UINT16 GetFecBlockLength() const
        {
            NormPayloadId payloadId(GetFecId(), 8, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockLength();
        }    
        
        UINT16 GetAckingNodeCount() const 
            {return ((length - header_length) >> 2);}
        const UINT32* GetAckingNodeList() const 
            {return (buffer+(header_length/4));}
        NormNodeId GetAckingNodeId(UINT16 index) const 
            {return (ntohl(buffer[(header_length/4)+index]));}
            
    private:
        enum
        {
            FEC_ID_OFFSET           = FLAVOR_OFFSET + 1,     
            OBJ_ID_OFFSET           = (FEC_ID_OFFSET + 1)/2,     
            FEC_PAYLOAD_ID_OFFSET   = ((OBJ_ID_OFFSET+1)*2)/4
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
        void Init(UINT8 fecId)
        {
            SetType(CMD);
            SetFlavor(SQUELCH);
            SetFecId(fecId);  // default "fec_id"
            SetBaseHeaderLength(4*FEC_PAYLOAD_ID_OFFSET + NormPayloadId::GetLength(fecId));
        }    
        void SetFecId(UINT8 fecId) 
            {((UINT8*)buffer)[FEC_ID_OFFSET] = fecId;}
        void SetObjectId(const NormObjectId& objectId)
            {((UINT16*)buffer)[OBJ_ID_OFFSET] = htons((UINT16)objectId);}
        void SetFecPayloadId(UINT8 fecId, UINT32 blockId, UINT16 symbolId, UINT16 blockLen, UINT8 m)
        {
            SetFecId(fecId);
            SetBaseHeaderLength(4*FEC_PAYLOAD_ID_OFFSET + NormPayloadId::GetLength(fecId));
            NormPayloadId payloadId(fecId, m, buffer + FEC_PAYLOAD_ID_OFFSET);
            payloadId.SetFecPayloadId(blockId, symbolId, blockLen);
            ResetInvalidObjectList();
        }
        void ResetInvalidObjectList() 
            {length = header_length;}
        
        // Note must apply any header extensions _before_ appending payload.
        bool AppendInvalidObject(NormObjectId objectId, UINT16 segmentSize)
        {
            if ((length-header_length+2) > segmentSize) return false;
            ((UINT16*)buffer)[length/2] = htons((UINT16)objectId);
            length += 2;
            return true;   
        }
        
        // Message processing
        UINT8 GetFecId() const
            {return ((UINT8*)buffer)[FEC_ID_OFFSET];}
        NormObjectId GetObjectId() const
            {return (ntohs(((UINT16*)buffer)[OBJ_ID_OFFSET]));}
        NormBlockId GetFecBlockId(UINT8 m) const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockId();
        }        
        UINT16 GetFecSymbolId(UINT8 m)  const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecSymbolId();
        }    
        UINT16 GetFecBlockLength() const
        {
            NormPayloadId payloadId(GetFecId(), 8, buffer + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockLength();
        }
        
        // Use these to parse invalid object list
        UINT16 GetInvalidObjectCount() const 
            {return ((length - header_length) >> 1);} 
        UINT16* GetInvalidObjectList() const 
            {return (UINT16*)(buffer+header_length);}
        NormObjectId GetInvalidObjectId(UINT16 index) const
            {return (ntohs(((UINT16*)buffer)[(header_length/2)+index]));}
        
    private:
        enum
        {
            FEC_ID_OFFSET           = FLAVOR_OFFSET + 1,      
            OBJ_ID_OFFSET           = (FEC_ID_OFFSET + 1)/2,      
            FEC_PAYLOAD_ID_OFFSET   = ((OBJ_ID_OFFSET+1)*2)/4
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
            LEAVE = 0x10,
            LIMIT = 0x20  // experimental, non-RFC5740 
        };                // (set when included rate is measured, not calculated)
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
            {return (ntohs(((UINT16*)buffer)[CC_SEQUENCE_OFFSET]));}
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
        
        // This function uses the "reserved" field of the NORM_CMD(CC) message
        // and is not strictly compliant with RFC 5740 when invoked.
        enum {FLAG_SYN = 0x01};
        bool SynIsSet() const
                {return (0 != (FLAG_SYN & ((UINT8*)buffer)[RESERVED_OFFSET]));} 
        void SetSyn()
            {((UINT8*)buffer)[RESERVED_OFFSET] = FLAG_SYN;}
        
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
                        
};  // end class NormCmdCCMsg

class NormCCRateExtension : public NormHeaderExtension
{
    public:
            
        virtual void Init(UINT32* theBuffer, UINT16 numBytes)
        {
            AttachBuffer(theBuffer, numBytes);
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
        static UINT16 RepairItemLength(UINT8 fecId)  
            {return (4 + NormPayloadId::GetLength(fecId));}
        static UINT16 RepairRangeLength(UINT8 fecId)
            {return (2 * RepairItemLength(fecId));}
        static UINT16 ErasureItemLength(UINT8 fecId) 
            {return RepairItemLength(fecId);}
        
        // Repair request building
        void SetForm(NormRepairRequest::Form theForm) 
            {form = theForm;}
        void ResetFlags() 
            {flags = 0;}
        void SetFlag(NormRepairRequest::Flag theFlag) 
            {flags |= theFlag;} 
        void ClearFlag(NormRepairRequest::Flag theFlag) 
            {flags &= ~theFlag;}    
        void SetFlags(int theFlags)
            {flags = theFlags;}   
        
        // Returns length (each repair item requires 8 bytes of space)
        bool AppendRepairItem(UINT8               fecId,
                              UINT8               fecM,
                              const NormObjectId& objectId, 
                              const NormBlockId&  blockId,
                              UINT16              blockLen,
                              UINT16              symbolId);
        
        bool AppendRepairRange(UINT8               fecId,
                               UINT8               fecM,
                               const NormObjectId& startObjectId, 
                               const NormBlockId&  startBlockId,
                               UINT16              startBlockLen,
                               UINT16              startSymbolId,
                               const NormObjectId& endObjectId, 
                               const NormBlockId&  endBlockId,
                               UINT16              endBlockLen,
                               UINT16              endSymbolId);  
        
        bool AppendErasureCount(UINT8               fecId,
                                UINT8               fecM,
                                const NormObjectId& objectId, 
                                const NormBlockId&  blockId,
                                UINT16              blockLen,
                                UINT16              erasureCount);     
        
        UINT16 Pack();       
        
        // Repair request processing
        UINT16 Unpack(const UINT32* bufferPtr, UINT16 bufferLen);
        NormRepairRequest::Form GetForm() const 
            {return form;}
        bool FlagIsSet(NormRepairRequest::Flag theFlag) const
            {return (0 != (theFlag & flags));}
        int GetFlags() const
            {return flags;}
        UINT16 GetLength() const 
            {return (ITEM_LIST_OFFSET + length);}
        
        UINT32* GetBuffer() const
            {return buffer;}
        
        // Outputs textual representation of RepairRequest content
        void Log(UINT8 fecId, UINT8 fecM) const;
        
        class Iterator
        {
            public:
                 // Checks for matching fecId and assumes constant 'm' ?!?!
                Iterator(const NormRepairRequest& theRequest, UINT8 fecId, UINT8 fecM);  
                void Reset() {offset = 0;}
                UINT16 NextRepairItem(NormObjectId* objectId,
                                      NormBlockId*  blockId,
                                      UINT16*       blockLen,
                                      UINT16*       symbolId);
            private:
                const NormRepairRequest& request;
                UINT8                    fec_id;
                UINT8                    fec_m;
                UINT16                   offset;    
        };  // end class NormRepairRequest::Iterator
          
    private:
        UINT16 RetrieveRepairItem(UINT8           fecM,
                                  UINT16          offset,
                                  UINT8*          fecId,
                                  NormObjectId*   objectId, 
                                  NormBlockId*    blockId,
                                  UINT16*         blockLen,
                                  UINT16*         symbolId) const;
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
            FEC_PAYLOAD_ID_OFFSET = ((OBJ_ID_OFFSET+1)*2)/4
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
            {return (0 != ((UINT8)flag | ((UINT8*)buffer)[FLAGS_OFFSET]));}
        //char* AccessRepairContent() {return (buffer + header_length);}
        const UINT32* GetRepairContent() const 
            {return (buffer + header_length/4);}
        UINT16 GetRepairContentLength() const 
            {return (length - header_length);}
                  
    private:
        enum
        {
            FLAGS_OFFSET            = FLAVOR_OFFSET + 1,
            RESERVED_OFFSET         = FLAGS_OFFSET + 1,
            REPAIR_ADV_HEADER_LEN   = RESERVED_OFFSET + 2
        };    
        
};  // end class NormCmdRepairAdvMsg

// TBD - define NormCCFeedbackExtension2 for larger loss encoding range
//       the current 16-bit range is too limited for large RTT*rate combos
class NormCCFeedbackExtension : public NormHeaderExtension
{
    public:
        virtual void Init(UINT32* theBuffer, UINT16 numBytes)
        {
            AttachBuffer(theBuffer, numBytes);
            SetType(CC_FEEDBACK);
            SetWords(3);
            ((UINT8*)buffer)[CC_FLAGS_OFFSET] = 0;
            //((UINT16*)buffer)[CC_RESERVED_OFFSET] = 0;
        }
        void SetCCSequence(UINT16 ccSequence)
            {((UINT16*)buffer)[CC_SEQUENCE_OFFSET] = htons(ccSequence);}
        void ResetCCFlags() 
            {((UINT8*)buffer)[CC_FLAGS_OFFSET] = 0;}
        void SetCCFlag(NormCC::Flag flag) 
            {((UINT8*)buffer)[CC_FLAGS_OFFSET] |= (UINT8)flag;}
        void SetCCRtt(UINT8 ccRtt) 
            {((UINT8*)buffer)[CC_RTT_OFFSET] = ccRtt;}
        //void SetCCLoss(UINT16 ccLoss)
        //    {((UINT16*)buffer)[CC_LOSS_OFFSET] = htons(ccLoss);}
        void SetCCRate(UINT16 ccRate) 
            {((UINT16*)buffer)[CC_RATE_OFFSET] = htons(ccRate);}
        
        void SetCCLoss32(UINT32 ccLoss)
        {
            ccLoss = htonl(ccLoss);
            UINT16* ptr = (UINT16*)&ccLoss;
            ((UINT16*)buffer)[CC_LOSS_OFFSET] = ptr[0];    // msb
            ((UINT16*)buffer)[CC_LOSS_EX_OFFSET] = ptr[1]; // lsb
        }
        
        UINT16 GetCCSequence() const 
            {return (ntohs(((UINT16*)buffer)[CC_SEQUENCE_OFFSET]));}
        UINT8 GetCCFlags() 
            {return ((UINT8*)buffer)[CC_FLAGS_OFFSET];}
        bool CCFlagIsSet(NormCC::Flag flag) const
            {return (0 != ((UINT8)flag & ((UINT8*)buffer)[CC_FLAGS_OFFSET]));}
        UINT8 GetCCRtt() const
            {return ((UINT8*)buffer)[CC_RTT_OFFSET];}
        //UINT16 GetCCLoss() const
        //    {return (ntohs(((UINT16*)buffer)[CC_LOSS_OFFSET]));} 
        UINT16 GetCCRate()const
            {return (ntohs(((UINT16*)buffer)[CC_RATE_OFFSET]));} 
        
        UINT32 GetCCLoss32() const
        {
            UINT32 lossQuantized;
            UINT16* ptr = (UINT16*)&lossQuantized;
            ptr[0] = ((UINT16*)buffer)[CC_LOSS_OFFSET];    // msb
            ptr[1] = ((UINT16*)buffer)[CC_LOSS_EX_OFFSET]; // lsb
            return ntohl(lossQuantized);  // return in host byte order 
        }
            
    private:
        enum
        {
            CC_SEQUENCE_OFFSET  = (LENGTH_OFFSET+1)/2,
            CC_FLAGS_OFFSET     = (CC_SEQUENCE_OFFSET*2)+2,
            CC_RTT_OFFSET       = CC_FLAGS_OFFSET + 1,
            CC_LOSS_OFFSET      = (CC_RTT_OFFSET + 1)/2,
            CC_RATE_OFFSET      = ((CC_LOSS_OFFSET*2)+2)/2,
            //CC_RESERVED_OFFSET  = ((CC_RATE_OFFSET*2)+2)/2
            CC_LOSS_EX_OFFSET  = ((CC_RATE_OFFSET*2)+2)/2  // extended precision loss estimate (non-RFC5940 compliant, but compatible)
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
        void SetAckType(NormAck::Type ackType) 
            {((UINT8*)buffer)[ACK_TYPE_OFFSET] = (UINT8)ackType;}
        void SetAckId(UINT8 ackId) 
            {((UINT8*)buffer)[ACK_ID_OFFSET] = ackId;}       
        void ResetAckingNodeList() 
            {length = header_length;}
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
            {return (ntohl(buffer[(header_length/4)+index]));}
            
    private:
        enum 
        {
            RESERVED_OFFSET    = FLAVOR_OFFSET + 1,
            ACK_TYPE_OFFSET    = RESERVED_OFFSET + 1,
            ACK_ID_OFFSET      = ACK_TYPE_OFFSET + 1,
            ACK_REQ_HEADER_LEN = ACK_ID_OFFSET + 1
        };       
};  // end class NormCmdAckReqMsg


class NormCmdAppMsg : public NormCmdMsg
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
            length = header_length + len;
            return (contentLen <= segmentSize);
        }
        
        UINT16 GetContentLength() const 
            {return (length - header_length);}
        const char* GetContent() const 
            {return (((char*)buffer)+header_length);}
            
    private:
        enum 
        {
            RESERVED_OFFSET         = FLAVOR_OFFSET + 1,
            APPLICATION_HEADER_LEN  = RESERVED_OFFSET + 3
        };
};  // end class NormCmdAppMsg
 

// Receiver Messages

class NormNackMsg : public NormMsg
{
    public:
        enum {DEFAULT_LENGTH_MAX = 40};
        void Init()
        {
            SetType(NACK);
            ((UINT16*)buffer)[RESERVED_OFFSET] = 0;
            SetBaseHeaderLength(NACK_HEADER_LEN);
        }
        // Message building
        void SetSenderId(NormNodeId senderId)
            {buffer[SENDER_ID_OFFSET] = htonl(senderId);}
        void SetInstanceId(UINT16 instanceId)
            {((UINT16*)buffer)[INSTANCE_ID_OFFSET] = htons(instanceId);}
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
        
        // TBD - add some safety checks to these methods 
        void InitFrom(NormNackMsg nack)
        {
            // Copy header from "nack"
            memcpy(buffer, nack.buffer, nack.GetHeaderLength());
            header_length_base = nack.header_length_base;
            length = header_length = nack.GetHeaderLength();
        }
        void AppendRepairRequest(const NormRepairRequest request)
        {
            memcpy(buffer+length/4, request.GetBuffer(), request.GetLength());
            length += request.GetLength();
        }
        void ResetPayload()
            {length = GetHeaderLength();}
                        
        // Message processing 
        NormNodeId GetSenderId() const
            {return (ntohl(buffer[SENDER_ID_OFFSET]));}
        UINT16 GetInstanceId() const
            {return (ntohs(((UINT16*)buffer)[INSTANCE_ID_OFFSET]));}
        void GetGrttResponse(struct timeval& grttResponse) const
        {
            grttResponse.tv_sec = ntohl(buffer[GRTT_RESPONSE_SEC_OFFSET]);
            grttResponse.tv_usec = ntohl(buffer[GRTT_RESPONSE_USEC_OFFSET]);
        }      
        //char* AccessRepairContent() {return (buffer + header_length);}
        const UINT32* GetRepairContent() const 
            {return (buffer + header_length/4);}
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
            SENDER_ID_OFFSET          = MSG_OFFSET/4,
            INSTANCE_ID_OFFSET        = ((SENDER_ID_OFFSET*4)+4)/2,
            RESERVED_OFFSET           = ((INSTANCE_ID_OFFSET*2)+2)/2,
            GRTT_RESPONSE_SEC_OFFSET  = ((RESERVED_OFFSET*2)+2)/4,
            GRTT_RESPONSE_USEC_OFFSET = ((GRTT_RESPONSE_SEC_OFFSET*4)+4)/4,
            NACK_HEADER_LEN           = (GRTT_RESPONSE_USEC_OFFSET*4)+4
        };    
};  // end class NormNackMsg

class NormAckMsg : public NormAck, public NormMsg
{
    public:
        
        // Message building
        void Init()
        {
            SetType(ACK);
            SetBaseHeaderLength(ACK_HEADER_LEN);
            SetAckType(NormAck::INVALID);
        }
        void SetSenderId(NormNodeId senderId)
            {buffer[SENDER_ID_OFFSET] = htonl(senderId);}
        void SetInstanceId(UINT16 instanceId)
            {((UINT16*)buffer)[INSTANCE_ID_OFFSET] = htons(instanceId);}
        void SetAckType(NormAck::Type ackType) 
            {((UINT8*)buffer)[ACK_TYPE_OFFSET] = (UINT8)ackType;}
        void SetAckId(UINT8 ackId) 
            {((UINT8*)buffer)[ACK_ID_OFFSET] = ackId;}
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
        NormNodeId GetSenderId() const
            {return (ntohl(buffer[SENDER_ID_OFFSET]));}
        UINT16 GetInstanceId() const
            {return (ntohs(((UINT16*)buffer)[INSTANCE_ID_OFFSET]));}
        void GetGrttResponse(struct timeval& grttResponse) const
        {
            grttResponse.tv_sec = ntohl(buffer[GRTT_RESPONSE_SEC_OFFSET]);
            grttResponse.tv_usec = ntohl(buffer[GRTT_RESPONSE_USEC_OFFSET]);
        }             
        NormAck::Type GetAckType() const 
            {return (NormAck::Type)((UINT8*)buffer)[ACK_TYPE_OFFSET];}
        UINT8 GetAckId() const 
            {return ((UINT8*)buffer)[ACK_ID_OFFSET];}
        UINT16 GetPayloadLength() const 
            {return (length - header_length);}
        const char* GetPayload() const 
            {return (((char*)buffer) + header_length);}
        
    protected:
        enum
        {
            SENDER_ID_OFFSET          = MSG_OFFSET/4,
            INSTANCE_ID_OFFSET        = ((SENDER_ID_OFFSET*4)+4)/2,
            ACK_TYPE_OFFSET           = (INSTANCE_ID_OFFSET*2)+2,
            ACK_ID_OFFSET             = ACK_TYPE_OFFSET + 1,
            GRTT_RESPONSE_SEC_OFFSET  = (ACK_ID_OFFSET + 1)/4,
            GRTT_RESPONSE_USEC_OFFSET = ((GRTT_RESPONSE_SEC_OFFSET+1)*4)/4,
            ACK_HEADER_LEN            = (GRTT_RESPONSE_USEC_OFFSET+1)*4
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
            ((UINT8*)buffer)[RESERVED_OFFSET] = 0;
        }
        
        // Note: must apply any header exts _before_ the payload is set
        void SetFecId(UINT8 fecId) 
            {((UINT8*)buffer)[header_length+FEC_ID_OFFSET] = fecId;}
        void SetObjectId(NormObjectId objectId)
            {((UINT16*)buffer)[(header_length/2)+OBJ_ID_OFFSET] = htons((UINT16)objectId);}
        void SetFecPayloadId(UINT8 fecId, UINT32 blockId, UINT16 symbolId, UINT16 blockLen, UINT8 m)
        {
            SetFecId(fecId);
            ((UINT8*)buffer)[header_length+RESERVED_OFFSET] = 0;
            NormPayloadId payloadId(fecId, m, buffer + header_length/4 + FEC_PAYLOAD_ID_OFFSET);
            payloadId.SetFecPayloadId(blockId, symbolId, blockLen);
            length = header_length + 4*FEC_PAYLOAD_ID_OFFSET + NormPayloadId::GetLength(fecId);
        }
        
        UINT8 GetFecId() const
            {return ((UINT8*)buffer)[header_length+FEC_ID_OFFSET];}
        
        NormObjectId GetObjectId() const
            {return ntohs(((UINT16*)buffer)[(header_length/2)+OBJ_ID_OFFSET]);}
        
        NormBlockId GetFecBlockId(UINT8 m) const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + header_length/4 + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockId();
        }    
        UINT16 GetFecSymbolId(UINT8 m)  const
        {
            NormPayloadId payloadId(GetFecId(), m, buffer + header_length/4 + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecSymbolId();
        }   
        UINT16 GetFecBlockLength() const
        {
            NormPayloadId payloadId(GetFecId(), 8, buffer + header_length/4 + FEC_PAYLOAD_ID_OFFSET);
            return payloadId.GetFecBlockLength();
        }
        
    private:
        // Note - These are the payload offsets for "fec_id" = 129 
        // "fec_payload_id" field
        // IMPORTANT - These are _relative_ to the NORM_ACK header (incl. extensions)
        enum
        {
            FEC_ID_OFFSET           = 0,
            RESERVED_OFFSET         = FEC_ID_OFFSET + 1,
            OBJ_ID_OFFSET           = (RESERVED_OFFSET+1)/2,
            FEC_PAYLOAD_ID_OFFSET   = ((OBJ_ID_OFFSET*2)+2)/4
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
        NormMsg* GetHead() {return head;}
        bool IsEmpty() {return ((NormMsg*)NULL == head);}
    
    private:
        NormMsg*    head;
        NormMsg*    tail;
};  // end class NormMessageQueue

// Helper function to output report on repair content (e.g. NormNack content) to debug log
void LogRepairContent(const UINT32* buffer, UINT16 bufferLen, UINT8 fecId, UINT8 fecM);

#endif // _NORM_MESSAGE
