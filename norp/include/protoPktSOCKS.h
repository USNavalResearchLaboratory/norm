
#ifndef _PROTO_SOCKS
#define _PROTO_SOCKS

#include "protoPkt.h" 
#include "protoAddress.h"

// These classes provide message parsing and building for 
// the RFC 1928 SOCKS5 protocol specification.
// NOTES:
//   1) Only the methods needed for SOCKS server operation are defined here so far
//   2) Additional external helper logic is needed to incrementally read data from a 
//      SOCKS TCP client-server socket for a full SOCKS message.  The message parsing
//      here assumes the message has been read up to the point of whatever field
//      is being requested.

namespace ProtoPktSOCKS
{
    // SOCKS5 message definitions per RFC1928
    
    // SOCKS5 auth request:   VERSION + NMETHODS + METHOD_LIST ...
    //                         1 byte +  1 byte  +   N bytes
    
    // SOCKS5 authentication methods
    enum AuthType
    {
        AUTH_NONE    = 0x00,
        AUTH_GSSAPI  = 0x01,
        AUTH_USER    = 0x02,  // username / password
        AUTH_INVALID = 0xff   // no acceptable method
    };
        
    class AuthRequest : public ProtoPkt
    {
        public:
            AuthRequest(UINT32*       bufferPtr = NULL, 
                        unsigned int  numBytes = 0,
                        bool          initFromBuffer = true, 
                        bool          freeOnDestruct = false);
            ~AuthRequest();
            
            bool InitFromBuffer(unsigned int    reqLength,
                                UINT32*         bufferPtr = NULL,
                                unsigned int    numBytes = 0,
                                bool            freeOnDestruct = false)
                {return ProtoPkt::InitFromBuffer(reqLength, bufferPtr, numBytes, freeOnDestruct);}
            
            UINT8 GetVersion() const
                {return ((pkt_length > OFFSET_VERSION) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_VERSION]) : 0);}
            
            UINT8 GetMethodCount() const
                {return ((pkt_length > OFFSET_NMETHOD) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_NMETHOD]) : 0);}
            
            AuthType GetMethod(UINT8 index) const
                {return ((pkt_length > (unsigned int)(OFFSET_METHODS + index)) ? 
                            (AuthType)(((UINT8*)buffer_ptr)[OFFSET_METHODS + index]) : AUTH_INVALID);}
        
            // TBD - implement auth request building (don't need this for a server)
            
        private:
            enum
            {
                OFFSET_VERSION = 0,                   //  1 byte
                OFFSET_NMETHOD = OFFSET_VERSION + 1,  //  1 byte
                OFFSET_METHODS = OFFSET_NMETHOD + 1   //  N bytes
            };
                    
    };  // end class ProtoPktSOCKS::AuthRequest    
        
    // SOCKS5 auth reply:  VERSION + METHOD_SELECTED
    //             bytes:     1    +       1 
    class AuthReply : public ProtoPkt
    {
        public:
            AuthReply(UINT32*       bufferPtr = NULL, 
                      unsigned int  numBytes = 0,
                      bool          initFromBuffer = true, 
                      bool          freeOnDestruct = false);
            ~AuthReply();
            
            bool InitFromBuffer(unsigned int    replyLength,
                                UINT32*         bufferPtr = NULL,
                                unsigned int    numBytes = 0,
                                bool            freeOnDestruct = false)
                {return ProtoPkt::InitFromBuffer(replyLength, bufferPtr, numBytes, freeOnDestruct);}
            
            UINT8 GetVersion() const
                {return ((pkt_length > OFFSET_VERSION) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_VERSION]) : 0);}
            
            UINT8 GetMethod() const
                {return ((pkt_length > OFFSET_METHOD) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_METHOD]) : AUTH_INVALID);}
            
            void SetVersion(UINT8 version)
            {
                ((UINT8*)buffer_ptr)[OFFSET_VERSION] = version;
                pkt_length = 1;
            }
            
            void SetMethod(AuthType method)
            {
                ((UINT8*)buffer_ptr)[OFFSET_METHOD] = method;
                pkt_length = 2;
            }
            
        private:
            enum
            {
                OFFSET_VERSION = 0,                 // 1 byte
                OFFSET_METHOD = OFFSET_VERSION + 1  // 1 byte
            };
                    
    };  // end class ProtoPktSOCKS::AuthReply 
        
    // SOCKS5 request:   VERSION + REQ + RESV + ADDR_TYP + DST.ADDR + DST.PORT
    //          bytes:      1    +  1  +  1   +   1      +    N     +    2   
    
    // SOCKS5 address types (1 byte)
    enum AddrType
    {
        ADDR_INVALID = 0x00,   // invalid addr type (for implementation)
        IPv4         = 0x01,   // 4-bytes, network byte order
        NAME         = 0x03,   // 1-byte of "name" length, followed by "name"
        IPv6         = 0x04    // 16-bytes, network byte order
    };  // end ProtoPktSOCKS::AddrType
    
    class Request : public ProtoPkt
    {
        public:
            Request(UINT32*       bufferPtr = NULL, 
                    unsigned int  numBytes = 0,
                    bool          initFromBuffer = true, 
                    bool          freeOnDestruct = false);
            ~Request();
            
            enum Command
            {
                CMD_INVALID = 0x00,
                CONNECT     = 0x01,
                BIND        = 0x02,
                UDP_ASSOC   = 0x03  // Setup UDP association
            };  // end ProtoPktSOCKS::Request::Command
            
            bool InitFromBuffer(unsigned int    replyLength,
                                UINT32*         bufferPtr = NULL,
                                unsigned int    numBytes = 0,
                                bool            freeOnDestruct = false)
                {return ProtoPkt::InitFromBuffer(replyLength, bufferPtr, numBytes, freeOnDestruct);}
            
            UINT8 GetVersion() const
                {return ((pkt_length > OFFSET_VERSION) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_VERSION]) : 0);}
            
            Command GetCommand() const
                {return ((pkt_length > OFFSET_COMMAND) ? 
                            (Command)(((UINT8*)buffer_ptr)[OFFSET_COMMAND]) : CMD_INVALID);}
            
            AddrType GetAddressType() const
                {return ((pkt_length > OFFSET_ATYPE) ? 
                            (AddrType)(((UINT8*)buffer_ptr)[OFFSET_ATYPE]) : ADDR_INVALID);}
            
            UINT8 GetAddressLength() const;
            
            const char* GetAddressPtr() const
                {return (const char*)AccessAddressPtr();}
            
            UINT16 GetPort() const;
            
            bool GetAddress(ProtoAddress& theAddr) const;
            
            // TBD - implement request building (don't need this for a server)
            
            void SetVersion(UINT8 version)
            {
                ((UINT8*)buffer_ptr)[OFFSET_VERSION] = version;
                pkt_length = 1;
            }
            void SetCommand(Command cmd)
            {
                ((UINT8*)buffer_ptr)[OFFSET_COMMAND] = (UINT8)cmd;
                pkt_length = 2;
            }
            void SetAddressType(AddrType addrType)
            {
                ((UINT8*)buffer_ptr)[OFFSET_RESERVED] = 0;
                ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)addrType;
                pkt_length = 4;
            }
            bool SetAddress(const ProtoAddress& theAddr);  // note this sets port, too
            bool SetName(const char* theName);
            bool SetPort(UINT16 thePort);
            
        private:
            enum
            {
                OFFSET_VERSION  = 0,                   // 1 byte
                OFFSET_COMMAND  = OFFSET_VERSION + 1,  // 1 byte
                OFFSET_RESERVED = OFFSET_COMMAND + 1,  // 1 byte
                OFFSET_ATYPE    = OFFSET_RESERVED + 1, // 1 byte
                OFFSET_ADDR     = OFFSET_ATYPE + 1     // N bytes
            };
                
            char* AccessAddressPtr() const
            {
                char* addrPtr = ((char*)buffer_ptr) + OFFSET_ADDR;
                return  (NAME == GetAddressType()) ? (addrPtr + 1) : addrPtr;
            }
                    
    };  // end class ProtoPktSOCKS::Request 
    
    // SOCKS5 REPLY:  VERSION + REP + RESV + ATYP + BIND.ADDR + BIND.PORT   
    //        bytes:     1    +  1   + 1   +  1   +     N     +     2
    class Reply : public ProtoPkt
    {
        public:
            Reply(UINT32*       bufferPtr = NULL, 
                  unsigned int  numBytes = 0,
                  bool          initFromBuffer = true, 
                  bool          freeOnDestruct = false); 
            ~Reply();
            
            enum Type
            {
                SUCCESS = 0x00,  // succeeded
                FAILURE = 0x01,  // general failure
                DENIED  = 0x02,  // connection not allowed
                NET_X   = 0x03,  // network unreachable
                HOST_X  = 0x04,  // host unreachable
                REFUSED = 0x05,  // connection refused
                TTL_X   = 0x06,  // TTL expired
                CMD_X   = 0x07,  // command not supported
                ADDR_X  = 0x08,  // address type not supported
                TYPE_INVALID  = 0xff   // invalid reply type        
            };  // end ProtoPktSOCKS::Reply::Type
            
            bool InitFromBuffer(unsigned int    replyLength,
                                UINT32*         bufferPtr = NULL,
                                unsigned int    numBytes = 0,
                                bool            freeOnDestruct = false)
                {return ProtoPkt::InitFromBuffer(replyLength, bufferPtr, numBytes, freeOnDestruct);}
            
            UINT8 GetVersion() const
                {return ((pkt_length > OFFSET_VERSION) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_VERSION]) : 0);}
            
            Type GetType() const
                {return ((pkt_length > OFFSET_TYPE) ? 
                            (Type)(((UINT8*)buffer_ptr)[OFFSET_TYPE]) : TYPE_INVALID);}
            
            AddrType GetAddressType() const
                {return ((pkt_length > OFFSET_ATYPE) ? 
                            (AddrType)(((UINT8*)buffer_ptr)[OFFSET_ATYPE]) : ADDR_INVALID);}
            
            UINT8 GetAddressLength() const;
            
            const char* GetAddressPtr() const
                {return (const char*)AccessAddressPtr();}
            
            UINT16 GetPort() const;
            
            bool GetAddress(ProtoAddress& theAddr) const;
            
            // Reply building (call these in order
            void SetVersion(UINT8 version)
            {
                ((UINT8*)buffer_ptr)[OFFSET_VERSION] = version;
                pkt_length = 1;
            }
            void SetType(Type replyType)
            {
                ((UINT8*)buffer_ptr)[OFFSET_TYPE] = (UINT8)replyType;
                pkt_length = 2;
            }
            void SetAddress(AddrType addrType, const char* addrPtr, UINT8 addrLen);
            void SetPort(UINT16 thePort);
            void SetAddress(const ProtoAddress& theAddr);  // sets address and port from ProtoAddress
            
        private:
            enum
            {
                OFFSET_VERSION  = 0,                   // 1 byte
                OFFSET_TYPE     = OFFSET_VERSION + 1,  // 1 byte
                OFFSET_RESERVED = OFFSET_TYPE + 1,     // 1 byte
                OFFSET_ATYPE    = OFFSET_RESERVED + 1, // 1 byte
                OFFSET_ADDR     = OFFSET_ATYPE + 1     // N bytes
            };
                
            char* AccessAddressPtr() const
            {
                char* addrPtr = ((char*)buffer_ptr) + OFFSET_ADDR;
                return  (NAME == GetAddressType()) ? (addrPtr + 1) : addrPtr;
            }
                    
    };  // end class ProtoPktSOCKS::Reply
        
    // UDP relay header: RESV + FRAG + ATYP + DST.ADDR + DST.PORT + DATA
    //            bytes:  2   +   1      1  +    N     +    2     +  K
    
    class UdpRequest : public ProtoPkt
    {
        public:
            UdpRequest(UINT32*         bufferPtr = NULL, 
                       unsigned int    numBytes = 0,
                       bool            initFromBuffer = true,
                       bool            freeOnDestruct = false); 
            ~UdpRequest();
            
            bool InitFromBuffer(unsigned int    requestLength,
                                UINT32*         bufferPtr = NULL,
                                unsigned int    numBytes = 0,
                                bool            freeOnDestruct = false)
                {return ProtoPkt::InitFromBuffer(requestLength, bufferPtr, numBytes, freeOnDestruct);}
            
            UINT8 GetFrag() const
                {return ((pkt_length > OFFSET_FRAG) ? 
                            (((UINT8*)buffer_ptr)[OFFSET_FRAG]) : 0);}
            
            AddrType GetAddressType() const
                {return ((pkt_length > OFFSET_ATYPE) ? 
                            (AddrType)(((UINT8*)buffer_ptr)[OFFSET_ATYPE]) : ADDR_INVALID);}
            
            UINT8 GetAddressLength() const;
            
            const char* GetAddressPtr() const
                {return (const char*)AccessAddressPtr();}
            
            UINT16 GetPort() const;
            
            bool GetAddress(ProtoAddress& theAddr) const;
            
            const char* GetDataPtr() const
                {return (const char*)AccessDataPtr();}
            
            unsigned int GetDataLength() const
                {return (GetLength() - (GetDataPtr() - ((const char*)buffer_ptr)));}
            
            
            // Packet building (call these in order)
            void SetFragment(UINT8 fragId)
            {
                ((UINT8*)buffer_ptr)[OFFSET_RESERVED] = 0;
                ((UINT8*)buffer_ptr)[OFFSET_FRAG] = fragId;
                pkt_length = OFFSET_FRAG + 1;
            }
            void SetAddress(AddrType addrType, const char* addrPtr, UINT8 addrLen);
            void SetPort(UINT16 thePort);
            void SetAddress(const ProtoAddress& theAddr);  // sets address and port from ProtoAddress
            void SetData(const char* data, unsigned int numBytes)
            {
                memcpy(AccessDataPtr(), data, numBytes);
                SetDataLength(numBytes);
            }
            void SetDataLength(unsigned int numBytes)
                {pkt_length = (GetDataPtr() - ((char*)buffer_ptr)) + numBytes;}
            
        private:
            enum
            {
                OFFSET_RESERVED = 0,                   // 2 bytes
                OFFSET_FRAG    = OFFSET_RESERVED + 2, // 1 byte
                OFFSET_ATYPE   = OFFSET_FRAG + 1,     // 1 byte
                OFFSET_ADDR     = OFFSET_ATYPE + 1    // N bytes
            };
            char* AccessAddressPtr() const
            {
                char* addrPtr = ((char*)buffer_ptr) + OFFSET_ADDR;
                return  (NAME == GetAddressType()) ? (addrPtr + 1) : addrPtr;
            }
            char* AccessDataPtr() const
                {return (AccessAddressPtr() + GetAddressLength() + 2);}
            
    };  // end class ProtoPktSOCKS::UdpRequest
        
};  // end namespace ProtoPktSOCKS

#endif // _PROTO_SOCKS
