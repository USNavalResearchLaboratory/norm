// This code tests our NORM FEC encoder/decoder implementations


#include "protoTime.h"  // for ProtoTime

#include "normEncoderRS8.h"
#include "normEncoderRS16.h"

#include <string.h> // for memcpy(), etc
#include <stdlib.h> // for rand()
#include <stdio.h>

const unsigned int NUM_PARITY   = 100;
const unsigned int NUM_DATA     = 400;
const unsigned int SHORT_DATA   = 400;
const unsigned int SEG_SIZE     = 64;

const unsigned int B_SIZE = (SHORT_DATA + NUM_PARITY);

#define NORM_ENCODER NormEncoderRS16
#define NORM_DECODER NormDecoderRS16

int main(int argc, char* argv[])
{
    // Uncomment to seed random generator
    ProtoTime currentTime;
    currentTime.GetCurrentTime();
    int seed = (unsigned int)currentTime.usec();
    fprintf(stderr, "fect: seed = %u\n", seed);
    srand(seed);
    
    NORM_ENCODER encoder;
    encoder.Init(NUM_DATA, NUM_PARITY, SEG_SIZE);
    NORM_DECODER decoder;
    decoder.Init(NUM_DATA, NUM_PARITY, SEG_SIZE);
     
    for (int trial = 0; trial < 2; trial++)
    {

        // 1) Create some "printable" source data
        char txData[B_SIZE][SEG_SIZE];
        char* txDataPtr[B_SIZE];
        for (unsigned int i = 0 ; i < SHORT_DATA; i++)
        {
            txDataPtr[i] = txData[i];
            memset(txDataPtr[i], 'a' + (i%26), SEG_SIZE - 1);
            txDataPtr[i][SEG_SIZE - 1] = '\0';
        }

        // 2) Zero-init the parity vectors of our txData
        for (unsigned int i = SHORT_DATA; i < B_SIZE; i++)
        {
            txDataPtr[i] = txData[i];
            memset(txDataPtr[i], 0, SEG_SIZE);
        }

        // 3) Run our encoder (and record CPU time)
        ProtoTime startTime, stopTime;
        startTime.GetCurrentTime();
        for (unsigned int i = 0; i < SHORT_DATA; i++)
        {
            encoder.Encode(i, txDataPtr[i], txDataPtr + SHORT_DATA);
        }
        stopTime.GetCurrentTime();
        double encodeTime = ProtoTime::Delta(stopTime, startTime);

        // 4) Copy "txData" to our "rxData"
        char rxData[B_SIZE][SEG_SIZE];
        char* rxDataPtr[B_SIZE];
        for (unsigned int i = 0; i < B_SIZE; i++)
        {
            rxDataPtr[i] = rxData[i];
            memcpy(rxDataPtr[i], txDataPtr[i], SEG_SIZE);
        }

        // 5) Randomly pick some number of erasures and their locations
        unsigned int erasureCount = 2;//rand() % NUM_PARITY;
        unsigned int erasureLocs[B_SIZE];
        for (unsigned int i = 0; i < B_SIZE; i++)
            erasureLocs[i] = i;
        for (unsigned int i = 0; i < erasureCount; i++)
        {
            // We do a little random shuffle here to generate 
            // "erasureCount" unique erasure locations
            unsigned int loc = i + (rand() % (B_SIZE - i));
            unsigned int tmp = erasureLocs[i];
            erasureLocs[i] = erasureLocs[loc];
            erasureLocs[loc] = tmp;
        }
        // Sort the "erasureLocs" into order (important!)
        for (unsigned int i = 0; i < erasureCount; i++)
        {
            for (unsigned int j = i+1; j < erasureCount; j++)
            {
                if (erasureLocs[j] < erasureLocs[i])
                {
                    unsigned int tmp = erasureLocs[i];
                    erasureLocs[i] = erasureLocs[j];
                    erasureLocs[j] = tmp;
                }
            }
        }
        fprintf(stderr, "erasureCount: %u erasureLocs: ", erasureCount);
        for (unsigned int i = 0; i < erasureCount; i++)
            fprintf(stderr, "%u ", erasureLocs[i]);
        fprintf(stderr, "\n");

        // 6) Clear our erasure locs
        for (unsigned int i = 0; i < erasureCount; i++)
            memset(rxDataPtr[erasureLocs[i]], 0, SEG_SIZE);

        // 7) Decode the rxData 


        startTime.GetCurrentTime();
        decoder.Decode(rxDataPtr, SHORT_DATA, erasureCount, erasureLocs);
        stopTime.GetCurrentTime();
        double decodeTime = ProtoTime::Delta(stopTime, startTime);

        // 8) check decoding
        for (unsigned int i = 0; i < SHORT_DATA; i++)
        {
            if (0 != memcmp(rxDataPtr[i], txDataPtr[i], SEG_SIZE))
            {
                fprintf(stderr, "fect: segment:%d rxData decode error!\n", i);
                fprintf(stderr, "  txData: %.32s ...\n", txDataPtr[i]);   
                fprintf(stderr, "  rxData: %.32s ...\n", rxDataPtr[i]);
            }
        }    


        // 9) Print results
        fprintf(stderr, "fect: encodeTime:%lf usec decodeTime:%lf usec\n", 1.0e+06*encodeTime, 1.0e+06*decodeTime); 
    }
}  // end main()
