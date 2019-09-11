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
            if (((UINT8*)buffer)[NormObjectMsg::FEC_ID_OFFSET] == 129)
            {
                header_length_base = 24;
            }
            else
            {
                DMSG(0, "NormMsg::InitFromBuffer(DATA) unknown fec_id value: %u\n",
                         ((UINT8*)buffer)[NormObjectMsg::FEC_ID_OFFSET]);
                return false;
            }
            break;
        case CMD:
            switch (((UINT8*)buffer)[NormCmdMsg::FLAVOR_OFFSET])
            {
                case NormCmdMsg::FLUSH:
                case NormCmdMsg::SQUELCH:
                    if (((UINT8*)buffer)[NormCmdFlushMsg::FEC_ID_OFFSET] == 129)
                    {
                        header_length_base = 24;
                    }
                    else
                    {
                        DMSG(0, "NormMsg::InitFromBuffer(FLUSH|SQUELCH) unknown fec_id value: %u\n",
                                 ((UINT8*)buffer)[NormCmdFlushMsg::FEC_ID_OFFSET]);
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
                default:
                    DMSG(0, "NormMsg::InitFromBuffer() recv'd unkown cmd flavor:%d\n",
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
            DMSG(0, "NormMsg::InitFromBuffer() invalid message type!\n");
            return false;
    }
    if (msgLength < header_length)
    {
        DMSG(0, "NormMsg::InitFromBuffer() invalid message or header length\n");
        return false;
    }
    else
    {
        length = msgLength;
        return true;
    }
}  // end NormMsg::InitFromBuffer()

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
        UINT32* ptr = buffer + (length + ITEM_LIST_OFFSET)/4;
        ((UINT8*)ptr)[FEC_ID_OFFSET] = (char)129;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)objectId);
        ptr[BLOCK_ID_OFFSET] = htonl((UINT32)blockId);
        ((UINT16*)ptr)[BLOCK_LEN_OFFSET] = htons((UINT16)blockLen);
        ((UINT16*)ptr)[SYMBOL_ID_OFFSET] = htons((UINT16)symbolId);
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
        UINT32* ptr = buffer + (length + ITEM_LIST_OFFSET)/4;
        ((UINT8*)ptr)[FEC_ID_OFFSET] = (char)129;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)startObjectId);
        ptr[BLOCK_ID_OFFSET] = htonl((UINT32)startBlockId);
        ((UINT16*)ptr)[BLOCK_LEN_OFFSET] = htons((UINT16)startBlockLen);
        ((UINT16*)ptr)[SYMBOL_ID_OFFSET] = htons((UINT16)startSymbolId);
        ptr += (RepairItemLength()/4);
        // range end
        ((UINT8*)ptr)[FEC_ID_OFFSET] = (char)129;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)endObjectId);
        ptr[BLOCK_ID_OFFSET] = htonl((UINT32)endBlockId);
        ((UINT16*)ptr)[BLOCK_LEN_OFFSET] = htons((UINT16)endBlockLen);
        ((UINT16*)ptr)[SYMBOL_ID_OFFSET] = htons((UINT16)endSymbolId);
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
        UINT32* ptr = buffer + (length + ITEM_LIST_OFFSET)/4;
        ((UINT8*)ptr)[FEC_ID_OFFSET] = (char)129;
        ((UINT8*)ptr)[RESERVED_OFFSET] = 0;
        ((UINT16*)ptr)[OBJ_ID_OFFSET] = htons((UINT16)objectId);
        ptr[BLOCK_ID_OFFSET] = htonl((UINT32)blockId);
        ((UINT16*)ptr)[BLOCK_LEN_OFFSET] = htons((UINT16)blockLen);
        ((UINT16*)ptr)[SYMBOL_ID_OFFSET] = htons((UINT16)erasureCount);
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

bool NormRepairRequest::RetrieveRepairItem(UINT16        offset,
                                           NormObjectId* objectId,
                                           NormBlockId*  blockId,
                                           UINT16*       blockLen,
                                           UINT16*       symbolId) const
{
    if (length >= (offset + RepairItemLength()))
    {
        const UINT32* ptr = buffer+(ITEM_LIST_OFFSET+offset)/4;
        *objectId = ntohs(((UINT16*)ptr)[OBJ_ID_OFFSET]);
        *blockId = ntohl( ptr[BLOCK_ID_OFFSET]);
        *blockLen = ntohs(((UINT16*)ptr)[BLOCK_LEN_OFFSET]);
        *symbolId = ntohs(((UINT16*)ptr)[SYMBOL_ID_OFFSET]);
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
        return ((unsigned char)((rtt/NORM_RTT_MIN)) - 1);
    else
        return ((unsigned char)(ceil(255.0 - (13.0*log(NORM_RTT_MAX/rtt)))));
}  // end NormQuantizeRtt()

