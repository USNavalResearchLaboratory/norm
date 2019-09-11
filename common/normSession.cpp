#include "normSession.h"
#include <errno.h>

NormSession::NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId) 
 : session_mgr(sessionMgr), local_node_id(localNodeId), 
   tx_rate(DEFAULT_TRANSMIT_RATE),
   is_server(false), flush_count(NORM_ROBUST_FACTOR),
   posted_tx_queue_empty(false),
   is_client(false),
   next(NULL)
{
    tx_socket.Init((UdpSocketOwner*) this, 
                   (UdpSocketRecvHandler)&NormSession::TxSocketRecvHandler,
                   sessionMgr.SocketInstaller(),
                   sessionMgr.SocketInstallData());
    rx_socket.Init((UdpSocketOwner*) this, 
                   (UdpSocketRecvHandler)&NormSession::RxSocketRecvHandler,
                   sessionMgr.SocketInstaller(),
                   sessionMgr.SocketInstallData());
    
    tx_timer.Init(0.0, -1, 
                  (ProtocolTimerOwner *)this, 
                  (ProtocolTimeoutFunc)&NormSession::OnTxTimeout);
}

NormSession::~NormSession()
{
    Close();
}

const double NormSession::DEFAULT_TRANSMIT_RATE = 8000.0;


bool NormSession::Open()
{
    ASSERT(address.IsValid());
    if (!tx_socket.IsOpen())
    {    
        if (UDP_SOCKET_ERROR_NONE != tx_socket.Open())
        {
            DMSG(0, "NormSession::Open() tx_socket open error\n");
            return false;   
        }
    }
    if (!rx_socket.IsOpen())
    {
        if (UDP_SOCKET_ERROR_NONE != rx_socket.Open(address.Port()))
        {
            DMSG(0, "NormSession::Open() rx_socket open error\n");
            Close();
            return false;   
        }
        if (address.IsMulticast())
        {
            if (UDP_SOCKET_ERROR_NONE != rx_socket.JoinGroup(&address, ttl)) 
            {
                DMSG(0, "NormSession::Open() rx_socket join group error\n");
                Close();
                return false;
            }   
        }
    }
    
    for (unsigned int i = 0; i < DEFAULT_MESSAGE_POOL_DEPTH; i++)
    {
        NormMessage* msg = new NormMessage();
        if (msg)
        {
            message_pool.Append(msg);
        }
        else
        {
            DMSG(0, "NormSession::Open() new message error: %s\n", strerror(errno));
            Close();
            return false;
        }   
    }
    return true;
}  // end NormSession::Open()

void NormSession::Close()
{
    if (is_server) StopServer();
    if (is_client) StopClient();
    if (tx_timer.IsActive()) tx_timer.Deactivate();    
    message_queue.Destroy();
    message_pool.Destroy();
    if (tx_socket.IsOpen()) tx_socket.Close();
    if (rx_socket.IsOpen()) rx_socket.Close();
}  // end NormSession::Close()


bool NormSession::StartServer(unsigned long bufferSpace,
                              UINT16        segmentSize,
                              UINT16        numData, 
                              UINT16        numParity)
{
    if (!IsOpen())
    {
        if (!Open()) return false;
    }
    // (TBD) parameterize the object history depth
    if (!tx_table.Init(256))
    {
        DMSG(0, "NormSession::StartServer() tx_table.Init() error!\n");
        StopServer();
        return false;   
    }
    if (!tx_pending_mask.Init(256))
    {
        DMSG(0, "NormSession::StartServer() tx_pending_mask.Init() error!\n");
        StopServer();
        return false; 
    }
    if (!tx_repair_mask.Init(256))
    {
        DMSG(0, "NormSession::StartServer() tx_repair_mask.Init() error!\n");
        StopServer();
        return false; 
    }
    
    // Calculate how much memory each buffered block will require
    UINT16 blockSize = numData + numParity;
    unsigned long maskSize = blockSize >> 3;
    if (0 != (blockSize & 0x07)) maskSize++;
    unsigned long blockSpace = sizeof(NormBlock) + 
                               blockSize * sizeof(char*) + 
                               2*maskSize  +
                               numParity * (segmentSize + NormDataMsg::PayloadHeaderLen());
    
    unsigned long numBlocks = bufferSpace / blockSpace;
    if (bufferSpace > (numBlocks*blockSpace)) numBlocks++;
    if (numBlocks < 2) numBlocks = 2;
    unsigned long numSegments = numBlocks * numParity;
    
    if (!block_pool.Init(numBlocks, blockSize))
    {
        DMSG(0, "NormSession::StartServer() block_pool init error\n");
        StopServer();
        return false;
    }
    
    if (!segment_pool.Init(numSegments, segmentSize + NormDataMsg::PayloadHeaderLen()))
    {
        DMSG(0, "NormSession::StartServer() segment_pool init error\n");
        StopServer();
        return false;
    }
    
    if (numParity)
    {
        if (!encoder.Init(numParity, segmentSize + NormDataMsg::PayloadHeaderLen()))
        {
            DMSG(0, "NormSession::StartServer() encoder init error\n");
            StopServer();
            return false;
        }    
    }
    
    segment_size = segmentSize;
    ndata = numData;
    nparity = numParity;
    is_server = true;
    flush_count = NORM_ROBUST_FACTOR;  // (TBD) parameterize robust_factor
    return true;
}  // end NormSession::StartServer()

void NormSession::StopServer()
{
    encoder.Destroy();
    tx_table.Destroy();
    block_pool.Destroy();
    segment_pool.Destroy();
    tx_repair_mask.Destroy();
    tx_pending_mask.Destroy();
    tx_table.Destroy();
    is_server = false;
    if (!IsClient()) Close();
}   // end NormSession::StopServer()

bool NormSession::StartClient(unsigned long bufferSize)
{
    is_client = true;
    remote_server_buffer_size = bufferSize;
    return true;
}

void NormSession::StopClient()
{    
    server_tree.Destroy();
    is_client = false;
    if (!is_server) Close();
}

void NormSession::Serve()
{
    if (tx_timer.IsActive()) 
    {
        return;
    }
    
    NormObject* obj = NULL;
    // Queue next server message
    if (tx_pending_mask.IsSet())
    {
        NormObjectId objectId(tx_pending_mask.FirstSet());
        obj = tx_table.Find(objectId);
        ASSERT(obj);
        flush_count = 0;
    }
    else
    {
        if (!posted_tx_queue_empty)
        {
            posted_tx_queue_empty = true;
            Notify(NormController::TX_QUEUE_EMPTY,
                   (NormServerNode*)NULL,
                   (NormObject*)NULL);
            // (TBD) Was session deleted?
            Serve();
            return;
        }       
    }
    
    if (obj)
    {
        NormMessage* msg = message_pool.RemoveHead();
        if (msg)
        {
            if (obj->NextServerMsg(msg))
            {
                msg->generic.SetDestination(address);
                QueueMessage(msg);
                if (!obj->IsPending())
                    tx_pending_mask.Unset(obj->Id());
            }
            else
            {
                message_pool.Append(msg);
                if (obj->IsStream())
                {
                    if (!posted_tx_queue_empty)
                    {
                        posted_tx_queue_empty = true;
                        Notify(NormController::TX_QUEUE_EMPTY,
                               (NormServerNode*)NULL, obj);
                        // (TBD) Was session deleted?
                        Serve();
                        return;
                    }      
                }
                else
                {
                    DMSG(0, "NormSession::Serve() pending obj, no message?.\n");
                
                }
            }
        }
        else
        {
            DMSG(0, "NormSession::Serve() Warning! message_pool empty.\n");
        }
    }
    else if (flush_count < NORM_ROBUST_FACTOR)
    {
        // Queue flush message
        flush_count++;
    }   
}  // end NormSession::Serve()

void NormSession::QueueMessage(NormMessage* msg)
{
    if (message_queue.IsEmpty())
    {
        tx_timer.SetInterval(0.0);
        InstallTimer(&tx_timer);   
    }
    message_queue.Append(msg);
}  // end NormSesssion::QueueMessage(NormMessage& msg)

NormStreamObject* NormSession::QueueTxStream(UINT32         bufferSize, 
                                             const char*    infoPtr, 
                                             UINT16         infoLen)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxStream() non-server session!\n");
        return NULL;
    }     
    NormStreamObject* stream = new NormStreamObject(this, NULL, current_tx_object_id);
    if (!stream)
    {
        DMSG(0, "NormSession::QueueTxStream() new stream error!\n");
        return NULL; 
    }
    if (!stream->Open(bufferSize, infoPtr, infoLen))
    {
        DMSG(0, "NormSession::QueueTxStream() stream open error!\n");
        delete stream;
        return NULL;
    }
    if (QueueTxObject(stream, false))
    {
        // (???: stream has nothing pending until user writes to it???)
        //stream->Reset();
        return stream;
    }
    else
    {
        stream->Close();
        delete stream;
        return NULL;
    }
}  // end NormSession::QueueTxStream()


bool NormSession::QueueTxObject(NormObject* obj, bool touchServer)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxObject() non-server session!\n");
        return false;
    }
    // Attempt to queue the object
    if (!tx_table.Insert(obj))
    {
        // (TBD) steal an old non-pending object
        DMSG(0, "NormSession::QueueTxObject() tx_table insert error\n");
        return false;
    }
    tx_pending_mask.Set(obj->Id());
    ASSERT(tx_pending_mask.Test(obj->Id()));
    current_tx_object_id++;
    if (touchServer) TouchServer();
    return true;
}  // end NormSession::QueueTxObject()

NormBlock* NormSession::ServerGetFreeBlock(NormObjectId objectId, 
                                           NormBlockId  blockId)
{
    // First, try to get one from our block pool
    NormBlock* b = block_pool.Get();
    if (!b)
    {
        NormObjectTable::Iterator iterator(tx_table);
        NormObject* obj;
        while ((obj = iterator.GetNextObject()))
        {
            if (obj->Id() > objectId)
            {
                break;
            }
            else
            {
                if (obj->Id() < objectId)
                    b = obj->StealOldestBlock(false);
                else
                    b = obj->StealOldestBlock(true, blockId);
                if (b) 
                {
                    b->EmptyToPool(segment_pool);
                    break;
                }
            }
        }
    }
    return b;
}  // end NormSession::ServerGetFreeBlock()

char* NormSession::ServerGetFreeSegment(NormObjectId objectId, 
                                        NormBlockId  blockId)
{
    while (segment_pool.IsEmpty() && 
           ServerGetFreeBlock(objectId, blockId));
    return segment_pool.Get();
}  // end NormSession::ServerGetFreeSegment()

bool NormSession::TxSocketRecvHandler(UdpSocket* /*theSocket*/)
{
    NormMessage msg;
    unsigned int buflen = NORM_MSG_SIZE_MAX;
    if (UDP_SOCKET_ERROR_NONE == tx_socket.RecvFrom(msg.generic.GetBuffer(), &buflen, msg.generic.Src()))
    {
        msg.generic.SetLength(buflen);
        HandleReceiveMessage(msg, true);
    }
    else
    {
        DMSG(0, "NormSession::TxSocketRecvHandler() recv from error");
    }
    return true;
}  // end NormSession::TxSocketRecvHandler()

bool NormSession::RxSocketRecvHandler(UdpSocket* /*theSocket*/)
{
    NormMessage msg;
    unsigned int buflen = NORM_MSG_SIZE_MAX;
    if (UDP_SOCKET_ERROR_NONE == rx_socket.RecvFrom(msg.generic.GetBuffer(), &buflen, msg.generic.Src()))
    {
        msg.generic.SetLength(buflen);
        HandleReceiveMessage(msg, false);
    }
    else
    {
        DMSG(0, "NormSession::RxSocketRecvHandler() recv from error");
    }
    return true;
}  // end NormSession::RxSocketRecvHandler()

void NormSession::HandleReceiveMessage(NormMessage& msg, bool wasUnicast)
{
    switch (msg.generic.GetType())
    {
        case NORM_MSG_INFO:
            DMSG(0, "NormSession::HandleReceiveMessage(NORM_MSG_INFO)\n");
            break;
        case NORM_MSG_DATA:
            //DMSG(0, "NormSession::HandleReceiveMessage(NORM_MSG_DATA)\n");
            ClientHandleObjectMessage(msg);
            break;
        case NORM_MSG_CMD:
        case NORM_MSG_NACK:
        case NORM_MSG_ACK:
        case NORM_MSG_REPORT: 
        case NORM_MSG_INVALID:
            DMSG(0, "NormSession::HandleReceiveMessage(NORM_MSG_INVALID)\n");
            break;
    }
}  // end NormSession::HandleReceiveMessage()

void NormSession::ClientHandleObjectMessage(NormMessage& msg)
{
    NormNodeId serverId = msg.generic.GetSender();
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(serverId);
    if (!theServer)
    {
        if ((theServer = new NormServerNode(this, serverId)))
        {
            server_tree.AttachNode(theServer);
            DMSG(0, "NormSession::ClientHandleObjectMessage() new remote server:%lu ...\n",
                    serverId);
        }
        else
        {
            DMSG(0, "NormSession::ClientHandleObjectMessage() new server node error\n");
            // (TBD) notify application of error
            return;   
        }   
    }
    theServer->SetAddress(*msg.generic.Src());
    theServer->HandleObjectMessage(msg);
    
}  // end NormSession::ClientHandleObjectMessage()

bool NormSession::OnTxTimeout()
{
    NormMessage* msg = message_queue.RemoveHead();
    if (msg)
    {
        // Fill in common message fields
        msg->generic.SetVersion(1);
        msg->generic.SetSequence(tx_sequence++); 
        msg->generic.SetSender(local_node_id);
        NetworkAddress* dest = msg->generic.Dest();
        // Use session address by default
        if (!dest) dest = &address;   
        UINT16 len = msg->generic.GetLength();
        if (UDP_SOCKET_ERROR_NONE != tx_socket.SendTo(dest, msg->generic.GetBuffer(), len))
        {
            DMSG(0, "NormSession::OnTxTimeout() sendto() error\n");
        }
        tx_timer.SetInterval(((double)len) / tx_rate);
        message_pool.Append(msg);
    }
    else
    {   
        tx_timer.Deactivate();
        if (IsServer()) Serve();
        return false;
    }
    return true;
}  // end NormSession::OnTxTimeout()

NormSessionMgr::NormSessionMgr()
 : socket_installer(NULL), socket_install_data(NULL), 
   controller(NULL), top_session(NULL)
{
}

NormSessionMgr::~NormSessionMgr()
{
    Destroy();
}

void NormSessionMgr::Destroy()
{
    NormSession* next;
    while ((next = top_session))
    {
        top_session = next->next;
        delete next;
    }
}  // end NormSessionMgr::Destroy()

NormSession* NormSessionMgr::NewSession(const char* sessionAddress,
                                        UINT16      sessionPort,
                                        NormNodeId  localNodeId)
{
    if (NORM_NODE_ANY == localNodeId)
    {
        // Use local ip address to assign default localNodeId
        NetworkAddress localAddr;
        if (!localAddr.LookupLocalHostAddress())
        {
            DMSG(0, "NormSessionMgr::NewSession() local address lookup error\n");
            return ((NormSession*)NULL);    
        } 
        // (TBD) test IPv6 "EndIdentifier" ???
        localNodeId = localAddr.EndIdentifier();
    }
    
    
    NetworkAddress a;
    if (!a.LookupHostAddress(sessionAddress))
    {
        DMSG(0, "NormSessionMgr::NewSession() session address lookup error!\n");
        return ((NormSession*)NULL);   
    }
    a.SetPort(sessionPort);
    
    NormSession* s = new NormSession(*this, localNodeId);
    
    if (!s)
    {
        DMSG(0, "NormSessionMgr::NewSession() new session error: %s\n", 
                strerror(errno)); 
        return ((NormSession*)NULL);  
    }
      
    s->SetAddress(a);
    
    // Add new session to our session list
    s->next = top_session;
    top_session = s;
    return s;
}  // end NormSessionMgr::NewSession();

