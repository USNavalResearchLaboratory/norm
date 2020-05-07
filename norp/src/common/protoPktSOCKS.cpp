#include "protoPktSOCKS.h"
#include "protoDebug.h"

ProtoPktSOCKS::AuthRequest::AuthRequest(UINT32* bufferPtr, unsigned int numBytes, bool initFromBuffer, bool freeOnDestruct)
 : ProtoPkt(bufferPtr, numBytes, freeOnDestruct)
{    
    if (initFromBuffer) InitFromBuffer(numBytes);
}

ProtoPktSOCKS::AuthRequest::~AuthRequest()
{
}

ProtoPktSOCKS::AuthReply::AuthReply(UINT32* bufferPtr, unsigned int numBytes, bool initFromBuffer, bool freeOnDestruct)
 : ProtoPkt(bufferPtr, numBytes, freeOnDestruct)
{    
    if (initFromBuffer) InitFromBuffer(numBytes);
}

ProtoPktSOCKS::AuthReply::~AuthReply()
{
}

ProtoPktSOCKS::Request::Request(UINT32* bufferPtr, unsigned int numBytes, bool initFromBuffer, bool freeOnDestruct)
 : ProtoPkt(bufferPtr, numBytes, freeOnDestruct)
{   
    if (initFromBuffer) InitFromBuffer(numBytes); 
}

ProtoPktSOCKS::Request::~Request()
{
}

UINT8 ProtoPktSOCKS::Request::GetAddressLength() const
{
    switch (GetAddressType())
    {
        case IPv4:
            return 4;
        case NAME:
            // First byte of "name" is length value
            return ((UINT8*)buffer_ptr)[OFFSET_ADDR];
        case IPv6:
            return 16;
        default:
            return 0;
    }
}  // end ProtoPktSOCKS::Request::GetAddressLength()

UINT16 ProtoPktSOCKS::Request::GetPort() const
{
    const char* portPtr = GetAddressPtr() + GetAddressLength();
    UINT16 temp16;
    memcpy(&temp16, portPtr, 2);
    return ntohs(temp16);
}  // end ProtoPktSOCKS::Request::GetPort()


bool ProtoPktSOCKS::Request::GetAddress(ProtoAddress& theAddr) const
{
    switch (GetAddressType())
    {
        case IPv4:
        {
            theAddr.SetRawHostAddress(ProtoAddress::IPv4, GetAddressPtr(), 4);
            break;
        }
        case NAME:
        {    
            char name[256];
            UINT8 addrLen = GetAddressLength();
            strncpy(name, GetAddressPtr(), addrLen);   
            name[addrLen] = '\0';
            if (!theAddr.ResolveFromString(name))
            {
                PLOG(PL_ERROR, "ProtoPktSOCKS::Request::GetAddress() error: unable to resolve name \"%s\" to addr!\n", name);
                return false;
            }            break;
        }
        case IPv6:
       {
            theAddr.SetRawHostAddress(ProtoAddress::IPv6, GetAddressPtr(), 16);
            break;
        }
        default:
        {
            PLOG(PL_ERROR, "ProtoPktSOCKS::Request::GetAddress() error: invalid address type!\n");
            return false;
        }
    }
    theAddr.SetPort(GetPort());
    return true;
}  // end ProtoPktSOCKS::Request::GetAddress()

bool ProtoPktSOCKS::Request::SetAddress(const ProtoAddress& theAddr)
{
    AddrType addrType;
    UINT8 addrLength;
    switch (theAddr.GetType())
    {
        case ProtoAddress::IPv4:
            addrType = IPv4;
            addrLength = 4;
            break;
        case ProtoAddress::IPv6:
            addrType = IPv6;
            addrLength = 16;
            break;
        default:
            PLOG(PL_ERROR, "ProtoPktSOCKS::Request::SetAddress() error: invalid address type!\n");
            return false;
    }
    SetAddressType(addrType);
    memcpy(AccessAddressPtr(), theAddr.GetRawHostAddress(), addrLength);
    pkt_length = OFFSET_ADDR + addrLength;
    
    SetPort(theAddr.GetPort());
    
    return true;
}  // end ProtoPktSOCKS::Request::SetAddress()

bool ProtoPktSOCKS::Request::SetPort(UINT16 thePort)
{
    char* portPtr = AccessAddressPtr() + GetAddressLength();
    UINT16 temp16 = htons(thePort);
    memcpy(portPtr, (char*)&temp16, 2);
    pkt_length = OFFSET_ADDR + GetAddressLength() + 2;
    return true;
}  // end ProtoPktSOCKS::Request::SetPort()


bool ProtoPktSOCKS::Request::SetName(const char* theName)
{
    SetAddressType(NAME);
    unsigned int nameLength = strlen(theName);
    // TBD - validate there is sufficient buffer space
    memcpy(AccessAddressPtr(), theName, nameLength);
    pkt_length += nameLength;
    return true;
}  // end ProtoPktSOCKS::Request::SetAddress()
            
ProtoPktSOCKS::Reply::Reply(UINT32* bufferPtr, unsigned int numBytes, bool initFromBuffer, bool freeOnDestruct)
 : ProtoPkt(bufferPtr, numBytes, freeOnDestruct)
{    
    if (initFromBuffer) InitFromBuffer(numBytes);
}

ProtoPktSOCKS::Reply::~Reply()
{
}

UINT8 ProtoPktSOCKS::Reply::GetAddressLength() const
{
    switch (GetAddressType())
    {
        case IPv4:
            return 4;
        case NAME:
            // First byte of "name" is length value
            return ((UINT8*)buffer_ptr)[OFFSET_ADDR];
        case IPv6:
            return 16;
        default:
            return 0;
    }
}  // end ProtoPktSOCKS::Reply::GetAddressLength()

UINT16 ProtoPktSOCKS::Reply::GetPort() const
{
    const char* portPtr = GetAddressPtr() + GetAddressLength();
    UINT16 temp16;
    memcpy(&temp16, portPtr, 2);
    return ntohs(temp16);
}  // end ProtoPktSOCKS::Reply::GetPort()

bool ProtoPktSOCKS::Reply::GetAddress(ProtoAddress& theAddr) const
{
    switch (GetAddressType())
    {
        case IPv4:
        {
            theAddr.SetRawHostAddress(ProtoAddress::IPv4, GetAddressPtr(), 4);
            break;
        }
        case NAME:
        {    
            char name[256];
            UINT8 addrLen = GetAddressLength();
            strncpy(name, GetAddressPtr(), addrLen);   
            name[addrLen] = '\0';
            if (!theAddr.ResolveFromString(name))
            {
                PLOG(PL_ERROR, "ProtoPktSOCKS::Reply::GetAddress() error: unable to resolve name \"%s\" to addr!\n", name);
                return false;
            }            break;
        }
        case IPv6:
       {
            theAddr.SetRawHostAddress(ProtoAddress::IPv6, GetAddressPtr(), 16);
            break;
        }
        default:
        {
            PLOG(PL_ERROR, "ProtoPktSOCKS::Reply::GetAddress() error: invalid address type!\n");
            return false;
        }
    }
    theAddr.SetPort(GetPort());
    return true;
}  // end ProtoPktSOCKS::Reply::GetAddress()

void ProtoPktSOCKS::Reply::SetAddress(AddrType addrType, const char* addrPtr, UINT8 addrLen)
{
    switch (addrType)
    {
        case ProtoPktSOCKS::IPv4:
            ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)ProtoPktSOCKS::IPv4;
            memcpy(((char*)buffer_ptr) + OFFSET_ADDR, addrPtr, 4);
            pkt_length = OFFSET_ADDR + 4;
            break;
        case ProtoPktSOCKS::NAME:
            ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)ProtoPktSOCKS::NAME;
            ((UINT8*)buffer_ptr)[OFFSET_ADDR] = (UINT8)addrLen;
            memcpy(((char*)buffer_ptr) + OFFSET_ADDR + 1, addrPtr, addrLen);
            pkt_length = OFFSET_ADDR + 1 + addrLen;
            break;
        case ProtoPktSOCKS::IPv6:
            ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)ProtoPktSOCKS::IPv6;
            memcpy(((char*)buffer_ptr) + OFFSET_ADDR, addrPtr, 16);
            pkt_length = OFFSET_ADDR + 16;
            break;
        default:
            PLOG(PL_ERROR, "ProtoPktSOCKS::Reply::SetAddress() invalid SOCKS address type\n");
            break;    
    }
}  // end ProtoPktSOCKS::Reply::SetAddress()

void ProtoPktSOCKS::Reply::SetPort(UINT16 thePort)
{
    UINT16 temp16 = htons(thePort);
    char* portPtr = AccessAddressPtr() + GetAddressLength();
    memcpy(portPtr, &temp16, 2);
    pkt_length = (GetAddressPtr() + GetAddressLength() + 2 - (char*)buffer_ptr);
}  // end ProtoPktSOCKS::Reply::SetPort()

void ProtoPktSOCKS::Reply::SetAddress(const ProtoAddress& theAddr)
{
    switch (theAddr.GetType())
    {
        case ProtoAddress::IPv4:
            SetAddress(ProtoPktSOCKS::IPv4, theAddr.GetRawHostAddress(), 4);
            SetPort(theAddr.GetPort());
            break;
        case ProtoAddress::IPv6:
            SetAddress(ProtoPktSOCKS::IPv6, theAddr.GetRawHostAddress(), 16);
            SetPort(theAddr.GetPort());
            break;
        default:
            PLOG(PL_ERROR, "ProtoPktSOCKS::Reply::SetAddress() invalid ProtoAddress type\n");
            break;
    }
}  // end ProtoPktSOCKS::Reply::SetAddress()

ProtoPktSOCKS::UdpRequest::UdpRequest(UINT32* bufferPtr, unsigned int numBytes, bool initFromBuffer, bool freeOnDestruct)
 : ProtoPkt(bufferPtr, numBytes, freeOnDestruct)
{    
    if (initFromBuffer) InitFromBuffer(numBytes);
}

ProtoPktSOCKS::UdpRequest::~UdpRequest()
{
}

UINT8 ProtoPktSOCKS::UdpRequest::GetAddressLength() const
{
    switch (GetAddressType())
    {
        case IPv4:
            return 4;
        case NAME:
            // First byte of "name" is length value
            return ((UINT8*)buffer_ptr)[OFFSET_ADDR];
        case IPv6:
            return 16;
        default:
            return 0;
    }
}  // end ProtoPktSOCKS::UdpRequest::GetAddressLength()

UINT16 ProtoPktSOCKS::UdpRequest::GetPort() const
{
    const char* portPtr = GetAddressPtr() + GetAddressLength();
    UINT16 temp16;
    memcpy(&temp16, portPtr, 2);
    return ntohs(temp16);
}  // end ProtoPktSOCKS::UdpRequest::GetPort()


bool ProtoPktSOCKS::UdpRequest::GetAddress(ProtoAddress& theAddr) const
{
    switch (GetAddressType())
    {
        case IPv4:
        {
            theAddr.SetRawHostAddress(ProtoAddress::IPv4, GetAddressPtr(), 4);
            break;
        }
        case NAME:
        {    
            char name[256];
            UINT8 addrLen = GetAddressLength();
            strncpy(name, GetAddressPtr(), addrLen);   
            name[addrLen] = '\0';
            if (!theAddr.ResolveFromString(name))
            {
                PLOG(PL_ERROR, "ProtoPktSOCKS::UdpRequest::GetAddress() error: unable to resolve name \"%s\" to addr!\n", name);
                return false;
            }            
            break;
        }
        case IPv6:
       {
            theAddr.SetRawHostAddress(ProtoAddress::IPv6, GetAddressPtr(), 16);
            break;
        }
        default:
        {
            PLOG(PL_ERROR, "ProtoPktSOCKS::UdpRequest::GetAddress() error: invalid address type!\n");
            return false;
        }
    }
    theAddr.SetPort(GetPort());
    return true;
}  // end ProtoPktSOCKS::UdpRequest::GetAddress()


void ProtoPktSOCKS::UdpRequest::SetAddress(AddrType addrType, const char* addrPtr, UINT8 addrLen)
{
    switch (addrType)
    {
        case ProtoPktSOCKS::IPv4:
            ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)ProtoPktSOCKS::IPv4;
            memcpy(((char*)buffer_ptr) + OFFSET_ADDR, addrPtr, 4);
            pkt_length = OFFSET_ADDR + 4;
            break;
        case ProtoPktSOCKS::NAME:
            ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)ProtoPktSOCKS::NAME;
            ((UINT8*)buffer_ptr)[OFFSET_ADDR] = (UINT8)addrLen;
            memcpy(((char*)buffer_ptr) + OFFSET_ADDR + 1, addrPtr, addrLen);
            pkt_length = OFFSET_ADDR + 1 + addrLen;
            break;
        case ProtoPktSOCKS::IPv6:
            ((UINT8*)buffer_ptr)[OFFSET_ATYPE] = (UINT8)ProtoPktSOCKS::IPv6;
            memcpy(((char*)buffer_ptr) + OFFSET_ADDR, addrPtr, 16);
            pkt_length = OFFSET_ADDR + 16;
            break;
        default:
            PLOG(PL_ERROR, "ProtoPktSOCKS::UdpRequest::SetAddress() invalid SOCKS address type\n");
            break;    
    }
}  // end ProtoPktSOCKS::UdpRequest::SetAddress()

void ProtoPktSOCKS::UdpRequest::SetPort(UINT16 thePort)
{
    UINT16 temp16 = htons(thePort);
    char* portPtr = AccessAddressPtr() + GetAddressLength();
    memcpy(portPtr, &temp16, 2);
    pkt_length = (GetAddressPtr() + GetAddressLength() + 2 - (char*)buffer_ptr);
}  // end ProtoPktSOCKS::UdpRequest::SetPort()

void ProtoPktSOCKS::UdpRequest::SetAddress(const ProtoAddress& theAddr)
{
    switch (theAddr.GetType())
    {
        case ProtoAddress::IPv4:
            SetAddress(ProtoPktSOCKS::IPv4, theAddr.GetRawHostAddress(), 4);
            SetPort(theAddr.GetPort());
            break;
        case ProtoAddress::IPv6:
            SetAddress(ProtoPktSOCKS::IPv6, theAddr.GetRawHostAddress(), 16);
            SetPort(theAddr.GetPort());
            break;
        default:
            PLOG(PL_ERROR, "ProtoPktSOCKS::UdpRequest::SetAddress() invalid ProtoAddress type\n");
            break;
    }
}  // end ProtoPktSOCKS::UdpRequest::SetAddress()

