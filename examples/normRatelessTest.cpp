#include <normApi.h>
#include <normEncoder.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

class MockRatelessEncoder : public NormEncoder {
public:
    virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize) {
        ndata = numData;
        vec_size = vectorSize;
        return true;
    }
    virtual void Destroy() {}
    virtual void Encode(unsigned int segmentId, const char* dataVector, char** parityVectorList) {}
    virtual bool IsRateless() const { return true; }

private:
    unsigned int ndata;
    UINT16 vec_size;
};

class MockRatelessDecoder : public NormDecoder {
public:
    virtual bool Init(unsigned int numData, unsigned int numParity, UINT16 vectorSize) {
        ndata = numData;
        vec_size = vectorSize;
        return true;
    }
    virtual void Destroy() {}
    virtual int Decode(char** vectorList, unsigned int numData, unsigned int erasureCount, unsigned int* erasureLocs) {
        return erasureCount;
    }

private:
    unsigned int ndata;
    UINT16 vec_size;
};

extern "C" {
    NormEncoder* CreateMockEncoder() { return new MockRatelessEncoder(); }
    NormDecoder* CreateMockDecoder() { return new MockRatelessDecoder(); }
}

int main(int argc, char* argv[]) {
    NormInstanceHandle instance = NormCreateInstance();
    if (!instance) return -1;
    
    NormSessionHandle session = NormCreateSession(instance, "127.0.0.1", 6003, 1);
    
    // Register Rateless Coder (FEC ID 131)
    if (!NormRegisterFecCoder(session, 131, CreateMockEncoder, CreateMockDecoder, true)) {
        fprintf(stderr, "Failed to register custom FEC codec.\n");
        return -1;
    }
    
    NormSetRxPortReuse(session, true);
    NormSetMulticastLoopback(session, true);
    
    // Start Sender using FEC 131
    if (!NormStartSender(session, 1, 1024*1024, 1024, 64, 0, 131)) {
        fprintf(stderr, "Failed to start sender.\n");
        return -1;
    }
    
    printf("Sender started with mock rateless codec. Transmitting...\n");
    
    NormObjectHandle obj = NormDataEnqueue(session, "testdata", 8);
    
    if (obj) {
        bool running = true;
        while (running) {
            NormEvent event;
            if (NormGetNextEvent(instance, &event)) {
                if (event.type == NORM_TX_FLUSH_COMPLETED) {
                    printf("Transmission flushed completely.\n");
                    running = false;
                }
            }
        }
    }
    
    NormStopSender(session);
    NormDestroyInstance(instance);
    
    printf("Success.\n");
    return 0;
}
