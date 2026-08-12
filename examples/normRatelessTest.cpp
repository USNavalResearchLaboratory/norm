#include <normApi.h>
#include <normEncoder.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// The NORM descriptor is a socket on unix but a HANDLE on win32, so poll for events
// instead of select()ing on it -- that keeps this test buildable on every platform.
#ifdef WIN32
#include <windows.h>
static void SleepMsec(unsigned int msec) { Sleep(msec); }
#else
#include <unistd.h>
static void SleepMsec(unsigned int msec) { usleep(msec * 1000); }
#endif // if/else WIN32

// Read an unsigned tunable from the environment, ignoring unset/empty/unparsable
// values so a mistyped override can't silently produce a degenerate run.
static unsigned long EnvULong(const char* name, unsigned long defaultValue)
{
    const char* text = getenv(name);
    if ((NULL == text) || ('\0' == text[0])) return defaultValue;
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if ((NULL == end) || ('\0' != *end) || (0 == value))
    {
        fprintf(stderr, "normRatelessTest warning: ignoring bad %s=\"%s\"\n", name, text);
        return defaultValue;
    }
    return value;
}

// FEC ID for the (partially-specified) rateless code family.
static const UINT8 RATELESS_FEC_ID = 131;

static void PackCustomPayloadId(UINT32* buffer, UINT32 blockId, UINT16 symbolId, UINT16)
{
    UINT16* payloadId = (UINT16*)buffer;
    payloadId[0] = htons((UINT16)blockId);
    payloadId[1] = htons(symbolId);
}

static UINT32 UnpackCustomBlockId(const UINT32* buffer)
{
    return ntohs(((const UINT16*)buffer)[0]);
}

static UINT16 UnpackCustomSymbolId(const UINT32* buffer)
{
    return ntohs(((const UINT16*)buffer)[1]);
}

static UINT16 UnpackCustomBlockLength(const UINT32*) { return 0; }

// Deliberately registers a stack object; the library must copy it.
static bool RegisterCustomLayout(NormInstanceHandle instance, UINT8 fecId)
{
    NormFecLayout layout = {
        4, 0x0000ffff, PackCustomPayloadId, UnpackCustomBlockId,
        UnpackCustomSymbolId, UnpackCustomBlockLength
    };
    return NormRegisterFecLayout(instance, fecId, &layout);
}

// Deterministic permutation of [0, n) shared by encoder and decoder.
// The seed depends only on "n", so both sides produce the identical ordering.
static void BuildRatelessPermutation(unsigned int* perm, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++) perm[i] = i;
    unsigned int state = 0x9e3779b9u ^ n;
    for (unsigned int i = n; i > 1; i--)
    {
        state = state * 1103515245u + 12345u;
        unsigned int j = (state >> 8) % i;
        unsigned int t = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = t;
    }
}

class MockRatelessEncoder : public NormEncoder
{
    public:
        MockRatelessEncoder() : ndata(0), vec_size(0), perm(NULL) {}
        virtual ~MockRatelessEncoder() { Destroy(); }

        virtual bool Init(unsigned int numData, unsigned int /*numParity*/, UINT16 vectorSize)
        {
            Destroy();
            ndata = numData;
            vec_size = vectorSize;
            perm = new unsigned int[ndata ? ndata : 1];
            return (NULL != perm);
        }
        virtual void Destroy() { delete[] perm; perm = NULL; }

        // Block-oriented encode is unused by this rateless surrogate; repair
        // symbols are synthesized on demand in EncodeParity() below.
        virtual void Encode(unsigned int, const char*, char**) {}

        // Generate the "parityId"-th repair symbol as a copy of a source symbol
        // selected by the shared permutation.
        virtual void EncodeParity(unsigned int parityId, const char** sourceVectorList,
                                  unsigned int numData, char* parityVector)
        {
            if (0 == numData) return;
            BuildRatelessPermutation(perm, numData);
            unsigned int k = perm[parityId % numData];
            if ((k < numData) && (NULL != sourceVectorList[k]))
                memcpy(parityVector, sourceVectorList[k], vec_size);
        }

        virtual bool IsRateless() const { return true; }

    private:
        unsigned int  ndata;
        UINT16        vec_size;
        unsigned int* perm;  // scratch buffer sized to the block size
};

class MockRatelessDecoder : public NormDecoder
{
    public:
        MockRatelessDecoder() : ndata(0), nparity(0), vec_size(0), perm(NULL) {}
        virtual ~MockRatelessDecoder() { Destroy(); }

        virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize)
        {
            Destroy();
            ndata = numData;
            nparity = numParity;
            vec_size = vectorSize;
            perm = new unsigned int[ndata ? ndata : 1];
            return (NULL != perm);
        }
        virtual void Destroy() { delete[] perm; perm = NULL; }

        // Fill erased source symbols using received repair symbols. Returns the
        // number of source erasures that could NOT be recovered (0 == success),
        // which NORM uses to decide whether to NACK for more repair symbols.
        virtual int Decode(char** vectorList, unsigned int numData,
                           unsigned int erasureCount, unsigned int* erasureLocs)
        {
            unsigned int total = numData + nparity;
            bool* erased = new bool[total];
            for (unsigned int i = 0; i < total; i++) erased[i] = false;
            for (unsigned int i = 0; i < erasureCount; i++)
                if (erasureLocs[i] < total) erased[erasureLocs[i]] = true;

            unsigned int sourceErasures = 0;
            for (unsigned int i = 0; i < numData; i++)
                if (erased[i]) sourceErasures++;

            // Exercise NORM's explicit-source fallback after every advertised
            // repair symbol has been consumed.
            if (getenv("NORM_FORCE_DECODE_FAILURE") != NULL)
            {
                delete[] erased;
                return (int)sourceErasures;
            }

            BuildRatelessPermutation(perm, numData);

            unsigned int recovered = 0;
            for (unsigned int p = 0; (p < nparity) && (recovered < sourceErasures); p++)
            {
                unsigned int pos = numData + p;
                if (erased[pos] || (NULL == vectorList[pos])) continue;  // repair symbol not received
                unsigned int k = perm[p % numData];  // source symbol this repair carries
                if ((k < numData) && erased[k] && (NULL != vectorList[k]))
                {
                    memcpy(vectorList[k], vectorList[pos], vec_size);
                    erased[k] = false;
                    recovered++;
                }
            }
            unsigned int stillMissing = sourceErasures - recovered;
            delete[] erased;
            return (int)stillMissing;
        }

    private:
        unsigned int  ndata;
        unsigned int  nparity;
        UINT16        vec_size;
        unsigned int* perm;
};

extern "C" {
    NormEncoder* CreateMockEncoder() { return new MockRatelessEncoder(); }
    NormDecoder* CreateMockDecoder() { return new MockRatelessDecoder(); }
}

int main(int /*argc*/, char* /*argv*/[])
{
    NormInstanceHandle instance = NormCreateInstance();
    if (NORM_INSTANCE_INVALID == instance) return -1;

    if (getenv("NORM_DEBUG") != NULL) NormSetDebugLevel((unsigned int)atoi(getenv("NORM_DEBUG")));
    // Keep simulated loss/backoff choices repeatable in automated runs.
    srand(0x4e4f524dU);

    UINT8 fecId = (UINT8)EnvULong("NORM_FEC_ID", RATELESS_FEC_ID);
    if ((RATELESS_FEC_ID != fecId) && !RegisterCustomLayout(instance, fecId))
    {
        fprintf(stderr, "normRatelessTest error: failed to register custom FEC layout\n");
        return -1;
    }

    // Register the mock rateless codec with the instance.  The registry is shared by
    // every session the instance owns, so this must happen before any are created.
    if (!NormRegisterFecCoder(instance, fecId, CreateMockEncoder, CreateMockDecoder, true))
    {
        fprintf(stderr, "normRatelessTest error: failed to register rateless FEC codec\n");
        return -1;
    }

    // Use two sessions with DISTINCT node ids on the same multicast group so the
    // receiver treats the sender as a genuine remote peer.  (A single session
    // acting as both sender and receiver would hear its own NACKs and suppress
    // subsequent ones, stalling multi-round rateless repair.)
    const char* groupAddr = "224.1.2.3";
    const UINT16 groupPort = 6003;

    NormSessionHandle txSession = NormCreateSession(instance, groupAddr, groupPort, 1);
    NormSessionHandle rxSession = NormCreateSession(instance, groupAddr, groupPort, 2);
    if ((NORM_SESSION_INVALID == txSession) || (NORM_SESSION_INVALID == rxSession)) return -1;

    NormSetRxPortReuse(txSession, true);
    NormSetRxPortReuse(rxSession, true);
    NormSetMulticastLoopback(txSession, true);  // let our data reach the rx session on this host
    NormSetMulticastLoopback(rxSession, true);  // let our NACKs reach the tx session on this host
    NormSetTxRate(txSession, 10.0e+06);  // 10 Mbps for a quick loopback run

    // Induce loss on the receiver so it must repair via rateless parity.
    double lossPct = (getenv("NORM_RX_LOSS") != NULL) ? atof(getenv("NORM_RX_LOSS")) : 10.0;
    NormSetRxLoss(rxSession, lossPct);

    if (!NormStartReceiver(rxSession, 1024 * 1024))
    {
        fprintf(stderr, "normRatelessTest error: NormStartReceiver() failed\n");
        return -1;
    }

    // Shrinking the sender buffer (relative to the object) forces blocks out of the
    // sender's block_buffer before their repairs finish, so repair requests have to be
    // served from SenderRecoverBlock()-recovered blocks.  That is a distinct code path
    // from steady-state repair, so make it reachable:
    //   NORM_TX_BUFFER=131072 NORM_DATA_LEN=200000 ./normRatelessTest
    UINT32 txBuffer = (UINT32)EnvULong("NORM_TX_BUFFER", 1024 * 1024);

    UINT16 numData = (UINT16)EnvULong("NORM_NDATA", 16);
    UINT16 numParity = (UINT16)EnvULong("NORM_NPARITY", 64);
    if (!NormStartSender(txSession, 1, txBuffer, 512, numData, numParity, fecId))
    {
        fprintf(stderr, "normRatelessTest error: NormStartSender() failed\n");
        return -1;
    }

    // Build a known payload spanning several blocks (incl. a short final block).
    const UINT32 dataLen = (UINT32)EnvULong("NORM_DATA_LEN", 20000);
    char* txData = new char[dataLen];
    for (UINT32 i = 0; i < dataLen; i++)
        txData[i] = (char)((i * 31 + 7) & 0xff);

    NormObjectHandle txObj = NormDataEnqueue(txSession, txData, dataLen);
    if (NORM_OBJECT_INVALID == txObj)
    {
        fprintf(stderr, "normRatelessTest error: NormDataEnqueue() failed\n");
        return -1;
    }
    printf("normRatelessTest: sending %u bytes via mock rateless codec (%.3g%% rx loss)...\n",
           dataLen, lossPct);

    int result = -1;
    // Poll non-blocking so the loop can enforce a wall-clock watchdog (must never hang).
    time_t deadline = time(NULL) + 60;
    bool running = true;
    while (running && (time(NULL) <= deadline))
    {
        NormEvent event;
        bool gotEvent = false;
        while (running && NormGetNextEvent(instance, &event, false))
        {
            gotEvent = true;
            switch (event.type)
            {
                case NORM_RX_OBJECT_COMPLETED:
                {
                    unsigned int rxLen = (unsigned int)NormObjectGetSize(event.object);
                    const char* rxData = NormDataAccessData(event.object);
                    if ((rxLen == dataLen) && (NULL != rxData) && (0 == memcmp(rxData, txData, dataLen)))
                    {
                        printf("normRatelessTest: received %u bytes, contents verified. PASS\n", rxLen);
                        result = 0;
                    }
                    else
                    {
                        fprintf(stderr, "normRatelessTest: received object mismatch (len %u vs %u). FAIL\n",
                                rxLen, dataLen);
                    }
                    running = false;
                    break;
                }
                case NORM_RX_OBJECT_ABORTED:
                    fprintf(stderr, "normRatelessTest error: NORM_RX_OBJECT_ABORTED\n");
                    running = false;
                    break;
                default:
                    break;
            }
        }
        if (!gotEvent) SleepMsec(10);
    }
    if (running)
        fprintf(stderr, "normRatelessTest error: timed out waiting for object reception\n");

    NormStopSender(txSession);
    NormStopReceiver(rxSession);
    NormDestroyInstance(instance);
    delete[] txData;

    printf("normRatelessTest: %s\n", (0 == result) ? "SUCCESS" : "FAILURE");
    return result;
}
