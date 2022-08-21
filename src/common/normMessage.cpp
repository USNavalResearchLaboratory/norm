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
        {
            UINT8 fecId = ((UINT8*)buffer)[NormObjectMsg::FEC_ID_OFFSET];
            UINT16 fecIdLen = NormPayloadId::GetLength(fecId);
            if (0 == fecIdLen)
            {
                PLOG(PL_FATAL, "NormMsg::InitFromBuffer(DATA) unknown fec_id value: %u\n", fecId);
                return false;
            }
            header_length_base  = NormObjectMsg::OBJ_MSG_OFFSET + fecIdLen;
            break;
        }
        case CMD:
            switch (((UINT8*)buffer)[NormCmdMsg::FLAVOR_OFFSET])
            {
                case NormCmdMsg::FLUSH:
                case NormCmdMsg::SQUELCH:
                {
                    UINT8 fecId = ((UINT8*)buffer)[NormCmdFlushMsg::FEC_ID_OFFSET];
                    UINT16 fecIdLen = NormPayloadId::GetLength(fecId);
                    if (0 == fecIdLen)
                    {
                        PLOG(PL_FATAL, "NormMsg::InitFromBuffer(DATA) unknown fec_id value: %u\n", fecId);
                        return false;
                    }
                    header_length_base  = 4 * NormCmdFlushMsg::FEC_PAYLOAD_ID_OFFSET + fecIdLen;
                    break;
                }
                case NormCmdMsg::EOT:
                case NormCmdMsg::REPAIR_ADV:
                case NormCmdMsg::ACK_REQ: 
                case NormCmdMsg::APPLICATION: 
                    header_length_base = 16;
                    break;
                case NormCmdMsg::CC:
                    header_length_base = 24;
                    break;
                default:
                    PLOG(PL_FATAL, "NormMsg::InitFromBuffer() recv'd unkown cmd flavor:%d\n",
                        ((UINT8*)buffer)[NormCmdMsg::FLAVOR_OFFSET]);
                    return false;
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
            PLOG(PL_FATAL, "NormMsg::InitFromBuffer() invalid message type!\n");
            return false;
    }
    if (msgLength < header_length)
    {
        PLOG(PL_FATAL, "NormMsg::InitFromBuffer() invalid message or header length\n");
        return false;
    }
    else
    {
        length = msgLength;
        return true;
    }
}  // end NormMsg::InitFromBuffer()

void NormMsg::Display() const
{
    const unsigned char* ptr = (const unsigned char*)buffer;
    for (unsigned int i = 0; i < length; i++)
        PLOG(PL_ALWAYS, "%02x", *ptr++);
}  // end NormMsg::Display()

bool NormMsg::HasExtension(NormHeaderExtension::Type extType)
{
    
    NormHeaderExtension ext;
    while (GetNextExtension(ext))
    {
        if (ext.GetType() == extType)
            return true;
    }
    return false;
}  // end NormMsg::HasExtension()

bool NormCmdCCMsg::GetCCNode(NormNodeId     nodeId, 
                             UINT8&         flags, 
                             UINT8&         rtt, 
                             UINT16&        rate) const
{
    UINT16 cmdLength = length/4;
    UINT16 offset = header_length/4;
    nodeId = htonl(nodeId);
    while (offset < cmdLength)
    {
        if (nodeId == buffer[offset])
        {
            const UINT32* ptr = buffer+offset;
            flags = ((UINT8*)ptr)[CC_FLAGS_OFFSET];
            rtt = ((UINT8*)ptr)[CC_RTT_OFFSET];
            rate = ntohs(((UINT16*)ptr)[CC_RATE_OFFSET]);
            return true;
        } 
        offset += CC_ITEM_SIZE/4;
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
    if ((offset+CC_ITEM_SIZE) > cc_cmd.GetLength()) return false; 
    const UINT32* ptr = cc_cmd.buffer + cc_cmd.header_length/4;
    nodeId = ntohl(ptr[offset/4]); 
    flags = ((UINT8*)ptr)[offset+CC_FLAGS_OFFSET];
    rtt = ((UINT8*)ptr)[offset+CC_RTT_OFFSET];
    rate = ntohs(((UINT16*)ptr)[(offset/2)+CC_RATE_OFFSET]);
    offset += CC_ITEM_SIZE;
    return true;
}  // end NormCmdCCMsg::Iterator::GetNextNode()

NormRepairRequest::NormRepairRequest()
 : form(INVALID), flags(0), length(0), buffer(NULL), buffer_len(0)
{
}

bool NormRepairRequest::AppendRepairItem(UINT8               fecId,
                                         UINT8               fecM,
                                         const NormObjectId& objectId, 
                                         const NormBlockId&  blockId,
                                         UINT16              blockLen,
                                         UINT16              symbolId)
{
    if (RANGES == form)
        PLOG(PL_TRACE, "NormRepairRequest::AppendRepairItem-Range(fecId>%d obj>%hu blk>%lu seg>%hu) ...\n",
            fecId, (UINT16)objectId, (unsigned long)blockId.GetValue(), (UINT16)symbolId);
    else
        PLOG(PL_TRACE, "NormRepairRequest::AppendRepairItem(fecId>%d obj>%hu blk>%lu seg>%hu) ...\n",
                       fecId, (UINT16)objectId, (unsigned long)blockId.GetValue(), (UINT16)symbolId);
    ASSERT(NormPayloadId::IsValid(fecId));
    
    UINT16 itemLength = RepairItemLength(fecId);
    if (buffer_len >= (ITEM_LIST_OFFSET+length+itemLength))
    {
        UINT32* ptr = buffer + (length + ITEM_LIST_OFFSET)/4;
        ((UINT8*)ptr)[FEC_ID_OFFSET] = fecId;                       // 1 byte
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;                         // 1 byte
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)objectId);    // 2 bytes
        NormPayloadId payloadId(fecId, fecM, ptr + 1);
        payloadId.SetFecPayloadId(blockId.GetValue(), symbolId, blockLen);
        length += itemLength;
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::AppendRepairItem()

bool NormRepairRequest::AppendRepairRange(UINT8                 fecId,
                                          UINT8                 fecM,
                                          const NormObjectId&   startObjectId, 
                                          const NormBlockId&    startBlockId,
                                          UINT16                startBlockLen,
                                          UINT16                startSymbolId,
                                          const NormObjectId&   endObjectId, 
                                          const NormBlockId&    endBlockId,
                                          UINT16                endBlockLen,
                                          UINT16                endSymbolId)
{
    PLOG(PL_TRACE, "NormRepairRequest::AppendRepairRange(%hu:%lu:%hu->%hu:%lu:%hu) ...\n",
            (UINT16)startObjectId, (unsigned long)startBlockId.GetValue(), (UINT16)startSymbolId,
            (UINT16)endObjectId, (unsigned long)endBlockId.GetValue(), (UINT16)endSymbolId);
    // Note a "range" is two repair items
    UINT16 rangeLength = RepairRangeLength(fecId);
    if (buffer_len >= (ITEM_LIST_OFFSET+length+rangeLength))
    {
        // range start
        UINT32* ptr = buffer + (length + ITEM_LIST_OFFSET)/4;
        ((UINT8*)ptr)[FEC_ID_OFFSET] = fecId;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)startObjectId);
        NormPayloadId startId(fecId, fecM, ptr + 1);
        startId.SetFecPayloadId(startBlockId.GetValue(), startSymbolId, startBlockLen);
        // range end
        ptr += (rangeLength/8);  // advance ptr to second part of repair range
        ((UINT8*)ptr)[FEC_ID_OFFSET] = fecId;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)endObjectId);
        NormPayloadId endId(fecId, fecM, ptr + 1);
        endId.SetFecPayloadId(endBlockId.GetValue(), endSymbolId, endBlockLen);
        length += rangeLength;
        return true;
    }
    else
    {
        return false;
    }
}  // end NormRepairRequest::AppendRepairRange()

bool NormRepairRequest::AppendErasureCount(UINT8                fecId,
                                           UINT8                fecM,
                                           const NormObjectId&  objectId, 
                                           const NormBlockId&   blockId,
                                           UINT16               blockLen,
                                           UINT16               erasureCount)
{
    UINT16 itemLength = ErasureItemLength(fecId);
    if (buffer_len >= (ITEM_LIST_OFFSET+length+itemLength))
    {
        UINT32* ptr = buffer + (length + ITEM_LIST_OFFSET)/4;
        ((UINT8*)ptr)[FEC_ID_OFFSET] = fecId;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)objectId);
        NormPayloadId erasureId(fecId, fecM, ptr + 1);
        erasureId.SetFecPayloadId(blockId.GetValue(), erasureCount, blockLen);
        length += itemLength;
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
        ((UINT8*)buffer)[FORM_OFFSET] = (UINT8)form;
        ((UINT8*)buffer)[FLAGS_OFFSET] = (UINT8)flags;
        ((UINT16*)buffer)[LENGTH_OFFSET] = htons(length);
        return (ITEM_LIST_OFFSET + length);
    }
    else
    {
        return 0;
    }
}  // end NormRepairRequest::Pack()


UINT16 NormRepairRequest::Unpack(const UINT32* bufferPtr, UINT16 bufferLen)
{
    buffer = (UINT32*)bufferPtr;
    buffer_len = bufferLen;
    length = 0;
    
    // Make sure there's at least a header
    if (buffer_len >= ITEM_LIST_OFFSET)
    {
        form = (Form)((UINT8*)buffer)[FORM_OFFSET];
        flags = (int)((UINT8*)buffer)[FLAGS_OFFSET];
        length = ntohs(((UINT16*)buffer)[LENGTH_OFFSET]);
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

UINT16 NormRepairRequest::RetrieveRepairItem(UINT8          fecM,
                                             UINT16         offset,
                                             UINT8*         fecId,
                                             NormObjectId*  objectId,
                                             NormBlockId*   blockId,
                                             UINT16*        blockLen,
                                             UINT16*        symbolId) const
{
    if (length >= (offset + FEC_ID_OFFSET + 1))
    {
        const UINT32* ptr = buffer + (ITEM_LIST_OFFSET+offset)/4;
        *fecId = ((UINT8*)ptr)[FEC_ID_OFFSET];
        ASSERT(NormPayloadId::IsValid(*fecId));
        UINT16 itemLength = RepairItemLength(*fecId);
        if (length < (offset + RepairItemLength(*fecId))) return 0; 
        *objectId = ntohs(((UINT16*)ptr)[OBJ_ID_OFFSET]);
        NormPayloadId payloadId(*fecId, fecM, ptr + 1);
        *blockId = payloadId.GetFecBlockId();
        *symbolId = payloadId.GetFecSymbolId();
        *blockLen = payloadId.GetFecBlockLength();
        return itemLength;
    }
    else
    {
        return 0;
    }
}  // end NormRepairRequest::RetrieveRepairItem()

NormRepairRequest::Iterator::Iterator(const NormRepairRequest& theRequest, UINT8 fecId, UINT8 fecM)
 : request(theRequest), fec_id(fecId), fec_m(fecM), offset(0)
{
}

// For erasure requests, symbolId is loaded with erasureCount
UINT16 NormRepairRequest::Iterator::NextRepairItem(NormObjectId* objectId,
                                                   NormBlockId*  blockId,
                                                   UINT16*       blockLen,
                                                   UINT16*       symbolId)
{
    UINT8 fecId;  // for check
    UINT16 itemLength = request.RetrieveRepairItem(fec_m, offset, &fecId, objectId, blockId, blockLen, symbolId);
    if (0 != itemLength)
    {
        if (fecId != fec_id)
        {
            PLOG(PL_ERROR, "NormRepairRequest::Iterator::NextRepairItem() received repair request with wrong fec_id?!\n");
            return false;
        }
        offset += itemLength;
    }
    return itemLength;
}  // end NormRepairRequest::Iterator::NextRepairItem()


// Helper function to log contents of a NormNack (or other message with repair request content)
void LogRepairContent(const UINT32* buffer, UINT16 bufferLen, UINT8 fecId, UINT8 fecM)
{
    NormRepairRequest req;
    UINT16 requestLength = 0;
    while ((requestLength = req.Unpack(buffer, bufferLen)))
    {      
        // Point "buffer" to next request and adjust "bufferLen"
        buffer += (requestLength/4); 
        bufferLen -= requestLength;
        req.Log(fecId, fecM);
    }
}  // end void LogRepairContent()

void NormRepairRequest::Log(UINT8 fecId, UINT8 fecM) const
{
    Iterator iterator(*this, fecId, fecM);
    NormObjectId objId;
    NormBlockId blkId;
    UINT16 blkLen, segId;
    while (iterator.NextRepairItem(&objId, &blkId, &blkLen, &segId))
    {
        if (FlagIsSet(SEGMENT))
            PLOG(PL_ALWAYS, "RepairItem> %hu:%lu:%hu", (UINT16)objId, (unsigned long)blkId.GetValue(), (UINT16)segId);
        else if (FlagIsSet(BLOCK))
            PLOG(PL_ALWAYS, "RepairItem> %hu:%lu", (UINT16)objId, (unsigned long)blkId.GetValue(), (UINT16)segId);
        else
            PLOG(PL_ALWAYS, "RepairItem> %hu", (UINT16)objId);
        if (form == RANGES)
        {
            iterator.NextRepairItem(&objId, &blkId, &blkLen, &segId);
            if (FlagIsSet(SEGMENT))
                PLOG(PL_ALWAYS, " -> %hu:%lu:%hu", (UINT16)objId, (unsigned long)blkId.GetValue(), (UINT16)segId);
            else if (FlagIsSet(BLOCK))
                PLOG(PL_ALWAYS, " -> %hu:%lu", (UINT16)objId, (unsigned long)blkId.GetValue(), (UINT16)segId);
            else
                PLOG(PL_ALWAYS, " -> %hu", (UINT16)objId);
        }
        if (FlagIsSet(INFO))
            PLOG(PL_ALWAYS, " INFO\n");
        else
            PLOG(PL_ALWAYS, "\n");
    }
}  // end NormRepairRequest::Log()

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
UINT8 NormQuantizeRtt(double rtt)
{
    if (rtt > NORM_RTT_MAX)
        rtt = NORM_RTT_MAX;
    else if (rtt < NORM_RTT_MIN)
        rtt = NORM_RTT_MIN;
    if (rtt < 3.3e-05) 
        return ((UINT8)((rtt/NORM_RTT_MIN)) - 1);
    else
        return ((UINT8)(ceil(255.0 - (13.0*log(NORM_RTT_MAX/rtt)))));
}  // end NormQuantizeRtt()


// These are lookup tables to support for efficient NORM field
// conversion for those fields that are quantized values


////////////////////////////////////////////////
// The following table provides a lookup of NORM
// RTT approximation from the 8-bit "rtt" and 
// "grtt" fields of some NORM messages.  The 
// following routine was used to generate
// this table:
//
//  inline double NormUnquantizeRtt(UINT8 qrtt)
//  {
//      return ((qrtt < 31) ? 
//              (((double)(qrtt+1))*(double)NORM_RTT_MIN) :
//              (NORM_RTT_MAX/exp(((double)(255-qrtt))/(double)13.0)));
//  }
//


const double NORM_RTT[256] = 
{
    1.000e-06, 2.000e-06, 3.000e-06, 4.000e-06, 
    5.000e-06, 6.000e-06, 7.000e-06, 8.000e-06, 
    9.000e-06, 1.000e-05, 1.100e-05, 1.200e-05, 
    1.300e-05, 1.400e-05, 1.500e-05, 1.600e-05, 
    1.700e-05, 1.800e-05, 1.900e-05, 2.000e-05, 
    2.100e-05, 2.200e-05, 2.300e-05, 2.400e-05, 
    2.500e-05, 2.600e-05, 2.700e-05, 2.800e-05, 
    2.900e-05, 3.000e-05, 3.100e-05, 3.287e-05, 
    3.550e-05, 3.833e-05, 4.140e-05, 4.471e-05, 
    4.828e-05, 5.215e-05, 5.631e-05, 6.082e-05, 
    6.568e-05, 7.093e-05, 7.660e-05, 8.273e-05, 
    8.934e-05, 9.649e-05, 1.042e-04, 1.125e-04, 
    1.215e-04, 1.313e-04, 1.417e-04, 1.531e-04, 
    1.653e-04, 1.785e-04, 1.928e-04, 2.082e-04, 
    2.249e-04, 2.429e-04, 2.623e-04, 2.833e-04, 
    3.059e-04, 3.304e-04, 3.568e-04, 3.853e-04, 
    4.161e-04, 4.494e-04, 4.853e-04, 5.241e-04, 
    5.660e-04, 6.113e-04, 6.602e-04, 7.130e-04, 
    7.700e-04, 8.315e-04, 8.980e-04, 9.698e-04, 
    1.047e-03, 1.131e-03, 1.222e-03, 1.319e-03, 
    1.425e-03, 1.539e-03, 1.662e-03, 1.795e-03, 
    1.938e-03, 2.093e-03, 2.260e-03, 2.441e-03, 
    2.636e-03, 2.847e-03, 3.075e-03, 3.321e-03, 
    3.586e-03, 3.873e-03, 4.182e-03, 4.517e-03, 
    4.878e-03, 5.268e-03, 5.689e-03, 6.144e-03, 
    6.635e-03, 7.166e-03, 7.739e-03, 8.358e-03, 
    9.026e-03, 9.748e-03, 1.053e-02, 1.137e-02, 
    1.228e-02, 1.326e-02, 1.432e-02, 1.547e-02, 
    1.670e-02, 1.804e-02, 1.948e-02, 2.104e-02, 
    2.272e-02, 2.454e-02, 2.650e-02, 2.862e-02, 
    3.090e-02, 3.338e-02, 3.604e-02, 3.893e-02, 
    4.204e-02, 4.540e-02, 4.903e-02, 5.295e-02, 
    5.718e-02, 6.176e-02, 6.669e-02, 7.203e-02, 
    7.779e-02, 8.401e-02, 9.072e-02, 9.798e-02, 
    1.058e-01, 1.143e-01, 1.234e-01, 1.333e-01, 
    1.439e-01, 1.554e-01, 1.679e-01, 1.813e-01, 
    1.958e-01, 2.114e-01, 2.284e-01, 2.466e-01, 
    2.663e-01, 2.876e-01, 3.106e-01, 3.355e-01, 
    3.623e-01, 3.913e-01, 4.225e-01, 4.563e-01, 
    4.928e-01, 5.322e-01, 5.748e-01, 6.207e-01, 
    6.704e-01, 7.240e-01, 7.819e-01, 8.444e-01, 
    9.119e-01, 9.848e-01, 1.064e+00, 1.149e+00, 
    1.240e+00, 1.340e+00, 1.447e+00, 1.562e+00, 
    1.687e+00, 1.822e+00, 1.968e+00, 2.125e+00, 
    2.295e+00, 2.479e+00, 2.677e+00, 2.891e+00, 
    3.122e+00, 3.372e+00, 3.641e+00, 3.933e+00, 
    4.247e+00, 4.587e+00, 4.953e+00, 5.349e+00, 
    5.777e+00, 6.239e+00, 6.738e+00, 7.277e+00, 
    7.859e+00, 8.487e+00, 9.166e+00, 9.898e+00, 
    1.069e+01, 1.154e+01, 1.247e+01, 1.346e+01, 
    1.454e+01, 1.570e+01, 1.696e+01, 1.832e+01, 
    1.978e+01, 2.136e+01, 2.307e+01, 2.491e+01, 
    2.691e+01, 2.906e+01, 3.138e+01, 3.389e+01, 
    3.660e+01, 3.953e+01, 4.269e+01, 4.610e+01, 
    4.979e+01, 5.377e+01, 5.807e+01, 6.271e+01, 
    6.772e+01, 7.314e+01, 7.899e+01, 8.530e+01, 
    9.212e+01, 9.949e+01, 1.074e+02, 1.160e+02, 
    1.253e+02, 1.353e+02, 1.462e+02, 1.578e+02, 
    1.705e+02, 1.841e+02, 1.988e+02, 2.147e+02, 
    2.319e+02, 2.504e+02, 2.704e+02, 2.921e+02, 
    3.154e+02, 3.406e+02, 3.679e+02, 3.973e+02, 
    4.291e+02, 4.634e+02, 5.004e+02, 5.404e+02, 
    5.836e+02, 6.303e+02, 6.807e+02, 7.351e+02, 
    7.939e+02, 8.574e+02, 9.260e+02, 1.000e+03
};


////////////////////////////////////////////////
// The following tables is provides a lookup 
// of NORM group size approximation from the
// 4-bit "gsize" field of some NORM messages
// The following routine was used to generate
// this table
//
//  inline double NormUnquantizeGroupSize(UINT8 gsize)
//  {
//      double exponent = (double)((gsize & 0x07) + 1);
//      double mantissa = (0 != (gsize & 0x08)) ? 5.0 : 1.0;
//      return (mantissa * pow(10.0, exponent)); 
//  }
//

const double NORM_GSIZE[16] = 
{
    1.000e+01, 1.000e+02, 1.000e+03, 1.000e+04, 
    1.000e+05, 1.000e+06, 1.000e+07, 1.000e+08, 
    5.000e+01, 5.000e+02, 5.000e+03, 5.000e+04, 
    5.000e+05, 5.000e+06, 5.000e+07, 5.000e+08
};
    
// This function rounds up the "gsize" to be conservative
UINT8 NormQuantizeGroupSize(double gsize)
{
    UINT8 exponent = (int)log10(gsize);
    if (exponent > 8)
    {
        return 0x0f;                    // = 0x08 + 0x07
    }
    else if (exponent >= 1)
    {
        UINT8 mantissa = (int)ceil(gsize / pow(10.0, exponent));
        if (mantissa > 5)
        {
            if (exponent > 7)
                return 0x0f;            // = 0x08 + 0x07
            else
                return exponent;        // = 0x00 + exponent;
        }
        else if (mantissa > 1)
        {
            return (exponent + 0x07);   // = 0x08 + exponent - 1
        }
        else
        {
            return (exponent - 1);      // = 0x00 + exponent - 1;
        }
    }
    else
    {
        return 0x00;  // "gsize" < 10      = 0x00 + 0x00
    }
}  // end NormQuantizeGroupSize()



