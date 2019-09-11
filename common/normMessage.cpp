#include "normMessage.h"

NormRepairRequest::NormRepairRequest()
 : form(INVALID), flags(0), length(0), buffer(NULL), buffer_len(0)
{
}

bool NormRepairRequest::AppendRepairItem(const NormObjectId& objectId, 
                                         const NormBlockId&  blockId,
                                         UINT16              symbolId)
{
    if (RANGES == form)
        DMSG(4, "NormRepairRequest::AppendRepairRange(obj>%hu blk>%lu seg>%hu) ...\n",
            (UINT16)objectId, (UINT32)blockId, (UINT32)symbolId);
    else
        DMSG(4, "NormRepairRequest::AppendRepairItem(obj>%hu blk>%lu seg>%hu) ...\n",
            (UINT16)objectId, (UINT32)blockId, (UINT32)symbolId);
    if (buffer_len >= (length+CONTENT_OFFSET+RepairItemLength()))
    {
        UINT16 temp16 = htons((UINT16)objectId);
        memcpy(buffer+CONTENT_OFFSET+length, &temp16, 2);
        UINT32 temp32 =htonl((UINT32)blockId);
        memcpy(buffer+length+CONTENT_OFFSET+2, &temp32, 4);
        symbolId = htons(symbolId);
        memcpy(buffer+length+CONTENT_OFFSET+6, &symbolId, 2);
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
                                          UINT16                startSymbolId,
                                          const NormObjectId&   endObjectId, 
                                          const NormBlockId&    endBlockId,
                                          UINT16                endSymbolId)
{
    DMSG(4, "NormRepairRequest::AppendRepairRange(%hu:%lu:%hu->%hu:%lu:%hu) ...\n",
            (UINT16)startObjectId, (UINT32)startBlockId, (UINT32)startSymbolId,
            (UINT16)endObjectId, (UINT32)endBlockId, (UINT32)endSymbolId);
    if (buffer_len >= (length+CONTENT_OFFSET+RepairRangeLength()))
    {
        // range start
        UINT16 temp16;
        temp16 = htons((UINT16)startObjectId);
        memcpy(buffer+CONTENT_OFFSET+length, &temp16, 2);
        UINT32 temp32 =htonl((UINT32)startBlockId);
        memcpy(buffer+length+CONTENT_OFFSET+2, &temp32, 4);
        startSymbolId = htons(startSymbolId);
        memcpy(buffer+length+CONTENT_OFFSET+6, &startSymbolId, 2);
        // range end
        temp16 = htons((UINT16)endObjectId);
        memcpy(buffer+CONTENT_OFFSET+length+8, &temp16, 2);
        temp32 =htonl((UINT32)endBlockId);
        memcpy(buffer+length+CONTENT_OFFSET+10, &temp32, 4);
        endSymbolId = htons(endSymbolId);
        memcpy(buffer+length+CONTENT_OFFSET+14, &endSymbolId, 2);
        length += RepairRangeLength();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::AppendErasureCount()

bool NormRepairRequest::AppendErasureCount(const NormObjectId& objectId, 
                                           const NormBlockId&  blockId,
                                           UINT16              erasureCount)
{
    if (buffer_len >= (CONTENT_OFFSET+length+ErasureItemLength()))
    {
        UINT16 temp16 = htons((UINT16)objectId);
        memcpy(buffer+CONTENT_OFFSET+length, &temp16, 2);
        UINT32 temp32 =htonl((UINT32)blockId);
        memcpy(buffer+length+CONTENT_OFFSET+2, &temp32, 4);
        erasureCount = htons(erasureCount);
        memcpy(buffer+length+CONTENT_OFFSET+6, &erasureCount, 2);
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
        UINT16 temp16 = htons(length);
        memcpy(buffer+LENGTH_OFFSET, &temp16, 2);
        return (CONTENT_OFFSET + length);
    }
    else
    {
        return 0;
    }
}  // end NormRepairRequest::Pack()


UINT16 NormRepairRequest::Unpack()
{
    // Make sure there's at least a header
    if (buffer_len >= CONTENT_OFFSET)
    {
        form = (Form)buffer[FORM_OFFSET];
        flags = (int)buffer[FLAGS_OFFSET];
        memcpy(&length, buffer+LENGTH_OFFSET, 2);
        length = ntohs(length);
        if (length > (buffer_len - CONTENT_OFFSET))
        {
            // Badly formed message
            return 0;
        }
        else 
        {
            return (CONTENT_OFFSET+length);
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
                                           UINT16*       symbolId) const
{
    if (length >= (offset + RepairItemLength()))
    {
        UINT16 temp16;
        memcpy(&temp16, buffer+CONTENT_OFFSET+offset, 2);
        *objectId = ntohs(temp16);
        UINT32 temp32;
        memcpy(&temp32, buffer+CONTENT_OFFSET+offset+2, 4);
        *blockId = ntohl(temp32);
        memcpy(symbolId, buffer+CONTENT_OFFSET+offset+6, 2);
        *symbolId = ntohs(*symbolId);
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
                                                 UINT16*       symbolId)
{
    if (request.RetrieveRepairItem(offset, objectId, blockId, symbolId))
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
    NormMessage* next;
    while ((next = head))
    {
        head = next->next;
        delete next;
    }   
}  // end NormMessageQueue::Destroy()


void NormMessageQueue::Prepend(NormMessage* msg)
{
    if ((msg->next = head))
        head->prev = msg;
    else
        tail = msg;
    msg->prev = NULL;
    head = msg;
}  // end NormMessageQueue::Prepend()

void NormMessageQueue::Append(NormMessage* msg)
{
    if ((msg->prev = tail))
        tail->next = msg;
    else
        head = msg;
    msg->next = NULL;
    tail = msg;
}  // end NormMessageQueue::Append() 

void NormMessageQueue::Remove(NormMessage* msg)
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

NormMessage* NormMessageQueue::RemoveHead()
{
    if (head)
    {
        NormMessage* msg = head;
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

NormMessage* NormMessageQueue::RemoveTail()
{
    if (tail)
    {
        NormMessage* msg = tail;
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



bool NormObjectSize::operator<(const NormObjectSize& size) const
{
    UINT16 diff = msb - size.msb;
    if (0 == diff)
        return (lsb < size.lsb);
    else if ((diff > 0x8000) ||
        ((0x8000 == diff) && (msb > size.msb)))
        return true;
    else
        return false;
}  // end NormObjectSize::operator<(NormObjectSize size)

bool NormObjectSize::operator>(const NormObjectSize& size) const
{
    UINT16 diff = size.msb - msb;
    if (0 == diff)
        return (lsb > size.lsb);
    else if ((diff > 0x8000) ||
        ((0x8000 == diff) && (size.msb > msb)))
        return true;
    else
        return false;
}  // end NormObjectSize::operator>(NormObjectSize id)

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


bool NormObjectId::operator<(const NormObjectId& id) const
{
    UINT16 diff = value - id.value;
    if ((diff > 0x8000) ||
        ((0x8000 == diff) && (value > id.value)))
        return true;
    else
        return false;
}  // end NormObjectId::operator<(NormObjectId id)

bool NormObjectId::operator>(const NormObjectId& id) const
{
    UINT16 diff = id.value - value;;
    if ((diff > 0x8000) ||
        ((0x8000 == diff) && (id.value > value)))
        return true;
    else
        return false;
}  // end NormObjectId::operator>(NormObjectId id)

int NormObjectId::operator-(const NormObjectId& id) const
{
    UINT16 diff = value - id.value;
    if ((diff > 0x8000) ||
        ((0x8000 == diff) && (value > id.value)))
        return ((INT16)(value - id.value));
    else
        return (-(INT16)(id.value - value));
}  // end NormObjectId::operator-(NormObjectId id) const


bool NormBlockId::operator<(const NormBlockId& id) const
{
    UINT32 diff = value - id.value;
    if ((diff > 0x80000000) ||
        ((0x80000000 == diff) && (value > id.value)))
        return true;
    else
        return false;
}  // end NormBlockId::operator<(NormBlockId id)

bool NormBlockId::operator>(const NormBlockId& id) const
{
    UINT32 diff = id.value - value;;
    if ((diff > 0x80000000) ||
        ((0x80000000 == diff) && (id.value > value)))
        return true;
    else
        return false;
}  // end NormBlockId::operator>(NormBlockId id)

long NormBlockId::operator-(const NormBlockId& id) const
{
    UINT32 diff = value - id.value;
    if ((diff > 0x80000000) ||
        ((0x80000000 == diff) && (value > id.value)))
        return ((INT32)(value - id.value));
    else
        return (-(INT32)(id.value - value));
}  // end NormBlockId::operator-(NormBlockId id) const
