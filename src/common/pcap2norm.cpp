
// Assumes UDP packets in tcpdump trace file (pcap file) are
// MGEN packets and parses to build an MGEN log file

#include <stdio.h>
#include <pcap.h>
#include <sys/socket.h>  // for PF_ types (protocol family)
#include "protoPktETH.h" // for Ethernet frame parsing
#include "protoPktIP.h"  // for IP packet parsing
#include "protoPktARP.h"

#include "normSession.h"

void NormTrace2(const struct timeval &currentTime,
                const NormMsg &msg,
                const ProtoAddress &srcAddr,
                const ProtoAddress &dstAddr);
void Usage()
{
    fprintf(stderr, "pcap2norm [pcapInputFile [outputFile]]\n");
}

int main(int argc, char *argv[])
{
    // Use stdin/stdout by default
    FILE *infile = stdin;
    FILE *outfile = stdout;
    switch (argc)
    {
    case 1:
        // using default stdin/stdout
        break;
    case 2:
        // using named input pcap file and stdout
        if (NULL == (infile = fopen(argv[1], "r")))
        {
            perror("pcap2norm: error opening input file");
            return -1;
        }
        break;
    case 3:
        // use name input and output files
        if (NULL == (infile = fopen(argv[1], "r")))
        {
            perror("pcap2norm: error opening input file");
            return -1;
        }
        if (NULL == (outfile = fopen(argv[2], "w+")))
        {
            perror("pcap2norm: error opening output file");
            return -1;
        }
        break;
    default:
        fprintf(stderr, "pcap2norm: error: too many arguments!\n");
        Usage();
        return -1;
    } // end switch(argc)

    char pcapErrBuf[PCAP_ERRBUF_SIZE + 1];
    pcapErrBuf[PCAP_ERRBUF_SIZE] = '\0';
    pcap_t *pcapDevice = pcap_fopen_offline(infile, pcapErrBuf);
    if (NULL == pcapDevice)
    {
        fprintf(stderr, "pcap2norm: pcap_fopen_offline() error: %s\n", pcapErrBuf);
        if (stdin != infile)
            fclose(infile);
        if (stdout != outfile)
            fclose(outfile);
        return -1;
    }

    int deviceType = pcap_datalink(pcapDevice);

    UINT32 alignedBuffer[4096 / 4]; // 4096 byte buffer for packet parsing
    UINT16 *ethBuffer = ((UINT16 *)alignedBuffer) + 1;
    unsigned int maxBytes = 4096 - 2; // due to offset, can only use 4094 bytes of buffer

    pcap_pkthdr hdr;
    const u_char *pktData;
    while (NULL != (pktData = pcap_next(pcapDevice, &hdr)))
    {
        unsigned int numBytes = maxBytes;
        if (hdr.caplen < numBytes)
            numBytes = hdr.caplen;
        ProtoPktETH::Type ethType;
        unsigned int payloadLength;
        UINT32 *payloadPtr;
        if (DLT_NULL == deviceType)
        {
            // pcap was captured from "loopback" device
            memcpy(alignedBuffer, pktData, numBytes);
            switch (alignedBuffer[0])
            {
            case PF_INET:
                ethType = ProtoPktETH::IP;
                break;
            case PF_INET6:
                ethType = ProtoPktETH::IPv6;
                break;
            default:
                continue; // not an IP packet
            }
            payloadLength = numBytes - 4;
            payloadPtr = alignedBuffer + 1;
        }
        else
        {
            memcpy(ethBuffer, pktData, numBytes);
            ProtoPktETH ethPkt(ethBuffer, maxBytes);
            if (!ethPkt.InitFromBuffer(hdr.len))
            {
                fprintf(stderr, "pcap2norm error: invalid Ether frame in pcap file\n");
                continue;
            }
            ethType = ethPkt.GetType();
            payloadLength = ethPkt.GetPayloadLength();
            // This is done know we offset the ethBuffer above
            payloadPtr = alignedBuffer + (2 + ethPkt.GetLength() - ethPkt.GetPayloadLength()) / 4;
            //payloadPtr = (UINT32*)ethPkt.AccessPayload();
        }

        ProtoPktIP ipPkt;
        ProtoAddress srcAddr, dstAddr;
        if ((ProtoPktETH::IP == ethType) ||
            (ProtoPktETH::IPv6 == ethType))
        {
            if (!ipPkt.InitFromBuffer(payloadLength, payloadPtr, payloadLength))
            {
                fprintf(stderr, "pcap2norm error: bad IP packet\n");
                continue;
            }
            switch (ipPkt.GetVersion())
            {
            case 4:
            {
                ProtoPktIPv4 ip4Pkt(ipPkt);
                ip4Pkt.GetDstAddr(dstAddr);
                ip4Pkt.GetSrcAddr(srcAddr);
                break;
            }
            case 6:
            {
                ProtoPktIPv6 ip6Pkt(ipPkt);
                ip6Pkt.GetDstAddr(dstAddr);
                ip6Pkt.GetSrcAddr(srcAddr);
                break;
            }
            default:
            {
                PLOG(PL_ERROR, "pcap2norm Error: Invalid IP pkt version.\n");
                break;
            }
            }
            //PLOG(PL_ALWAYS, "pcap2norm IP packet dst>%s ", dstAddr.GetHostString());
            //PLOG(PL_ALWAYS," src>%s length>%d\n", srcAddr.GetHostString(), ipPkt.GetLength());
        }
        else
        {
            fprintf(stderr, "eth type = %d\n", ethType);
        }
        if (!srcAddr.IsValid())
            continue; // wasn't an IP packet

        ProtoPktUDP udpPkt;
        if (!udpPkt.InitFromPacket(ipPkt))
            continue; // not a UDP packet

        NormMsg msg;
        if (msg.CopyFromBuffer((const char *)udpPkt.GetPayload(), udpPkt.GetPayloadLength()))
        {
            srcAddr.SetPort(udpPkt.GetSrcPort());
            msg.AccessAddress() = srcAddr;
            dstAddr.SetPort(udpPkt.GetDstPort());
            NormTrace2(hdr.ts, msg, srcAddr, dstAddr);
        }
        else
        {
            fprintf(stderr, "pcap2norm warning: UDP packet not an MGEN packet?\n");
        }
    } // end while (pcap_next())

} // end main()

static UINT8 lastFecId = 0;

void NormTrace2(const struct timeval &currentTime,
                const NormMsg &msg,
                const ProtoAddress &srcAddr,
                const ProtoAddress &dstAddr)
{

    UINT8 fecM = 8; // NOTE - this assumes 16-bit RS code for fec_id == 2

    static const char *MSG_NAME[] =
        {
            "INVALID",
            "INFO",
            "DATA",
            "CMD",
            "NACK",
            "ACK",
            "REPORT"};
    static const char *CMD_NAME[] =
        {
            "CMD(INVALID)",
            "CMD(FLUSH)",
            "CMD(EOT)",
            "CMD(SQUELCH)",
            "CMD(CC)",
            "CMD(REPAIR_ADV)",
            "CMD(ACK_REQ)",
            "CMD(APP)"};
    static const char *REQ_NAME[] =
        {
            "INVALID",
            "WATERMARK",
            "RTT",
            "APP"};

    NormMsg::Type msgType = msg.GetType();
    UINT16 length = msg.GetLength();
    UINT16 seq = msg.GetSequence();
    char src[64], dst[64];
    src[63] = dst[63] = '\0';
    srcAddr.GetHostString(src, 63);
    dstAddr.GetHostString(dst, 63);

#ifdef _WIN32_WCE
    struct tm timeStruct;
    timeStruct.tm_hour = currentTime.tv_sec / 3600;
    unsigned long hourSecs = 3600 * timeStruct.tm_hour;
    timeStruct.tm_min = (currentTime.tv_sec - (hourSecs)) / 60;
    timeStruct.tm_sec = currentTime.tv_sec - (hourSecs) - (60 * timeStruct.tm_min);
    timeStruct.tm_hour = timeStruct.tm_hour % 24;
    struct tm *ct = &timeStruct;
#else
    time_t secs = (time_t)currentTime.tv_sec;
    struct tm *ct = gmtime(&secs);
#endif // if/else _WIN32_WCE

    PLOG(PL_ALWAYS, "trace>%02d:%02d:%02d.%06lu ",
         (int)ct->tm_hour, (int)ct->tm_min, (int)ct->tm_sec, (unsigned int)currentTime.tv_usec);
    PLOG(PL_ALWAYS, "src>%s/%hu dst>%s/%hu id>0x%08x ", src, srcAddr.GetPort(), dst, dstAddr.GetPort(), (UINT32)msg.GetSourceId());

    bool clrFlag = false;
    switch (msgType)
    {
    case NormMsg::INFO:
    {
        const NormInfoMsg &info = (const NormInfoMsg &)msg;
        lastFecId = info.GetFecId();
        PLOG(PL_ALWAYS, "inst>%hu seq>%hu INFO obj>%hu ",
             info.GetInstanceId(), seq, (UINT16)info.GetObjectId());
        break;
    }
    case NormMsg::DATA:
    {
        const NormDataMsg &data = (const NormDataMsg &)msg;
        lastFecId = data.GetFecId();
        PLOG(PL_ALWAYS, "inst>%hu seq>%hu DATA obj>%hu blk>%u seg>%04hu ",
             data.GetInstanceId(),
             seq,
             //data.IsData() ? "DATA" : "PRTY",
             (UINT16)data.GetObjectId(),
             (UINT32)data.GetFecBlockId(fecM).GetValue(),
             (UINT16)data.GetFecSymbolId(fecM));

        if (data.IsStream())
        {
            UINT32 offset = NormDataMsg::ReadStreamPayloadOffset(data.GetPayload());
            PLOG(PL_ALWAYS, "offset>%lu ", offset);
        }
        /*
            if (data.IsData() && data.IsStream())
            {
                //if (NormDataMsg::StreamPayloadFlagIsSet(data.GetPayload(), NormDataMsg::FLAG_MSG_START))
                UINT16 msgStartOffset = NormDataMsg::ReadStreamPayloadMsgStart(data.GetPayload());
                if (0 != msgStartOffset)
                {
                    PLOG(PL_ALWAYS, "start word>%hu ", msgStartOffset - 1);
                }
                //if (NormDataMsg::StreamPayloadFlagIsSet(data.GetPayload(), NormDataMsg::FLAG_STREAM_END))
                if (0 == NormDataMsg::ReadStreamPayloadLength(data.GetPayload()))
                    PLOG(PL_ALWAYS, "(stream end) ");
            }
            */
        break;
    }
    case NormMsg::CMD:
    {
        const NormCmdMsg &cmd = static_cast<const NormCmdMsg &>(msg);
        NormCmdMsg::Flavor flavor = cmd.GetFlavor();
        PLOG(PL_ALWAYS, "inst>%hu seq>%hu %s ", cmd.GetInstanceId(), seq, CMD_NAME[flavor]);
        switch (flavor)
        {
        case NormCmdMsg::ACK_REQ:
        {
            int index = ((const NormCmdAckReqMsg &)msg).GetAckType();
            index = MIN(index, 3);
            PLOG(PL_ALWAYS, "(%s) ", REQ_NAME[index]);
            break;
        }
        case NormCmdMsg::SQUELCH:
        {
            const NormCmdSquelchMsg &squelch =
                static_cast<const NormCmdSquelchMsg &>(msg);
            PLOG(PL_ALWAYS, " obj>%hu blk>%lu seg>%hu ",
                 (UINT16)squelch.GetObjectId(),
                 (UINT32)squelch.GetFecBlockId(fecM).GetValue(),
                 (UINT16)squelch.GetFecSymbolId(fecM));
            break;
        }
        case NormCmdMsg::FLUSH:
        {
            const NormCmdFlushMsg &flush =
                static_cast<const NormCmdFlushMsg &>(msg);
            PLOG(PL_ALWAYS, " obj>%hu blk>%lu seg>%hu ",
                 (UINT16)flush.GetObjectId(),
                 (UINT32)flush.GetFecBlockId(fecM).GetValue(),
                 (UINT16)flush.GetFecSymbolId(fecM));

            // Print acking node list (if any)
            UINT16 nodeCount = flush.GetAckingNodeCount();
            if (nodeCount > 0)
            {
                PLOG(PL_ALWAYS, "ackers>");
                for (UINT16 i = 0; i < nodeCount; i++)
                {
                    if (i > 0)
                        PLOG(PL_ALWAYS, ",");
                    PLOG(PL_ALWAYS, "0x%08x", (UINT32)flush.GetAckingNodeId(i));
                }
                PLOG(PL_ALWAYS, " ");
            }
            break;
        }
        case NormCmdMsg::CC:
        {
            const NormCmdCCMsg &cc = static_cast<const NormCmdCCMsg &>(msg);
            PLOG(PL_ALWAYS, " seq>%u ", cc.GetCCSequence());
            NormHeaderExtension ext;
            while (cc.GetNextExtension(ext))
            {
                if (NormHeaderExtension::CC_RATE == ext.GetType())
                {
                    UINT16 sendRate = ((NormCCRateExtension &)ext).GetSendRate();
                    PLOG(PL_ALWAYS, " rate>%f ", 8.0e-03 * NormUnquantizeRate(sendRate));
                    break;
                }
            }
            struct timeval sendTime;
            cc.GetSendTime(sendTime);
            double delay = ProtoTime::Delta(ProtoTime(currentTime), ProtoTime(sendTime));
            PLOG(PL_ALWAYS, "delay>%lf ", delay);
            break;
        }
        default:
            break;
        }
        break;
    }

    case NormMsg::ACK:
    case NormMsg::NACK:
    {
        PLOG(PL_ALWAYS, "%s ", MSG_NAME[msgType]);
        // look for NormCCFeedback extension
        NormHeaderExtension ext;
        while (msg.GetNextExtension(ext))
        {
            if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
            {
                clrFlag = ((NormCCFeedbackExtension &)ext).CCFlagIsSet(NormCC::CLR);
                // Print ccRtt (only valid if pcap file is from sender node)
                double ccRtt = NormUnquantizeRtt(((NormCCFeedbackExtension &)ext).GetCCRtt());
                double ccLoss = NormUnquantizeLoss32(((NormCCFeedbackExtension &)ext).GetCCLoss32());
                PLOG(PL_ALWAYS, "ccRtt:%lf ccLoss:%lf ", ccRtt, ccLoss);
                break;
            }
        }
        // Print locally measured rtt (only valid if pcap file is from sender node)
        struct timeval grttResponse;
        if (NormMsg::NACK == msgType)
            static_cast<const NormNackMsg &>(msg).GetGrttResponse(grttResponse);
        else
            static_cast<const NormAckMsg &>(msg).GetGrttResponse(grttResponse);

        double rtt = ProtoTime::Delta(ProtoTime(currentTime), ProtoTime(grttResponse));
        PLOG(PL_ALWAYS, "rtt:%lf ", rtt);

        PLOG(PL_ALWAYS, "len>%hu %s\n", length, clrFlag ? "(CLR)" : "");
        if (NormMsg::NACK == msgType)
        {
            const NormNackMsg &nack = static_cast<const NormNackMsg &>(msg);
            PLOG(PL_ALWAYS, "repair content for sender id 0x%08x)\n", nack.GetSenderId());
            LogRepairContent(nack.GetRepairContent(), nack.GetRepairContentLength(), lastFecId, fecM);
        }
        return;
        break;
    }

    default:
        PLOG(PL_ALWAYS, "%s ", MSG_NAME[msgType]);
        break;
    }
    PLOG(PL_ALWAYS, "len>%hu %s\n", length, clrFlag ? "(CLR)" : "");
} // end NormTrace2();
