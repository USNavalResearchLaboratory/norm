#include "normMessage.h"


NormHeaderExtension::NormHeaderExtension()
 : buffer(NULL)
{
    
}


NormMsg::NormMsg() 
 : length(8), header_length(8), header_length_base(8)
{
    SetType(INVALID);
    SetVersion(NORM_PROTOCOL_VERSION);
}

bool NormMsg::InitFromBuffer(UINT16 msgLength)
{
    header_length = GetHeaderLength();
    // "header_length_base" is type dependent
    switch (GetType())
    {
        
        // for INFO, DATA, and  
        case INFO:
            header_length_base = 16;
            break;
        case DATA:
            // (TBD) look at "fec_id" to determine "fec_payload_id" size 
            // (we _assume_ "fec_id" == 129 here
            if ((unsigned char)buffer[NormDataMsg::FEC_ID_OFFSET] == 129)
            {
                header_length_base = 24;
            }
            else
            {
                DMSG(0, "NormMsg::InitFromBuffer(DATA) unknown fec_id value: %u\n",
                         buffer[NormDataMsg::FEC_ID_OFFSET]);
                return false;
            }
            break;
        case CMD:
            switch (buffer[NormCmdMsg::FLAVOR_OFFSET])
            {
                case NormCmdMsg::FLUSH:
                case NormCmdMsg::SQUELCH:
                    // (TBD) look at "fec_id" to determine "fec_payload_id" size 
                    // (we _assume_ "fec_id" == 129 here
                    if ((unsigned char)buffer[NormCmdFlushMsg::FEC_ID_OFFSET] == 129)
                    {
                        header_length_base = 24;
                    }
                    else
                    {
                        DMSG(0, "NormMsg::InitFromBuffer(FLUSH|SQUELCH) unknown fec_id value: %u\n",
                                 buffer[NormDataMsg::FEC_ID_OFFSET]);
                        return false;
                    }
                    break;
                case NormCmdMsg::EOT:
                case NormCmdMsg::REPAIR_ADV:
                case NormCmdMsg::ACK_REQ: 
                case NormCmdMsg::APPLICATION: 
                    header_length_base = 16;
                    break;
                case NormCmdMsg::CC:
                    header_length_base = 24;
                    break;
            }
            break;
        case NACK:
        case ACK:
            header_length_base= 24;
            break;
        case REPORT:
            header_length_base= 8;
            break;
           
        default:
            DMSG(0, "NormMsg::InitFromBuffer() invalid message type!\n");
            return false;
    }
    ASSERT(msgLength >= header_length);
    length = msgLength;
    return true;
}  // end NormMsg::InitFromBuffer()

bool NormCmdCCMsg::GetCCNode(NormNodeId     nodeId, 
                             UINT8&         flags, 
                             UINT8&         rtt, 
                             UINT16&        rate) const
{
    UINT16 cmdLength = length;
    UINT16 offset = header_length;
    nodeId = htonl(nodeId);
    while (offset < cmdLength)
    {
        if (nodeId == *((UINT32*)(buffer+offset)))
        {
            const char* ptr = buffer+offset;
            flags = ptr[CC_FLAGS_OFFSET];
            rtt = ptr[CC_RTT_OFFSET];
            rate = ntohs(*((UINT16*)(ptr+CC_RATE_OFFSET)));
            return true;
        } 
        offset += CC_ITEM_SIZE;
    }
    return false;
}  // end NormCmdCCMsg::GetCCNode()

NormCmdCCMsg::Iterator::Iterator(const NormCmdCCMsg& msg)
 : cc_cmd(msg), offset(0)
{
    
}

bool NormCmdCCMsg::Iterator::GetNextNode(NormNodeId&    nodeId, 
                                         UINT8&         flags, 
                                         UINT8&         rtt, 
                                         UINT16&        rate)
{
    if ((offset+CC_ITEM_SIZE) >  cc_cmd.GetLength()) return false; 
    const char* ptr = cc_cmd.buffer + cc_cmd.header_length;
    nodeId = ntohl(*((UINT32*)(ptr+offset))); 
    flags = ptr[offset+CC_FLAGS_OFFSET];
    rtt = ptr[offset+CC_RTT_OFFSET];
    rate = ntohs(*((UINT16*)(ptr+CC_RATE_OFFSET)));
    offset += CC_ITEM_SIZE;
    return true;
}  // end NormCmdCCMsg::Iterator::GetNextNode()


NormRepairRequest::NormRepairRequest()
 : form(INVALID), flags(0), length(0), buffer(NULL), buffer_len(0)
{
}

bool NormRepairRequest::AppendRepairItem(const NormObjectId& objectId, 
                                         const NormBlockId&  blockId,
                                         UINT16              blockLen,
                                         UINT16              symbolId)
{
    if (RANGES == form)
        DMSG(4, "NormRepairRequest::AppendRepairItem-Range(obj>%hu blk>%lu seg>%hu) ...\n",
            (UINT16)objectId, (UINT32)blockId, (UINT32)symbolId);
    else
        DMSG(4, "NormRepairRequest::AppendRepairItem(obj>%hu blk>%lu seg>%hu) ...\n",
            (UINT16)objectId, (UINT32)blockId, (UINT32)symbolId);
    if (buffer_len >= (length+ITEM_LIST_OFFSET+RepairItemLength()))
    {
        char* ptr = buffer + length + ITEM_LIST_OFFSET;
        ptr[FEC_ID_OFFSET] = (char)129;
        ptr[RESERVED_OFFSET] = 0;
        *((UINT16*)(ptr+OBJ_ID_OFFSET)) = htons((UINT16)objectId);
        *((UINT32*)(ptr+BLOCK_ID_OFFSET)) = htonl((UINT32)blockId);
        *((UINT16*)(ptr+BLOCK_LEN_OFFSET)) = htons((UINT16)blockLen);
        *((UINT16*)(ptr+SYMBOL_ID_OFFSET)) = htons((UINT16)symbolId);
        length += RepairItemLength();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::AppendRepairItem()

bool NormRepairRequest::AppendRepairRange(const NormObjectId&   startObjectId, 
                                          const NormBlockId&    startBlockId,
                                          UINT16                startBlockLen,
                                          UINT16                startSymbolId,
                                          const NormObjectId&   endObjectId, 
                                          const NormBlockId&    endBlockId,
                                          UINT16                endBlockLen,
                                          UINT16                endSymbolId)
{
    DMSG(4, "NormRepairRequest::AppendRepairRange(%hu:%lu:%hu->%hu:%lu:%hu) ...\n",
            (UINT16)startObjectId, (UINT32)startBlockId, (UINT32)startSymbolId,
            (UINT16)endObjectId, (UINT32)endBlockId, (UINT32)endSymbolId);
    if (buffer_len >= (length+ITEM_LIST_OFFSET+RepairRangeLength()))
    {
        // range start
        char* ptr = buffer + length + ITEM_LIST_OFFSET;
        ptr[FEC_ID_OFFSET] = (char)129;
        ptr[RESERVED_OFFSET] = 0;
        *((UINT16*)(ptr+OBJ_ID_OFFSET)) = htons((UINT16)startObjectId);
        *((UINT32*)(ptr+BLOCK_ID_OFFSET)) = htonl((UINT32)startBlockId);
        *((UINT16*)(ptr+BLOCK_LEN_OFFSET)) = htons((UINT16)startBlockLen);
        *((UINT16*)(ptr+SYMBOL_ID_OFFSET)) = htons((UINT16)startSymbolId);
        ptr += RepairItemLength();
        // range end
        ptr[FEC_ID_OFFSET] = (char)129;
        ptr[RESERVED_OFFSET] = 0;
        *((UINT16*)(ptr+OBJ_ID_OFFSET)) = htons((UINT16)endObjectId);
        *((UINT32*)(ptr+BLOCK_ID_OFFSET)) = htonl((UINT32)endBlockId);
        *((UINT16*)(ptr+BLOCK_LEN_OFFSET)) = htons((UINT16)endBlockLen);
        *((UINT16*)(ptr+SYMBOL_ID_OFFSET)) = htons((UINT16)endSymbolId);
        length += RepairRangeLength();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::AppendRepairRange()

bool NormRepairRequest::AppendErasureCount(const NormObjectId& objectId, 
                                           const NormBlockId&  blockId,
                                           UINT16              blockLen,
                                           UINT16              erasureCount)
{
    if (buffer_len >= (ITEM_LIST_OFFSET+length+ErasureItemLength()))
    {
        char* ptr = buffer + length + ITEM_LIST_OFFSET;
        ptr[FEC_ID_OFFSET] = (char)129;
        ptr[RESERVED_OFFSET] = 0;
        *((UINT16*)(ptr+OBJ_ID_OFFSET)) = htons((UINT16)objectId);
        *((UINT32*)(ptr+BLOCK_ID_OFFSET)) = htonl((UINT32)blockId);
        *((UINT16*)(ptr+BLOCK_LEN_OFFSET)) = htons((UINT16)blockLen);
        *((UINT16*)(ptr+SYMBOL_ID_OFFSET)) = htons((UINT16)erasureCount);
        length += ErasureItemLength();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::AppendErasureCount()


UINT16 NormRepairRequest::Pack()
{
    if (length)
    {
        buffer[FORM_OFFSET] = form;
        buffer[FLAGS_OFFSET] = (char)flags;
        *((UINT16*)(buffer+LENGTH_OFFSET)) = htons(length);
        return (ITEM_LIST_OFFSET + length);
    }
    else
    {
        return 0;
    }
}  // end NormRepairRequest::Pack()


UINT16 NormRepairRequest::Unpack(const char* bufferPtr, UINT16 bufferLen)
{
    buffer =  (char*)bufferPtr;
    buffer_len = bufferLen;
    length = 0;
    
    // Make sure there's at least a header
    if (buffer_len >= ITEM_LIST_OFFSET)
    {
        form = (Form)buffer[FORM_OFFSET];
        flags = (int)buffer[FLAGS_OFFSET];
        length = ntohs(*((UINT16*)(buffer+LENGTH_OFFSET)));
        if (length > (buffer_len - ITEM_LIST_OFFSET))
        {
            // Badly formed message
            return 0;
        }
        else 
        {
            return (ITEM_LIST_OFFSET+length);
        }
    }
    else
    {
        return 0;
    }
}  // end NormRepairRequest::Unpack()

bool NormRepairRequest::RetrieveRepairItem(UINT16        offset,
                                           NormObjectId* objectId,
                                           NormBlockId*  blockId,
                                           UINT16*       blockLen,
                                           UINT16*       symbolId) const
{
    if (length >= (offset + RepairItemLength()))
    {
        const char* ptr = buffer+ITEM_LIST_OFFSET+offset;
        *objectId = ntohs(*((UINT16*)(ptr+OBJ_ID_OFFSET)));
        *blockId = ntohl(*((UINT32*)(ptr+BLOCK_ID_OFFSET)));
        *blockLen = ntohs(*((UINT16*)(ptr+BLOCK_LEN_OFFSET)));
        *symbolId = ntohs(*((UINT16*)(ptr+SYMBOL_ID_OFFSET)));
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::RetrieveRepairItem()

NormRepairRequest::Iterator::Iterator(NormRepairRequest& theRequest)
 : request(theRequest), offset(0)
{
}

// For erasure requests, symbolId is loaded with erasureCount
bool NormRepairRequest::Iterator::NextRepairItem(NormObjectId* objectId,
                                                 NormBlockId*  blockId,
                                                 UINT16*       blockLen,
                                                 UINT16*       symbolId)
{
    if (request.RetrieveRepairItem(offset, objectId, blockId, blockLen, symbolId))
    {
        offset += NormRepairRequest::RepairItemLength();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::Iterator::NextRepairItem()




NormMessageQueue::NormMessageQueue()
 : head(NULL), tail(NULL)
{   
}

NormMessageQueue::~NormMessageQueue()
{
    Destroy();
}

void NormMessageQueue::Destroy()
{
    NormMsg* next;
    while ((next = head))
    {
        head = next->next;
        delete next;
    }   
}  // end NormMessageQueue::Destroy()


void NormMessageQueue::Prepend(NormMsg* msg)
{
    if ((msg->next = head))
        head->prev = msg;
    else
        tail = msg;
    msg->prev = NULL;
    head = msg;
}  // end NormMessageQueue::Prepend()

void NormMessageQueue::Append(NormMsg* msg)
{
    if ((msg->prev = tail))
        tail->next = msg;
    else
        head = msg;
    msg->next = NULL;
    tail = msg;
}  // end NormMessageQueue::Append() 

void NormMessageQueue::Remove(NormMsg* msg)
{
    if (msg->prev)
        msg->prev->next = msg->next;
    else
        head = msg->next;
    if (msg->next)
        msg->next->prev = msg->prev;
    else
        tail = msg->prev;
}  // end NormMessageQueue::Remove()

NormMsg* NormMessageQueue::RemoveHead()
{
    if (head)
    {
        NormMsg* msg = head;
        if ((head = msg->next))
            msg->next->prev = NULL;
        else
            tail = NULL;
        return msg;
    }
    else
    {
        return NULL;
    }
}  // end NormMessageQueue::RemoveHead()

NormMsg* NormMessageQueue::RemoveTail()
{
    if (tail)
    {
        NormMsg* msg = tail;
        if ((tail = msg->prev))
            msg->prev->next = NULL;
        else
            head = NULL;
        return msg;
    }
    else
    {
        return NULL;
    }
}  // end NormMessageQueue::RemoveTail()


/****************************************************************
 *  RTT quantization routines:
 *  These routines are valid for 1.0e-06 <= RTT <= 1000.0 seconds
 *  They allow us to pack our RTT estimates into a 1 byte fields
 */

// valid for rtt = 1.0e-06 to 1.0e+03
unsigned char NormQuantizeRtt(double rtt)
{
    if (rtt > NORM_RTT_MAX)
        rtt = NORM_RTT_MAX;
    else if (rtt < NORM_RTT_MIN)
        rtt = NORM_RTT_MIN;
    if (rtt < 3.3e-05) 
        return ((unsigned char)ceil((rtt*NORM_RTT_MIN)) - 1);
    else
        return ((unsigned char)(ceil(255.0 - (13.0*log(NORM_RTT_MAX/rtt)))));
}  // end NormQuantizeRtt()

NormObjectSize NormObjectSize::operator+(const NormObjectSize& size) const
{
    NormObjectSize total;
    total.msb = msb + size.msb;
    total.lsb = lsb + size.lsb;
    if ((total.lsb < lsb) || (total.lsb < size.lsb)) total.msb++;
    return total;
}  // end NormObjectSize::operator+(NormObjectSize id)

NormObjectSize NormObjectSize::operator-(const NormObjectSize& b) const
{   
    NormObjectSize result;
    result.lsb = lsb - b.lsb;
    result.msb = msb - b.msb;
    if (lsb < b.lsb) result.msb--;
    return result;
}  // end NormObjectSize::operator-(NormObjectSize id)

NormObjectSize NormObjectSize::operator*(const NormObjectSize& b) const
{
    UINT32 ll = (lsb & 0x0000ffff) * (b.lsb & 0x0000ffff);
    UINT32 lm = (lsb & 0x0000ffff) * ((b.lsb >> 16) & 0x0000ffff);
    UINT32 ml = ((lsb >> 16) & 0x0000ffff) * (b.lsb & 0x0000ffff);
    UINT32 lu = (lsb & 0x0000ffff) * (UINT32)b.msb;
    UINT32 mm = ((lsb >> 16) & 0x0000ffff) *
                ((b.lsb >> 16) & 0x0000ffff);
    NormObjectSize result;
    result.lsb = ll + (lm << 16) + (ml << 16);
    result.msb = lu + mm + ((lm >> 16) & 0x0000ffff) + 
                 ((ml >> 16) & 0x0000ffff);
    return result;
}  // end NormObjectSize::operator*(NormObjectSize size)

// Note: This always rounds up if there is _any_ remainder
NormObjectSize NormObjectSize::operator/(const NormObjectSize& b) const
{    
    // Zero dividend is special case
    if ((0 == lsb) && (0 == msb)) return NormObjectSize(0, 0);
    // Zero divisor is special case
    if ((0 == b.lsb) && (0 == b.msb)) return NormObjectSize(0xffff, 0xffffffff);
    // Divisor >= dividend is special case
    if ((b.lsb >= lsb) && (b.msb >= msb)) return NormObjectSize(0,1);    
    // divisor
    UINT32 divisor[2];
    divisor[0] = (UINT32)b.msb;
    divisor[1] = b.lsb;
    // dividend
    UINT32 dividend[2];
    dividend[0] = msb;
    dividend[1] = (UINT32)lsb;  
    // remainder
    UINT32 remainder[2];
    remainder[0] = remainder[1] = 0;     
    unsigned int numBits = 64;
    UINT32 d[2];
    while (((divisor[1] > remainder[1]) && (divisor[0] == remainder[0])) ||
           (divisor[0] > remainder[0]))
    {
        remainder[0] <<= 1;
        remainder[0] |= (remainder[1] >> 31) & 0x00000001;
        remainder[1] <<= 1;
        remainder[1] |= (dividend[0] >> 31) & 0x00000001;
        d[0] = dividend[0];
        d[1] = dividend[1];
        dividend[0] <<= 1;
        dividend[0] |= (dividend[1] >> 31) & 0x00000001;
        dividend[1] <<= 1;
        numBits--;   
    }
    
    // Note: above loop goes one step too far
    //       so we incr numBits, use old dividend (d)
    numBits++;
    UINT32 quotient[2];
    quotient[0] = quotient[1] = 0;
    bool extra = false;
    for (unsigned int i = 0; i < numBits; i++)
    {
        UINT32 t[2];
        t[0] = remainder[0] - divisor[0];
        t[1] = remainder[1] - divisor[1];
        if (remainder[1] < divisor[1]) t[0]--;
        UINT32 q = ((t[0] >> 31) & 0x00000001) ^ 0x00000001;
        d[0] <<= 1;
        d[0] |= (d[1] >> 31) & 0x00000001;
        d[1] <<= 1;
        quotient[0] <<= 1;
        quotient[1] = (quotient[1] << 1) | q;
        if (q)
        {
            remainder[0] = t[0];
            remainder[1] = t[1];   
        }
        if (remainder[0] || remainder[1]) 
            extra = true;
        else
            extra = false;
        remainder[0] <<= 1;
        remainder[0] |= (remainder[1] >> 31) & 0x00000001;
        remainder[1] <<= 1;
        remainder[1] |= (d[0] >> 31) & 0x00000001;
    }
    NormObjectSize result((UINT16)quotient[0], quotient[1]);
    // If there was _any_ remainder, round up
    if (extra) result++;
    return result;
}  // end NormObjectSize::operator/(NormObjectSize b)

