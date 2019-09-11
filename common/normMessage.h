#ifndef _NORM_MESSAGE
#define _NORM_MESSAGE

// PROTOLIB includes
#include "networkAddress.h" // for NetworkAddress class
#include "sysdefs.h"        // for UINT typedefs
#include "debug.h"

// standard includes
#include <string.h>  // for memcpy(), etc
#include <math.h>

// (TBD) Alot of the "memcpy()" calls could be eliminated by
//       taking advantage of the alignment of NORM messsages

const UINT16 NORM_MSG_SIZE_MAX = 8192;
const int NORM_ROBUST_FACTOR = 20;  // default robust factor

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
			(((double)(qrtt+1))/(double)NORM_RTT_MIN) :
		    (NORM_RTT_MAX/exp(((double)(255-qrtt))/(double)13.0)));
}
unsigned char NormQuantizeRtt(double rtt);

inline double NormUnquantizeGroupSize(unsigned char gsize)
{
    return ((double)(gsize >> 4) * pow(10.0, (double)(gsize & 0x0f)));   
}
inline unsigned char NormQuantizeGroupSize(double gsize)
{
    unsigned char exponent = (unsigned char)log10(gsize);
    return ((((unsigned char)(gsize / pow(10.0, (double)exponent) + 0.5)) << 4)
            | exponent);
}

// This class is used to describe object "size" and/or "offset"
class NormObjectSize
{
    public:
        NormObjectSize() : msb(0), lsb(0) {}
        NormObjectSize(UINT16 upper, UINT32 lower) : msb(upper), lsb(lower) {}
        NormObjectSize(UINT32 size) : msb(0), lsb(size) {}
        NormObjectSize(UINT16 size) : msb(0), lsb(size) {}
        NormObjectSize(const NormObjectSize& size) : msb(size.msb), lsb(size.lsb) {}
        
        UINT16 MSB() const {return msb;}
        UINT32 LSB() const {return lsb;}
        
        bool operator<(const NormObjectSize& size) const;
        bool operator>(const NormObjectSize& size) const;
        bool operator==(const NormObjectSize& size) const
            {return ((msb == size.msb) && (lsb == size.lsb));}
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
        
    private:
        UINT16  msb;
        UINT32  lsb;  
};  // end class NormObjectSize

typedef UINT32 NormNodeId;

class NormObjectId
{
    public:
        NormObjectId() {};
        NormObjectId(UINT16 id) {value = id;}
        NormObjectId(const NormObjectId& id) {value = id.value;}
        operator UINT16() const {return value;}
        bool operator<(const NormObjectId& id) const;
        bool operator>(const NormObjectId& id) const;
        bool operator==(const NormObjectId& id) const
            {return (value == id.value);}
        bool operator!=(const NormObjectId& id) const
            {return (value != id.value);}
        int operator-(const NormObjectId& id) const;
        NormObjectId& operator++(int ) {value++; return *this;}
        
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
        bool operator<(const NormBlockId& id) const;
        bool operator>(const NormBlockId& id) const;
        bool operator==(const NormBlockId& id) const
            {return (value == (UINT32)id);}
        bool operator!=(const NormBlockId& id) const
            {return (value != (UINT32)id);}
        long operator-(const NormBlockId& id) const;
        NormBlockId& operator++(int ) {value++; return *this;}
        
    private:
        UINT32  value;  
};  // end class NormObjectId

typedef UINT16 NormSymbolId;
typedef NormSymbolId NormSegmentId;

const NormNodeId NORM_NODE_INVALID = 0x0;
const NormNodeId NORM_NODE_ANY = 0xffffffff;

enum NormMsgType 
{
    NORM_MSG_INVALID,
    NORM_MSG_INFO, 
    NORM_MSG_DATA, 
    NORM_MSG_CMD,
    NORM_MSG_NACK,
    NORM_MSG_ACK,
    NORM_MSG_REPORT
};
    
class NormMsg
{
    public:
        // Message building routines
        void SetVersion(UINT8 version) {buffer[VERSION_OFFSET] = version;}
        void SetType(NormMsgType type) {buffer[TYPE_OFFSET] = type;}
        void SetSequence(UINT16 sequence) 
        {
            UINT16 temp16 = htons(sequence);
            memcpy(buffer+SEQUENCE_OFFSET, &temp16, 2);
        }   
        void SetSender(NormNodeId sender)
        {
            UINT32 temp32 = htonl(sender);
            memcpy(buffer+SENDER_OFFSET, &temp32, 4);   
        } 
        void SetDestination(const NetworkAddress& dst) {addr = dst;}
        void SetLength(UINT16 len) {length = len;}
        
        // Message processing routines
        UINT8 GetVersion() const {return buffer[VERSION_OFFSET];}
        NormMsgType GetType() const {return (NormMsgType)buffer[TYPE_OFFSET];}
        UINT16 GetSequence() const
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+SEQUENCE_OFFSET, 2);
            return (ntohs(temp16));   
        }
        NormNodeId GetSender() const
        {
            UINT32 temp32;
            memcpy(&temp32, buffer+SENDER_OFFSET, 4);
            return (ntohl(temp32));    
        }
        const NetworkAddress& GetDestination() {return addr;}
        const NetworkAddress& GetSource() {return addr;}
        UINT16 GetLength() {return length;}
                
        // For message buffer transmission/reception and misc.
        char* AccessBuffer() {return buffer;}                
        UINT16 AccessLength() const {return length;}
        NetworkAddress& AccessAddress() {return addr;}
        
        
    protected:
        // Common message header offsets
        enum
        {
            VERSION_OFFSET      = 0,
            TYPE_OFFSET         = VERSION_OFFSET + 1,
            SEQUENCE_OFFSET     = TYPE_OFFSET + 1,
            SENDER_OFFSET       = SEQUENCE_OFFSET + 2,
            MSG_OFFSET          = SENDER_OFFSET + 4  
        };    
        char            buffer[NORM_MSG_SIZE_MAX]; 
        UINT16          length; 
        NetworkAddress  addr;  // src/dst (default dst is session address)
};  // end class NormMsg

// "NormObjectMsg" are comprised of NORM_INFO and NORM_DATA messages
// (NormInfoMsg and NormDataMsg will derive from this type)

class NormObjectMsg : public NormMsg
{
    public:
        enum Flag
        { 
            FLAG_REPAIR     = 0x01,
            FLAG_EXPLICIT   = 0x02,
            FLAG_INFO       = 0x04,
            FLAG_UNRELIABLE = 0x08,
            FLAG_FILE       = 0x10,
            FLAG_STREAM     = 0x20
        }; 
        bool FlagIsSet(NormObjectMsg::Flag flag) const
            {return (0 != (flag & buffer[FLAGS_OFFSET]));}
        UINT8 GetGrtt() const {return buffer[GRTT_OFFSET];} 
        UINT8 GetGroupSize() const {return buffer[GSIZE_OFFSET];} 
        UINT8 GetFecId() const {return buffer[FEC_ID_OFFSET];}
        UINT16 GetSegmentSize() const 
        {
            UINT16 segmentSize;
            memcpy(&segmentSize, buffer+SEG_SIZE_OFFSET, 2);
            return (ntohs(segmentSize));   
        }
        NormObjectSize GetObjectSize() const
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+OBJ_SIZE_OFFSET, 2);
            UINT32 temp32;
            memcpy(&temp32, buffer+OBJ_SIZE_OFFSET+2, 4);
            return NormObjectSize(ntohs(temp16), ntohl(temp32));   
        }
        UINT16 GetFecEncodingName() const
        {
            UINT16 encodingName;
            memcpy(&encodingName, buffer+FEC_NAME_OFFSET, 2);
            return (ntohs(encodingName)); 
        }
        UINT16 GetFecNumParity() const
        {
            UINT16 nparity;
            memcpy(&nparity, buffer+FEC_NPARITY_OFFSET, 2);
            return (ntohs(nparity)); 
        }   
        UINT16 GetFecBlockLen() const
        {
            UINT16 ndata;
            memcpy(&ndata, buffer+FEC_NDATA_OFFSET, 2);
            return (ntohs(ndata)); 
        } 
        NormObjectId GetObjectId() const
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+OBJ_ID_OFFSET, 2);
            return (ntohs(temp16));   
        }
        
        // Message building routines
        void ResetFlags() {buffer[FLAGS_OFFSET] = 0;}
        void SetFlag(NormObjectMsg::Flag flag)
            {buffer[FLAGS_OFFSET] |= flag;}
        void SetGrtt(UINT8 grtt) {buffer[GRTT_OFFSET] = grtt;}
        void SetGroupSize(UINT8 gsize) {buffer[GSIZE_OFFSET] = gsize;}
        void SetFecId(UINT8 fecId) {buffer[FEC_ID_OFFSET] = fecId;}
        void SetSegmentSize(UINT16 segmentSize)
        {
            segmentSize = htons(segmentSize);
            memcpy(buffer+SEG_SIZE_OFFSET, &segmentSize, 2);
        }
        void SetObjectSize(const NormObjectSize& objectSize)
        {
            UINT16 temp16 = htons(objectSize.MSB());
            memcpy(buffer+OBJ_SIZE_OFFSET, &temp16, 2);
            UINT32 temp32 = htonl(objectSize.LSB());
            memcpy(buffer+OBJ_SIZE_OFFSET+2, &temp32, 4);
        }
        void SetFecEncodingName(UINT16 encodingName)
        {
            encodingName = htons(encodingName);
            memcpy(buffer+FEC_NAME_OFFSET, &encodingName, 2);
        }
        void SetFecNumParity(UINT16 nparity)
        {
            nparity = htons(nparity);
            memcpy(buffer+FEC_NPARITY_OFFSET, &nparity, 2);
        }
        void SetFecBlockLen(UINT16 blockLen)
        {
            blockLen = htons(blockLen);
            memcpy(buffer+FEC_NDATA_OFFSET, &blockLen, 2);
        }
        void SetObjectId(const NormObjectId& objectId)
        {
            UINT16 temp16 = htons((UINT16)objectId);
            memcpy(buffer+OBJ_ID_OFFSET, &temp16, 2);
        }
        
    protected: 
        enum
        {
            FLAGS_OFFSET        = MSG_OFFSET,
            GRTT_OFFSET         = FLAGS_OFFSET + 1,
            GSIZE_OFFSET        = GRTT_OFFSET + 1,
            FEC_ID_OFFSET       = GSIZE_OFFSET + 1,
            SEG_SIZE_OFFSET     = FEC_ID_OFFSET + 1,
            OBJ_SIZE_OFFSET     = SEG_SIZE_OFFSET + 2,
            RESV_OFFSET         = OBJ_SIZE_OFFSET + 6,
            FEC_NAME_OFFSET     = RESV_OFFSET + 2,
            FEC_NPARITY_OFFSET  = FEC_NAME_OFFSET + 2,
            FEC_NDATA_OFFSET    = FEC_NPARITY_OFFSET + 2,
            OBJ_ID_OFFSET       = FEC_NDATA_OFFSET + 2
        };          
};  // end class NormObjectMsg

class NormInfoMsg : public NormObjectMsg
{
    public:
        UINT16 GetInfoLen() const {return (length - INFO_OFFSET);}
        const char* GetInfo() const {return (buffer + INFO_OFFSET);}
        void SetInfo(char* data, UINT16 len)
        {
            memcpy(buffer+INFO_OFFSET, data, len);
            length = len + INFO_OFFSET;
        }
             
    private:    
        enum
        {
            INFO_OFFSET         = OBJ_ID_OFFSET + 2
        };
};  // end class NormInfoMsg

class NormDataMsg : public NormObjectMsg
{
    public:
        // Message building methods
        void SetFecBlockId(const NormBlockId& blockId)
        {
            UINT32 temp32 = htonl((UINT32)blockId);
            memcpy(buffer+BLOCK_ID_OFFSET, &temp32, 4);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            symbolId = htons(symbolId);
            memcpy(buffer+SYMBOL_ID_OFFSET, &symbolId, 2);
        }
        char* AccessData() {return (buffer+DATA_OFFSET);}
        void SetData(const NormObjectSize& offset, char* data, UINT16 len)
        {
            WriteLength(buffer+LENGTH_OFFSET, len);
            WriteOffset(buffer+LENGTH_OFFSET, offset);
            memcpy(buffer+DATA_OFFSET, data, len);
            length = DATA_OFFSET + len;
        }        
        void SetDataOffset(const NormObjectSize& offset)
        {
            WriteOffset(buffer+LENGTH_OFFSET, offset);
        }
        void SetDataLength(UINT16 len)
        {
            WriteLength(buffer+LENGTH_OFFSET, len);
            length = DATA_OFFSET + len; 
        }
        // (Note: "payload_len" and "offset" field spaces are in the FEC payload)
        char* AccessPayload() {return (buffer+PAYLOAD_OFFSET);}
        void SetPayload(const char* data, UINT16 len)
        {
            memcpy(buffer+PAYLOAD_OFFSET, data, len);
            length = PAYLOAD_OFFSET + len;
        }
               
        // Message processing methods
        NormBlockId GetFecBlockId() const
        {
            UINT32 temp32;
            memcpy(&temp32, buffer+BLOCK_ID_OFFSET, 4);
            return (ntohl(temp32));   
        }
        UINT16 GetFecSymbolId()  const
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+SYMBOL_ID_OFFSET, 2);
            return (ntohs(temp16));   
        }
        bool IsData() const {return (GetFecSymbolId() < GetFecBlockLen());}
        const char* GetData() {return (buffer + DATA_OFFSET);}
        UINT16 GetDataLength() const {return (length - DATA_OFFSET);}
        
        const char* GetPayload() {return (buffer+LENGTH_OFFSET);}
        UINT16 GetPayloadLength() const {return (length - LENGTH_OFFSET);}
        
        bool IsParity() const {return (GetFecSymbolId() >= GetFecBlockLen());}
        // (Note: "payload_len" and "offset" field spaces are in the FEC payload)
        char* GetParity() {return buffer+LENGTH_OFFSET;}
        // (Note: This should be the same as "segment_size")
        UINT16 GetParityLen() const 
                {return (length - LENGTH_OFFSET);}   
        
        // Some static helper routines for various purposes
        static UINT16 PayloadHeaderLen() {return (DATA_OFFSET - LENGTH_OFFSET);}
        static void WriteLength(char* payload, UINT16 len)
        {
            UINT16 temp16 = htons(len);
            memcpy(payload+LENGTH_OFFSET-LENGTH_OFFSET, &temp16, 2); 
        }
        static void WriteOffset(char* payload, const NormObjectSize& offset)
        {
            UINT16 temp16 = htons(offset.MSB());
            memcpy(payload+OFFSET_OFFSET-LENGTH_OFFSET, &temp16, 2);
            UINT32 temp32 = htonl(offset.LSB());
            memcpy(payload+OFFSET_OFFSET-LENGTH_OFFSET+2, &temp32, 4);
        }
        static UINT16 ReadLength(const char* payload)
        {
            UINT16 temp16;
            memcpy(&temp16, payload+LENGTH_OFFSET-LENGTH_OFFSET, 2);
            return ntohs(temp16);   
        }
        static NormObjectSize ReadOffset(const char* payload)
        {
            UINT16 temp16;
            memcpy(&temp16, payload+OFFSET_OFFSET-LENGTH_OFFSET, 2);
            UINT32 temp32;
            memcpy(&temp32, payload+OFFSET_OFFSET-LENGTH_OFFSET+2, 4);
            return NormObjectSize(ntohs(temp16), ntohl(temp32));  
        }
          
    private:    
        enum
        {
            BLOCK_ID_OFFSET     = OBJ_ID_OFFSET + 2,
            SYMBOL_ID_OFFSET    = BLOCK_ID_OFFSET + 4,
            PAYLOAD_OFFSET      = SYMBOL_ID_OFFSET + 2,
            LENGTH_OFFSET       = PAYLOAD_OFFSET,
            OFFSET_OFFSET       = LENGTH_OFFSET + 2,
            DATA_OFFSET         = OFFSET_OFFSET + 6
        };
};  // end class NormDataMsg


class NormCmdMsg : public NormMsg
{
    public:
        enum Flavor
        {
            NORM_CMD_INVALID,
            NORM_CMD_FLUSH,
            NORM_CMD_SQUELCH,
            NORM_CMD_ACK_REQ,
            NORM_CMD_REPAIR_ADV,
            NORM_CMD_CC,
            NORM_CMD_APPLICATION
        };
        
        // Message building
        void SetGrtt(UINT8 quantizedGrtt) 
            {buffer[GRTT_OFFSET] = quantizedGrtt;}
        void SetGroupSize(UINT8 quantizedGroupSize)
            {buffer[GSIZE_OFFSET] = quantizedGroupSize;}
        void SetFlavor(NormCmdMsg::Flavor flavor)
            {buffer[FLAVOR_OFFSET] = flavor;}
        
        // Message processing
        UINT8 GetGrtt() const {return buffer[GRTT_OFFSET];}
        UINT8 GetGroupSize() const {return buffer[GSIZE_OFFSET];}
        NormCmdMsg::Flavor GetFlavor() const {return (Flavor)buffer[FLAVOR_OFFSET];} 
            
    protected:
        enum
        {
            GRTT_OFFSET          = MSG_OFFSET,
            GSIZE_OFFSET         = GRTT_OFFSET + 1,
            FLAVOR_OFFSET        = GSIZE_OFFSET + 1
        }; 
};  // end class NormCmdMsg

class NormCmdFlushMsg : public NormCmdMsg
{
    public:
        enum Flag {NORM_FLUSH_FLAG_EOT = 0x01};
    
        // Message building
        void Reset() 
        {
            buffer[FLAGS_OFFSET] = 0;
            length = SYMBOL_ID_OFFSET + 2;
        }
        void SetFlag(NormCmdFlushMsg::Flag flag)
            {buffer[FLAGS_OFFSET] |= flag;}
        void UnsetFlag(NormCmdFlushMsg::Flag flag)
            {buffer[FLAGS_OFFSET] &= ~flag;}
        void SetObjectId(const NormObjectId& objectId)
        {
            UINT16 temp16 = htons((UINT16)objectId);
            memcpy(buffer+OBJECT_ID_OFFSET, &temp16, 2);
        }
        void SetFecBlockId(const NormBlockId& blockId)
        {
            UINT32 temp32 = htonl((UINT32)blockId);
            memcpy(buffer+BLOCK_ID_OFFSET, &temp32, 4);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            symbolId = htons(symbolId);
            memcpy(buffer+SYMBOL_ID_OFFSET, &symbolId, 2);
        }
        
        // Message processing
        bool FlagIsSet(NormCmdFlushMsg::Flag flag)
            {return (0 != (buffer[FLAGS_OFFSET] & flag));}
        NormObjectId GetObjectId() 
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+OBJECT_ID_OFFSET, 2);
            return (ntohs(temp16));    
        }        
        NormBlockId GetFecBlockId()
        {
            UINT32 temp32;
            memcpy(&temp32, buffer+BLOCK_ID_OFFSET, 4);
            return (ntohl(temp32));
        }
        UINT16 GetFecSymbolId()
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+SYMBOL_ID_OFFSET, 2);
            return (ntohs(temp16));  
        }
        // Flush messages are fixed length
        UINT16 GetLength() {return (SYMBOL_ID_OFFSET + 2);}
            
    private:
        enum
        {
            FLAGS_OFFSET        = FLAVOR_OFFSET + 1,
            OBJECT_ID_OFFSET    = FLAGS_OFFSET + 1,
            BLOCK_ID_OFFSET     = OBJECT_ID_OFFSET + 2,
            SYMBOL_ID_OFFSET    = BLOCK_ID_OFFSET + 4
        };
};  // end class NormCmdFlushMsg


class NormCmdSquelchMsg : public NormCmdMsg
{
    public:
        // Message building
        void SetObjectId(NormObjectId objectId)
        {
            UINT16 temp16 = htons((UINT16)objectId);
            memcpy(buffer+OBJECT_ID_OFFSET, &temp16, 2);
        }
        void SetFecBlockId(NormBlockId blockId)
        {
            UINT32 temp32 = htonl((UINT32)blockId);
            memcpy(buffer+BLOCK_ID_OFFSET, &temp32, 4);
        }
        void SetFecSymbolId(UINT16 symbolId)
        {
            symbolId = htons(symbolId);
            memcpy(buffer+SYMBOL_ID_OFFSET, &symbolId, 2);
        }
        
        void ResetInvalidObjectList() {length = OBJECT_LIST_OFFSET;}
        bool AppendInvalidObject(NormObjectId objectId, UINT16 segmentSize)
        {
            if ((length-OBJECT_LIST_OFFSET+2) > segmentSize) return false;
            UINT16 temp16 = htons((UINT16)objectId);
            memcpy(buffer+length, &temp16, 2);
            length += 2;
            return true;   
        }
        
        // Message processing
        NormObjectId GetObjectId() 
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+OBJECT_ID_OFFSET, 2);
            return (ntohs(temp16));    
        }        
        NormBlockId GetFecBlockId()
        {
            UINT32 temp32;
            memcpy(&temp32, buffer+BLOCK_ID_OFFSET, 4);
            return (NormBlockId(ntohl(temp32)));
        }
        UINT16 GetFecSymbolId()
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+SYMBOL_ID_OFFSET, 2);
            return (ntohs(temp16));  
        }   
        
        // Use these to parse invalid object list
        UINT16 GetInvalidObjectCount()
            {return ((length - OBJECT_LIST_OFFSET) / 2);} 
        NormObjectId GetInvalidObjectId(UINT16 index)
        {
            UINT16 temp16;
            memcpy(&temp16, buffer + (2*index+OBJECT_LIST_OFFSET), 2);  
            return (ntohs(temp16));
        }
        
    private:
        enum
        {
            RESERVED_OFFSET     = FLAVOR_OFFSET + 1,
            OBJECT_ID_OFFSET    = RESERVED_OFFSET + 1,
            BLOCK_ID_OFFSET     = OBJECT_ID_OFFSET + 2,
            SYMBOL_ID_OFFSET    = BLOCK_ID_OFFSET + 4,
            OBJECT_LIST_OFFSET  = SYMBOL_ID_OFFSET + 2,
        };           
};  // end class NormCmdSquelchMsg

class NormCmdAckReqMsg : public NormCmdMsg
{
    public:
        // Note: Support for application-defined AckFlavors (32-255) 
        //       may be provided, but 0-31 are reserved values
        enum AckFlavor
        {
            INVALID     =  0,
            WATERMARK   =  1,
            RTT         =  2,
            APP_BASE    = 32
        };
            
        // Message building
        void SetAckFlavor(UINT8 ackFlavor) 
            {buffer[ACK_FLAVOR_OFFSET] = ackFlavor;}
        // For setting generic content
        void SetAckContent(char content[64])
            {memcpy(buffer+CONTENT_OFFSET, content, 64);}
        
        void ResetAckingNodeList()
            {length = NODE_LIST_OFFSET;}
        UINT16 AppendAckingNode(NormNodeId nodeId)
        {
            nodeId = htonl(nodeId);
            memcpy(buffer+length, &nodeId, 4);
            return length;
        }
        
        void SetWatermark(NormObjectId objectId,
                          NormBlockId  fecBlockId,
                          UINT16       fecSymbolId)
        {
            UINT16 temp16 = htons((UINT16)objectId);
            memcpy(buffer+OBJECT_ID_OFFSET, &temp16, 2); 
            UINT32 temp32 = htonl((UINT32)fecBlockId);
            memcpy(buffer+BLOCK_ID_OFFSET, &temp32, 4);  
            fecSymbolId = htons(fecSymbolId);
            memcpy(buffer+SYMBOL_ID_OFFSET, &fecSymbolId, 2);
        }
        
        void SetRttSendTime(const struct timeval& sendTime)
        {
            UINT32 temp32 = htonl(sendTime.tv_sec);
            memcpy(buffer+SEND_TIME_OFFSET, &temp32, 4);
            temp32 = htonl(sendTime.tv_usec);
            memcpy(buffer+SEND_TIME_OFFSET+4, &temp32, 4);
        }
        
        // Message processing
        NormCmdAckReqMsg::AckFlavor GetAckFlavor() 
            {return (AckFlavor)buffer[ACK_FLAVOR_OFFSET];}
        char* GetAckContent() {return (buffer+CONTENT_OFFSET);}
        UINT16 GetAckingNodeCount()
            {return ((length - NODE_LIST_OFFSET) / 4);}
        NormNodeId GetAckingNodeId(UINT16 index)
        {
            UINT32 temp32;
            memcpy(&temp32, buffer+NODE_LIST_OFFSET+(4*index), 4);
            return (ntohl(temp32));   
        }
        
        void GetWatermark(NormObjectId& objectId,
                          NormBlockId&  fecBlockId,
                          UINT16        fecSymbolId)
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+OBJECT_ID_OFFSET, 2);
            objectId = ntohs(temp16);
            UINT32 temp32;
            memcpy(&temp32, buffer+BLOCK_ID_OFFSET, 4);
            fecBlockId = ntohl(temp32);
            memcpy(&fecSymbolId, buffer+SYMBOL_ID_OFFSET, 2);
            fecSymbolId = ntohs(fecSymbolId);
        }
    
        void GetRttSendTime(struct timeval& sendTime)
        {
            memcpy(&sendTime.tv_sec, buffer+SEND_TIME_OFFSET, 4);
            sendTime.tv_sec = ntohl(sendTime.tv_sec);   
            memcpy(&sendTime.tv_usec, buffer+SEND_TIME_OFFSET+4, 4);
            sendTime.tv_usec = ntohl(sendTime.tv_usec);
        }
            
    private:
        enum GenericOffsets
        {
            ACK_FLAVOR_OFFSET = FLAVOR_OFFSET + 1,
            CONTENT_OFFSET = ACK_FLAVOR_OFFSET + 1,
            NODE_LIST_OFFSET = CONTENT_OFFSET + 8
        };
        enum WatermarkOffsets
        {
            OBJECT_ID_OFFSET = CONTENT_OFFSET,
            BLOCK_ID_OFFSET  = OBJECT_ID_OFFSET + 2,
            SYMBOL_ID_OFFSET = BLOCK_ID_OFFSET + 4
        };
        enum RttOffsets
        {
            SEND_TIME_OFFSET = CONTENT_OFFSET  
        };                   
};  // end class NormCmdAckReqMsg


class NormCmdRepairAdvMsg : public NormCmdMsg
{
    public:
        enum Flag {NORM_REPAIR_ADV_FLAG_LIMIT = 0x01};
    // TBD (Uses NormNack format)
};  // end class NormCmdRepairAdvMsg


class NormCmdCCMsg : public NormCmdMsg
{
    // (TBD)  Congestion control command message
};  // end class NormCmdCCMsg

class NormCmdApplicationMsg : public NormCmdMsg
{
    // (TBD) Application-defined command (non-acked)
};  // end class NormCmdApplicationMsg
 
        
class NormRepairRequest
{
    public:
    class Iterator;
    friend class NormRepairRequest::Iterator;
            
        enum Form 
        {
            INVALID,
            ITEMS,
            RANGES,
            ERASURES
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
        void SetBuffer(char* bufferPtr, UINT16 bufferLen)
        {
            buffer = bufferPtr;
            buffer_len = bufferLen;
            length = 0;
        }
        static UINT16 RepairItemLength() {return 8;}
        static UINT16 RepairRangeLength() {return 16;}
        static UINT16 ErasureItemLength() {return 8;}
        
        // Repair request building
        void SetForm(NormRepairRequest::Form theForm) {form = theForm;}
        void ResetFlags() {flags = 0;}
        void SetFlag(NormRepairRequest::Flag theFlag) {flags |= theFlag;}
        //void SetFlags(int theFlags) {flags |= theFlags;} 
        void UnsetFlag(NormRepairRequest::Flag theFlag) {flags &= ~theFlag;} 
        
        
        
        // Returns length (each repair item requires 8 bytes of space)
        bool AppendRepairItem(const NormObjectId& objectId, 
                              const NormBlockId&  blockId,
                              UINT16              symbolId);
        
        bool AppendRepairRange(const NormObjectId& startObjectId, 
                               const NormBlockId&  startBlockId,
                               UINT16               startSymbolId,
                               const NormObjectId& endObjectId, 
                               const NormBlockId&  endBlockId,
                               UINT16              endSymbolId);  
        
        bool AppendErasureCount(const NormObjectId& objectId, 
                                const NormBlockId&  blockId,
                                UINT16              erasureCount);     
        
        UINT16 Pack();       
        
        // Repair request processing
        UINT16 Unpack();
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
                                    UINT16*       symbolId);
            private:
                NormRepairRequest& request;
                UINT16             offset;    
        };  // end class NormRepairRequest::Iterator
          
    private:
        bool RetrieveRepairItem(UINT16        offset,
                                NormObjectId* objectId, 
                                NormBlockId*  blockId,
                                UINT16*       erasureCount) const;
        enum
        {    
            FORM_OFFSET     = 0,
            FLAGS_OFFSET    = FORM_OFFSET + 1,   
            LENGTH_OFFSET   = FLAGS_OFFSET + 1,
            CONTENT_OFFSET  = LENGTH_OFFSET + 2
        }; 
        Form    form;
        int     flags;
        UINT16  length;
        char*   buffer;
        UINT16  buffer_len;
        
};  // end class NormRepairRequest


class NormNackMsg : public NormMsg
{
    public:
        // Message building
        void SetServerId(NormNodeId serverId)
        {
            serverId = htonl(serverId);
            memcpy(buffer+SERVER_ID_OFFSET, &serverId, 4);
        }
        void SetGrttResponse(const struct timeval& grttResponse)
        {
            UINT32 temp32 = htonl(grttResponse.tv_sec);
            memcpy(buffer+GRTT_RESPONSE_OFFSET, &temp32, 4);
            temp32 = htonl(grttResponse.tv_usec);
            memcpy(buffer+GRTT_RESPONSE_OFFSET+4, &temp32, 4);
        }
        void SetLossEstimate(UINT16 lossEstimate)
        {
            lossEstimate = htons(lossEstimate);
            memcpy(buffer+LOSS_OFFSET, &lossEstimate, 2);   
        }
        void SetGrttSequence(UINT16 sequence)
        {
            sequence = htons(sequence);
            memcpy(buffer+GRTT_SEQUENCE_OFFSET, &sequence, 2);   
        }
        
        void ResetNackContent() {length = CONTENT_OFFSET;}        
        void AttachRepairRequest(NormRepairRequest& request,
                                 UINT16             segmentMax)
        {
            int buflen = segmentMax - (length - CONTENT_OFFSET);
            buflen = (buflen>0) ? buflen : 0;
            request.SetBuffer(buffer+length, buflen);
        }
        UINT16 PackRepairRequest(NormRepairRequest& request)
        {
            UINT16 requestLength = request.Pack();
            length += request.Pack();
            return requestLength;
        }
                        
        // Message processing 
        NormNodeId GetServerId() 
        {
            UINT32 temp32;
            memcpy(&temp32, buffer+SERVER_ID_OFFSET, 4);
            return (ntohl(temp32));
        }
        void GetGrttResponse(struct timeval& grttResponse)
        {
            memcpy(&grttResponse.tv_sec, buffer+GRTT_RESPONSE_OFFSET, 4);
            grttResponse.tv_sec = ntohl(grttResponse.tv_sec);
            memcpy(&grttResponse.tv_usec, buffer+GRTT_RESPONSE_OFFSET+4, 4);
            grttResponse.tv_usec = ntohl(grttResponse.tv_usec);
        }
        UINT16 GetLossEstimate()
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+LOSS_OFFSET, 2);
            return (ntohs(temp16));   
        }
        UINT16 GetGrttSequence()
        {
            UINT16 temp16;
            memcpy(&temp16, buffer+GRTT_SEQUENCE_OFFSET, 2);
            return (ntohs(temp16));   
        }        
        
        UINT16 UnpackRepairRequest(NormRepairRequest& request,
                                   UINT16             requestOffset)
        {
            int buflen = length - CONTENT_OFFSET - requestOffset;
            buflen = (buflen > 0) ? buflen : 0;
            request.SetBuffer(buffer + CONTENT_OFFSET + requestOffset,
                              buflen);
            return request.Unpack();
        }
           
    private:
        enum
        {
            SERVER_ID_OFFSET        = MSG_OFFSET,
            GRTT_RESPONSE_OFFSET    = SERVER_ID_OFFSET + 4,
            LOSS_OFFSET             = GRTT_RESPONSE_OFFSET + 8,
            GRTT_SEQUENCE_OFFSET    = LOSS_OFFSET + 2,
            CONTENT_OFFSET          = GRTT_SEQUENCE_OFFSET + 2
        };    
};  // end class NormNackMsg

class NormAckMsg : public NormMsg
{
    // (TBD)
};  // end class NormAckMsg

class NormReportMsg : public NormMsg
{
    // (TBD)
};  // end class NormReportMsg

// One we've defined our basic message types, we
// do some unions so we can easily use these
// via casting or dereferencing the union members

class NormCommandMsg
{
    public:
        union 
        {
            NormCmdMsg              generic;
            NormCmdFlushMsg         flush;
            NormCmdSquelchMsg       squelch;
            NormCmdAckReqMsg        ack_req;
            NormCmdRepairAdvMsg     repair_adv;
            NormCmdCCMsg            cc;
            NormCmdApplicationMsg   app;
        };
};  // end class NormCommandMsg

class NormMessage
{
    friend class NormMessageQueue;
    public:
        union
        {
            NormMsg             generic;
            NormObjectMsg       object;
            NormInfoMsg         info;
            NormDataMsg         data;
            NormCommandMsg      cmd;
            NormNackMsg         nack;
            NormAckMsg          ack;
            NormReportMsg       report;
        };
        
    private:
        NormMessage*            prev;
        NormMessage*            next;
};  // end class NormMessage


class NormMessageQueue
{
    public:
        NormMessageQueue();
        ~NormMessageQueue();
        void Destroy();
        void Prepend(NormMessage* msg);
        void Append(NormMessage* msg);
        void Remove(NormMessage* msg);
        NormMessage* RemoveHead();
        NormMessage* RemoveTail();
        bool IsEmpty() {return ((NormMessage*)NULL == head);}
    
    private:
        NormMessage*    head;
        NormMessage*    tail;
};  // end class NormMessageQueue

#endif // _NORM_MESSAGE
