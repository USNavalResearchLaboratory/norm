#ifndef _NORM_SESSION
#define _NORM_SESSION

#include "normMessage.h"
#include "normObject.h"
#include "normNode.h"
#include "normEncoder.h"

#include "protocolTimer.h"
#include "udpSocket.h"


class NormController
{
    public:
        enum Event
        {
            TX_QUEUE_EMPTY,
            RX_OBJECT_NEW,
            RX_OBJECT_UPDATE
        };
                  
        virtual void Notify(NormController::Event event,
                            class NormSessionMgr* sessionMgr,
                            class NormSession*    session,
                            class NormServerNode* server,
                            class NormObject*     object) = 0;
                    
};  // end class NormController

class NormSessionMgr
{
    friend class NormSession;
    public:
        NormSessionMgr();
        ~NormSessionMgr();
        void Init(ProtocolTimerInstallFunc* timerInstaller, 
                  const void*               timerInstallData,
                  UdpSocketInstallFunc*     socketInstaller,
                  void*                     socketInstallData,
                  NormController*           theController)
        {
            timer_mgr.SetInstaller(timerInstaller, timerInstallData);
            socket_installer = socketInstaller;
            socket_install_data = socketInstallData;   
            controller = theController;
        }
        void Destroy();
        
        class NormSession* NewSession(const char*   sessionAddress,
                                      UINT16        sessionPort,
                                      NormNodeId    localNodeId = 
                                                    NORM_NODE_ANY);
        
        UdpSocketInstallFunc* SocketInstaller() {return socket_installer;}
        const void* SocketInstallData() {return socket_install_data;}
        
        void Notify(NormController::Event event,
                    class NormSession*    session,
                    class NormServerNode* server,
                    class NormObject*     object)
        {
            if (controller)
                controller->Notify(event, this, session, server, object);   
        }
        
    private:
        void InstallTimer(ProtocolTimer* timer) 
            {timer_mgr.InstallTimer(timer);}
    
        ProtocolTimerMgr        timer_mgr;  
        UdpSocketInstallFunc*   socket_installer;
        const void*             socket_install_data;
        NormController*         controller;
        
        class NormSession*      top_session;
              
};  // end class NormSessionMgr


class NormSession
{
    friend class NormSessionMgr;
    
    public:
        enum {DEFAULT_MESSAGE_POOL_DEPTH = 16};
        static const double DEFAULT_TRANSMIT_RATE;  // in bytes per second
    
        NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId);
        ~NormSession();
        
        // General methods
        const NormNodeId& LocalNodeId() {return local_node_id;}
        bool Open();
        void Close();
        bool IsOpen() {return (rx_socket.IsOpen() || tx_socket.IsOpen());}
        
        void SetAddress(const NetworkAddress& addr) {address = addr;}
        void SetLoopback(bool state)
            {rx_socket.SetLoopback(state);
             tx_socket.SetLoopback(state);}
        void Notify(NormController::Event event,
                    class NormServerNode* server,
                    class NormObject*     object)
        {
            session_mgr.Notify(event, this, server, object);  
        }
        
        // Server methods
        bool StartServer(unsigned long bufferSpace,
                         UINT16        segmentSize,
                         UINT16        numData,
                         UINT16        numParity);
        void StopServer();
        bool IsServer() {return is_server;}
        UINT16 ServerSegmentSize() {return segment_size;}
        UINT16 ServerBlockSize() {return ndata;}
        UINT16 ServerNumParity() {return nparity;}
        UINT16 ServerAutoParity() {return auto_parity;}
        void ServerSetAutoParity(UINT16 autoParity)
            {ASSERT(autoParity <= nparity); auto_parity = autoParity;}
        
        void ServerEncode(const char* segment, char** parityVectorList)
            {encoder.Encode(segment, parityVectorList);}
        
        NormStreamObject* QueueTxStream(UINT32      bufferSize, 
                                        const char* infoPtr = NULL, 
                                        UINT16      infoLen = 0);
        NormBlock* ServerGetFreeBlock(NormObjectId objectId, NormBlockId blockId);
        char* ServerGetFreeSegment(NormObjectId objectId, NormBlockId blockId);
        void ServerPutFreeBlock(NormBlock* block)
        {
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);
        }
        void TouchServer() 
        {
            posted_tx_queue_empty = false;
            Serve();
        }
        
        // Client methods
        bool StartClient(unsigned long bufferSpace);
        void StopClient();
        bool IsClient() {return is_client;}
        unsigned long RemoteServerBufferSize() 
            {return remote_server_buffer_size;}
            
    private:
        void QueueMessage(NormMessage* msg);
        void Serve();
        bool QueueTxObject(NormObject* obj, bool touchServer = true);
    
        void InstallTimer(ProtocolTimer* timer) 
            {session_mgr.InstallTimer(timer);}
    
        bool OnTxTimeout();
    
        bool TxSocketRecvHandler(UdpSocket* theSocket);
        bool RxSocketRecvHandler(UdpSocket* theSocket);
        void HandleReceiveMessage(NormMessage& msg, bool wasUnicast);
        
        void ClientHandleObjectMessage(NormMessage& msg);
        
        NormSessionMgr&     session_mgr;
        ProtocolTimer       tx_timer;
        UdpSocket           tx_socket;
        UdpSocket           rx_socket;
        NormMessageQueue    message_queue;
        NormMessageQueue    message_pool;
        
        // General session parameters
        NormNodeId          local_node_id;
        NetworkAddress      address;  // session destination address
        UINT8               ttl;      // session multicast ttl
        
        UINT16              tx_sequence;
        double              tx_rate;  // bytes per second
        
        // Server parameters
        bool                is_server;
        UINT16              segment_size;
        UINT16              ndata;
        UINT16              nparity;
        UINT16              auto_parity;
        
        NormObjectTable     tx_table;
        NormSlidingMask     tx_pending_mask;
        NormSlidingMask     tx_repair_mask;
        NormBlockPool       block_pool;
        NormSegmentPool     segment_pool;
        NormEncoder         encoder;
        
        NormObjectId        current_tx_object_id;
        int                 flush_count;
        bool                posted_tx_queue_empty;
        
        // Client parameters
        bool                is_client;
        NormNodeTree        server_tree;
        unsigned long       remote_server_buffer_size;

        // Misc
        NormSession*        next;
};  // end class NormSession


#endif  // _NORM_SESSION
