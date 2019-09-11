
#include "protoApp.h"
#include "normFile.h"

// Commment this #define out to use new, faster RS8 codec instead
// (TBD - provide option use 16-bit Reed Solomon for large block sizes?)
//#define USE_MDP_FEC

#ifdef USE_MDP_FEC
#include "normEncoderMDP.h"
#else
#include "normEncoderRS8.h"
#endif // if/else USE_MDP_FEC

#include <sys/types.h>  // for BYTE_ORDER macro
#include <stdlib.h>  // for atoi()
#include <stdio.h>   // for stdout/stderr printouts
#include <string.h>

class NormPrecodeApp : public ProtoApp
{
    public:
        NormPrecodeApp();
        ~NormPrecodeApp();

        // Overrides from ProtoApp or NsProtoSimAgent base
        bool OnStartup(int argc, const char*const* argv);
        void OnShutdown();
        bool ProcessCommands(int argc, const char*const* argv);
        bool OnCommand(const char* cmd, const char* val);

    private:
        void Usage();
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        CmdType CommandType(const char* cmd);
        static const char* const cmd_list[];
        
        bool Encode();
        bool Decode();
        
        void InitInterleaver(NormFile::Offset numSegments);
        NormFile::Offset ComputeInterleaverOffset(NormFile::Offset segmentId, NormFile::Offset numSegments);
        NormFile::Offset ComputeSegmentOffset(NormFile::Offset interleaverId, NormFile::Offset numSegments);
        // CRC32 checksum stuff
        static const UINT32 CRC32_TABLE[256];
        static UINT32 ComputeCRC32(const char* buffer, unsigned int buflen);
    
        static const NormFile::Offset SEGMENT_MIN;
        static const NormFile::Offset SEGMENT_MAX;
        
        // We use these assuming IEEE754 floating point
        static NormFile::Offset ntoho(NormFile::Offset offset)
        {
# if BYTE_ORDER == LITTLE_ENDIAN
            NormFile::Offset result;
            switch (sizeof(NormFile::Offset))
            {
                case 8:
                {
                    UINT32* outPtr = (UINT32*)&result;
                    UINT32* inPtr = (UINT32*)&offset;
                    outPtr[0] = ntohl(inPtr[1]);
                    outPtr[1] = ntohl(inPtr[0]);
                    break;
                }
                case 4:
                {
                    result = ntohl((UINT32)offset);
                    break;
                }
                default:
                    ASSERT(0);
                    result = offset;
                    break;
            }
            return result;
#else
            return offset;
#endif  // if/else __BIG_ENDIAN
        }
        static NormFile::Offset htono(NormFile::Offset offset)
        {
            return ntoho(offset);
        }
    
        NormFile         in_file;
        char             in_file_path[PATH_MAX];
        NormFile         out_file;
        bool             encode;
        
        unsigned int     segment_size;  // should be same as NORM segment size
        unsigned int     num_data;
        unsigned int     num_parity;
        
        NormFile::Offset i_max;         // max interleaver dimension
        NormFile::Offset i_buffer_max;  // Read buffer max (bigger yields less seeking)
                
        NormFile::Offset interleaver_width;
        NormFile::Offset interleaver_height;
        NormFile::Offset interleaver_size;  // (width * height)
        
}; // end class NormPrecodeApp

// Our application instance 
PROTO_INSTANTIATE_APP(NormPrecodeApp) 
        
const NormFile::Offset NormPrecodeApp::SEGMENT_MIN = 8;
const NormFile::Offset NormPrecodeApp::SEGMENT_MAX = 8192;

NormPrecodeApp::NormPrecodeApp()
 : encode(true), segment_size(1024), num_data(196), num_parity(4), 
   i_max(1000), i_buffer_max(1500000000)
{  
    in_file_path[0] = '\0';  
}

NormPrecodeApp::~NormPrecodeApp()
{
}

void NormPrecodeApp::Usage()
{
   fprintf(stderr, "Usage:  npc {encode|decode} input <inFile> [output <outFile>]\n"
                   "            [segment <segmentSize>][block numData][parity numParity]\n"
                   "            [background][help][debug <debugLevel>\n");  
}  // end NormPrecodeApp::Usage()

const char* const NormPrecodeApp::cmd_list[] = 
{
    "-help",        // show usage
    "+debug",       // set debug level
    "-encode",      // encode input file (default)
    "-decode",      // decode input file (default)     
    "+input",       // set input file     
    "+output",      // set output file     
    "+segment",     // set segment size (default = 1024)    
    "+block",       // set block size (default = 128)    
    "+parity",      // set parity per block (default = 2)    
    "+imax",        // set interleaver max dimension
    "+ibuffer",     // set imax interleaver buffer (buffer is used if interleaver size fits)
    "-background",  // run w/out command shel (Win32)  
    NULL         
};

bool NormPrecodeApp::OnCommand(const char* cmd, const char* val)
{
    CmdType type = CommandType(cmd);
    ASSERT(CMD_INVALID != type);
    size_t len = strlen(cmd);
    if ((CMD_ARG == type) && !val)
    {
        PLOG(PL_FATAL, "NormApp::OnCommand(%s) missing argument\n", cmd);
        return false;        
    }
    
    if (!strncmp("help", cmd, len))
    {   
        Usage();
        return false;
    }
    else if (!strncmp("debug", cmd, len))
    {
        
        int debugLevel = atoi(val);
        if ((debugLevel < 0) || (debugLevel > 12))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(segment) invalid debug level!\n");   
            return false;
        }
        SetDebugLevel(debugLevel);
    }
    else if (!strncmp("encode", cmd, len))
    {
        encode = true;
    }
    else if (!strncmp("decode", cmd, len))
    {
        encode = false;
    }
    else if (!strncmp("input", cmd, len))
    {
        if (!in_file.Open(val, O_RDONLY))
        {
            PLOG(PL_FATAL, "npc: error opening input file: %s\n", GetErrorString());
            Usage();
            return false;
        }
        strncpy(in_file_path, val, PATH_MAX);
    }
    else if (!strncmp("output", cmd, len))
    {
        if (!out_file.Open(val, O_WRONLY | O_CREAT | O_TRUNC))
        {
            PLOG(PL_FATAL, "npc: error opening input file: %s\n", GetErrorString());
            Usage();
            return false;
        }
    }
    else if (!strncmp("segment", cmd, len))
    {
        int segmentSize = atoi(val);
        if ((segmentSize < SEGMENT_MIN) || (segmentSize > SEGMENT_MAX))
        {
            PLOG(PL_FATAL, "npc: error: <segmentSize> out of range\n");
            return false;
        }
        segment_size = segmentSize;
    }
    else if (!strncmp("block", cmd, len))
    {
        int numData = atoi(val);
        if ((numData < 1) || (numData > 127))
        {
            PLOG(PL_FATAL, "npc: error: block <numData> out of range\n");
            return false;
        }
        num_data = numData;
    }
    else if (!strncmp("parity", cmd, len))
    {
        int numParity = atoi(val);
        if ((numParity < 0) || (numParity > 127))
        {
            PLOG(PL_FATAL, "npc: error: parity <numParity> out of range\n");
            return false;
        }
        num_parity = numParity;
    }
    else if (!strncmp("imax", cmd, len))
    {
        int iMax = atoi(val);
        if (iMax <= 0) iMax = 0;
        i_max = iMax;
    }
    else if (!strncmp("ibuffer", cmd, len))
    {
        int iBufferMax = atoi(val);
        if (iBufferMax < 0)
        {
            PLOG(PL_FATAL, "npc: error: \"ibuffer\" cannot be less than zero\n");
            return false;
        }
        i_buffer_max = iBufferMax;
    }
    else if (!strncmp("background", cmd, len))
    {
        // do nothing, handled by "ProtoApp" base
    }
    return true;
}  // end NormPrecodeApp::OnCommand()

bool NormPrecodeApp::ProcessCommands(int argc, const char*const* argv)
{
    int i = 1;
    while ( i < argc)
    {
        CmdType cmdType = CommandType(argv[i]);   
        switch (cmdType)
        {
            case CMD_INVALID:
                PLOG(PL_FATAL, "npc: error: Invalid command:%s\n", argv[i]);
                return false;
            case CMD_NOARG:
                if (!OnCommand(argv[i], NULL)) return false;
                i++;
                break;
            case CMD_ARG:
                if (!OnCommand(argv[i], argv[i+1])) return false;
                i += 2;
                break;
        }
    }
    return true;
}  // end NormPrecodeApp::ProcessCommands()

NormPrecodeApp::CmdType NormPrecodeApp::CommandType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    size_t len = strlen(cmd);
    bool matched = false;
    CmdType type = CMD_INVALID;
    const char* const* nextCmd = cmd_list;
    while (*nextCmd)
    {
        if (!strncmp(cmd, *nextCmd+1, len))
        {
            if (matched)
            {
                // ambiguous command (command should match only once)
                return CMD_INVALID;
            }
            else
            {
                matched = true;   
                if ('+' == *nextCmd[0])
                    type = CMD_ARG;
                else
                    type = CMD_NOARG;
            }
        }
        nextCmd++;
    }
    return type;
}  // end NormPrecodeApp::CommandType()

bool NormPrecodeApp::OnStartup(int argc, const char*const* argv)
{
    // This is essentially the "main()" for this program
    if (!ProcessCommands(argc, argv))
    {
        return false;
    }
    
    if (!in_file.IsOpen())
    {
        PLOG(PL_FATAL, "npc: error: no input file given\n");
        Usage();
        return false;
    }
    
    if (encode)
        return Encode();
    else
        return Decode();
    
}  // end NormPrecodeApp::OnStartup()

void NormPrecodeApp::OnShutdown()
{
    // (TBD) do better cleanup of allocated buffers, etc!!
   if (in_file.IsOpen()) in_file.Close();
   if (out_file.IsOpen()) out_file.Close();
   PLOG(PL_INFO, "npc: Done.\n");
}  // end NormPrecodeApp::OnShutdown()

#define DIFF_T(a,b) (1+ 1000000*(a.tv_sec - b.tv_sec) + (a.tv_usec - b.tv_usec) )

void NormPrecodeApp::InitInterleaver(NormFile::Offset numSegments)
{
    interleaver_width = (NormFile::Offset)(sqrt((double)numSegments));
    interleaver_height = numSegments / interleaver_width;
    if (0 != (numSegments % interleaver_height)) interleaver_height++;
    // Limit dimension if "i_max" is set to non-zero value
    if ((i_max > 0) && ((interleaver_width > i_max) || (interleaver_height > i_max)))
        interleaver_height = interleaver_width = i_max;
    interleaver_size = interleaver_height * interleaver_width;
    PLOG(PL_INFO, "npc interleaver width:%lu height:%lu segments (numSeg:%lld)\n", 
            (unsigned long)interleaver_width, (unsigned long)interleaver_height, numSegments);
    
}  // end NormPrecodeApp::InitInterleaver()


NormFile::Offset NormPrecodeApp::ComputeInterleaverOffset(NormFile::Offset segmentId, NormFile::Offset numSegments)
{
    ASSERT(0 != interleaver_height);
    
    NormFile::Offset interleaverWidth = interleaver_width;
    NormFile::Offset interleaverHeight = interleaver_height;
    NormFile::Offset interleaverSize = interleaver_size;
    NormFile::Offset blockId;
    if (i_max > 0) 
    {
        blockId = segmentId / interleaverSize;
        segmentId = segmentId % interleaverSize;
    }
    else
    {
        blockId = 0;
    }
    
    
    // Check to see if we're in the last block
    NormFile::Offset lastSegmentId = numSegments - 1;
    NormFile::Offset lastBlockId = lastSegmentId / interleaverSize;
    if ((blockId == lastBlockId) && (0 != (numSegments % interleaverSize)))
    {
        // This block is smaller than our usual interleaver_size,
        // so we're going to "square things up" to maximize the
        // distance of this last block within interleaver_size constraint
        NormFile::Offset lastBlockSize = (numSegments % interleaverSize);
        interleaverWidth = (NormFile::Offset)(sqrt((double)lastBlockSize)); 
        interleaverHeight = lastBlockSize / interleaverWidth;
        if (0 != (lastBlockSize % interleaverHeight)) interleaverHeight++;
    }
    
    NormFile::Offset interleaverCol = segmentId / interleaverHeight;
    NormFile::Offset interleaverRow = segmentId % interleaverHeight;
    NormFile::Offset interleaverId = ((interleaverRow * interleaverWidth) + interleaverCol);
    
    if (0 != blockId)
        interleaverId += (blockId * interleaver_size);
    
    // This check compensates for data not perfectly filling interleaver rectangle
    if (interleaverId >= numSegments)
    {
        // We're here because we hit a "hole" in the rectangle
        NormFile::Offset lastSegmentId = numSegments -  1;
        if (0 != blockId)
        {
            interleaverId = interleaverId % interleaverSize;
            lastSegmentId = lastSegmentId % interleaverSize;    
        }        
        // Find non-interleaved position of lastSegmentId within interleaver
        NormFile::Offset maxRow = lastSegmentId / interleaverWidth;
        NormFile::Offset maxCol = lastSegmentId % interleaverWidth;
        // There may be empty rows if lastSegmentId small wr2 interleaver size
        NormFile::Offset emptyRows = interleaverHeight - maxRow - 1;
        NormFile::Offset delta = 1 + emptyRows * interleaverCol;
        if (interleaverCol > maxCol)
        {
            delta += interleaverRow - maxRow;
            delta += interleaverCol - maxCol - 1;
        }
        else
        {
            delta += interleaverRow - maxRow - 1;
        }
        
        // Find interleaved position of lastSegmentId within interleaver
        NormFile::Offset lastCol = lastSegmentId / interleaverHeight;
        NormFile::Offset lastRow = lastSegmentId % interleaverHeight;
        
        // Remap this segment to the "delta" interleaved position after "lastSegmentId"
        lastRow += delta;
        // This assertion _should_ be true if we're "square" enough
        // (it does break when interleaver width is much greater than height,
        //  so, some day (TBD) we may want to generalize this remapping trick more???
        ASSERT(lastCol >= maxCol);
        if (lastCol == maxCol)
        {
            if (lastRow > maxRow)
            {
                lastCol++;
                lastRow -= maxRow + 1;
            }
        }
        interleaverCol = lastCol + (lastRow / maxRow);
        interleaverRow = lastRow % maxRow;
        
        interleaverId = (interleaverRow * interleaverWidth) + interleaverCol;
        if (0 != blockId) interleaverId += (blockId * interleaver_size);
        ASSERT((interleaverId < numSegments) && (interleaverId >= 0));
    }
    return (segment_size * interleaverId);
}  // end NormPrecodeApp::ComputeInterleaverOffset()

        
NormFile::Offset NormPrecodeApp::ComputeSegmentOffset(NormFile::Offset interleaverId, NormFile::Offset numSegments)
{
    NormFile::Offset interleaverWidth = interleaver_width;
    NormFile::Offset interleaverHeight = interleaver_height;
    NormFile::Offset interleaverSize = interleaver_size;
    NormFile::Offset blockId;
    if (i_max > 0) 
    {
        blockId = interleaverId / interleaverSize;
        interleaverId = interleaverId % interleaverSize;
    }
    else
    {
        blockId = 0;
    }
    
    
    NormFile::Offset interleaverRow = interleaverId / interleaverWidth;
    NormFile::Offset interleaverCol = interleaverId % interleaverWidth;
    
    NormFile::Offset segmentId = interleaverCol * interleaverHeight + interleaverRow;
    if (0 != blockId)
        segmentId += (blockId * interleaver_size);
    
    // Check this given change on non-rectangular data set wr2 interleaver dimensions
    if (segmentId >= numSegments)
    {
        // It was a "hole", so find its hole delta
        NormFile::Offset lastSegmentId = numSegments - 1;
        if (0 != blockId)
        {
            segmentId = segmentId % interleaverSize;
            lastSegmentId = lastSegmentId % interleaverSize;
        }
        // Here maxRow/maxCol are wr2 _interleaved_ position of lastSegmentId    
        NormFile::Offset maxCol = lastSegmentId / interleaverHeight;
        NormFile::Offset maxRow = lastSegmentId % interleaverHeight;
        // AS above, this assertion _should_ be true if we're "square" enough
        // (it does break when interleaver width is much greater than height,
        //  so, some day (TBD) we may want to generalize this remapping trick more???
        ASSERT(interleaverCol >= maxCol);
        NormFile::Offset delta = (interleaverCol - maxCol)*(maxRow+1) + interleaverRow - maxRow;
        
        // Then, remap "delta" to find _original_ "hole" position (corrected interleaver position)
        // Here maxRow/maxCol are wr2 _source_ position of lastSegmentId
        maxRow = lastSegmentId / interleaverWidth;
        maxCol = lastSegmentId % interleaverWidth;
        NormFile::Offset emptyRows = interleaverHeight - maxRow - 1;
        if (delta <= (emptyRows*(maxRow + 1)))
        {
            // in first area
            interleaverCol = (delta - 1) / emptyRows;
            interleaverRow = ((delta - 1) % emptyRows) + maxRow + 1;
        }
        else
        {
            // in second area
            delta -= emptyRows * (maxCol + 1);
            interleaverCol = maxCol + (delta / (emptyRows+1));
            interleaverRow = (delta % (emptyRows + 1)) + maxRow;
        }
        segmentId = interleaverCol * interleaverHeight + interleaverRow;
        if (0 != blockId) segmentId += (blockId * interleaver_size);   
        ASSERT(segmentId < numSegments);
    }
    NormFile::Offset segmentOffset = segment_size * segmentId;
    return segmentOffset;
}  // end NormPrecodeApp::ComputeSegmentOffset()

bool NormPrecodeApp::Encode()
{
    if (!out_file.IsOpen())
    {
        // Create ".npc" out_file name from in_file_path
        const char* ptr = strrchr(in_file_path, PROTO_PATH_DELIMITER);
        if (NULL == ptr)
            ptr = in_file_path;
        else
            ptr++;
        char outFileName[PATH_MAX+1];
        outFileName[PATH_MAX] = '\0';
        strncpy(outFileName, ptr, PATH_MAX);
        char* ptr2 = strrchr(outFileName, '.');
        if (NULL != ptr2) *ptr2 = '_';
        // Append ".npc" suffix
        if (strlen(outFileName) < (PATH_MAX - 4))
            strcat(outFileName, ".npc");
        else
            strcpy(outFileName + PATH_MAX - 4, ".npc");
        if (!out_file.Open(outFileName,  O_WRONLY | O_CREAT | O_TRUNC))
        {
            PLOG(PL_FATAL, "npc: error opening output file: %s\n", GetErrorString());
            return false;   
        }
    }
    
    struct timeval t1, t2;
    ProtoSystemTime(t1);
    
    NormFile::Offset fileSize = in_file.GetSize();
    
    // We reserve 4 bytes for our CRC (used to detect erasures)
    unsigned int dataSegmentSize = segment_size - 4;
    
    NormFile::Offset numInputSegments = 1 + fileSize / dataSegmentSize;
    unsigned int lastFecSegSize = (unsigned int)(fileSize % dataSegmentSize);
    
    if (0 != lastFecSegSize) numInputSegments++;
    
    // Calculate FEC block size(s)
    NormFile::Offset numBlocks = numInputSegments / num_data;
    unsigned int fecBlockSize = num_data;
    unsigned int lastBlockSize = (unsigned int)(numInputSegments % num_data);
    if (0 != lastBlockSize) numBlocks++; 
    NormFile::Offset lastBlockId = numBlocks - 1;
     
    // 0) Calculate "out_file" size and determine interleaver width and height
    NormFile::Offset numOutputSegments =  
        ((numBlocks - 1) * (fecBlockSize + num_parity)) + lastBlockSize + num_parity;
    
    
    InitInterleaver(numOutputSegments);
    // 1) Init our FEC encoder
#ifdef USE_MDP_FEC
    NormEncoderMDP encoder;
#else
    NormEncoderRS8 encoder;
#endif // if/else USE_MDP_FEC
    if (!encoder.Init(num_data, num_parity, dataSegmentSize))  // 4 CRC bytes are _not_ encoded
    {
        PLOG(PL_FATAL, "npc: error initializing FEC encoder\n");
        return false;
    }
    
    // Determine number of segments to allocate for FEC encoding and
    // interleaver buffering if applicable
    NormFile::Offset interleaverBytes = interleaver_size * segment_size;
	char* iBuffer = NULL;
    bool useBuffering = false;
    if (interleaverBytes <= i_buffer_max)
    {
        // Allocate buffering for full interleaver block and parity
        PLOG(PL_INFO, "npc: allocating interleaver buffer ...\n");
        iBuffer = new char[interleaverBytes + (num_parity * segment_size)];
        if (NULL != iBuffer)
            useBuffering = true;
        else
            PLOG(PL_WARN, "npc: warning: couldn't allocate full interleaver buffer: %s\n", GetErrorString());
    }
    if (NULL == iBuffer)
    {
        // Just allocate one segment plus parity vecs
        iBuffer = new char[(1 + num_parity) * segment_size];
        if (NULL == iBuffer)
        {
            PLOG(PL_FATAL, "npc: error: couldn't allocate parity buffer: %s\n", GetErrorString());
            return false;
        }
    }       
    
    // 2) Create parity vector array for FEC encoding
    char**  parityVec = new char*[num_parity];
    if (NULL == parityVec)
    {
        PLOG(PL_FATAL, "npc: new parity array error: %s\n", GetErrorString());
        return false;
    }
    // Keep parity vecs after first _or_ "interleaver_size" segments of "iBuffer"
    char* pvec =  iBuffer + (useBuffering ? interleaverBytes : segment_size);
    memset(pvec, 0, num_parity*segment_size);
    for (unsigned int i = 0; i < num_parity; i++)
    {
        parityVec[i] = pvec;
        pvec += segment_size;
    }
    
    // 3) Build "meta_data" segment for the file
    // (TBD) This could be built directly into iBuffer segment zero
    char metaData[SEGMENT_MAX+4];
    memset(metaData, 0, SEGMENT_MAX);
    NormFile::Offset sz = fileSize;
    if (sizeof(NormFile::Offset) == 8)
    {
        sz = htono(fileSize);
        memcpy(metaData, &sz, 8);
    }
    else if (sizeof(NormFile::Offset) == 4)
    {
        sz = htonl((UINT32)sz);
        memcpy(metaData + 4, &sz, 4);
    }
    else
    {
        PLOG(PL_FATAL, "npc: error: unsupported file offset size (%d bytes)\n", sizeof(NormFile::Offset));
        return false;
    }
    // put in_file_path file name portion into middle section of "metaData"
    const char* ptr = strrchr(in_file_path, PROTO_PATH_DELIMITER);
    if (NULL == ptr)
        ptr = in_file_path;
    else
        ptr++;
    // Reserves space for file size (8 byte header) and CRC (4 byte trailer)
    strncpy(metaData+8, ptr, segment_size - 12);
    
    // 2) Read "in_file" segments, encode, and output to "out_file"
    PLOG(PL_ALWAYS, "npc: encoding file ... (progress:   0%%)");
    // State to track/display encoding progress
    NormFile::Offset progressThreshold = numOutputSegments / 100;
    double progressIncrement = 100.0;
    if (progressThreshold > 1)
         progressIncrement = (double)numOutputSegments / (double)progressThreshold;
    else
        progressThreshold = numOutputSegments; // small number of segments
    NormFile::Offset progressCounter = 0;
    int progressPercent = 0;
    
    
    NormFile::Offset blockId = 0;
    unsigned int parityCount = 0;
    bool parityReady = false;
    NormFile::Offset inputSegmentId = 0;
    NormFile::Offset outputSegmentId = 0;
    while (outputSegmentId < numOutputSegments)
    {
        NormFile::Offset interleaverOffset = ComputeInterleaverOffset(outputSegmentId, numOutputSegments);
        char* segment = useBuffering ? (iBuffer + (interleaverOffset % interleaverBytes)) : iBuffer;
        if (parityReady)
        {
            // D) Output parity segment for this block
            char* pvec = parityVec[num_parity - parityCount];
            memcpy(segment, pvec, dataSegmentSize);
            if (0 == --parityCount) 
            {
                memset(parityVec[0], 0, num_parity * segment_size);
                parityReady = false;
                blockId++;
            }
        }
        else
        {
            // Read and encode a segment
            inputSegmentId++;
            if (1 == inputSegmentId)
            {
                // A) Segment '0' is the meta-data segment
                memcpy(segment, metaData, dataSegmentSize);
            }
            else
            {
                // B) Read in data portion of next "segment"
                unsigned int bytesToRead;
                if (inputSegmentId != numInputSegments)
                {
                    bytesToRead = dataSegmentSize; 
                }
                else
                {
                    memset(segment, 0, dataSegmentSize);
                    bytesToRead = lastFecSegSize;
                }
                if (in_file.Read(segment, bytesToRead) != bytesToRead)
                {
                    PLOG(PL_FATAL, "\nnpc: unexpected error reading input file: %s\n", GetErrorString());
                    return false;
                }
            }
            // C) Encode and check for parity readiness
            //TRACE("outputSegmentId:%lu\n", outputSegmentId);
            
            encoder.Encode(outputSegmentId % fecBlockSize, segment, parityVec);
            unsigned int numData = (blockId != lastBlockId) ? fecBlockSize : lastBlockSize;
            if (numData == ++parityCount) 
            {
                parityCount = num_parity;
                parityReady = true;
            }
        }
        // E) Calculate and add CRC32 checksum to each "segment"
        UINT32 checksum = ComputeCRC32(segment, dataSegmentSize);
        checksum = htonl(checksum);
        memcpy(segment+dataSegmentSize, &checksum, 4);   
        
        if (useBuffering)
        {
            
            outputSegmentId++;
            if ((0 == (outputSegmentId % interleaver_size)) || (outputSegmentId == numOutputSegments))
            {
                // Output our buffered interleaver block from memory (iBuffer) to "out_file"
                NormFile::Offset bytesToWrite;
                if ((outputSegmentId != numOutputSegments) || (numOutputSegments == interleaver_size))
                    bytesToWrite = interleaver_size;
                else
                    bytesToWrite = (outputSegmentId % interleaver_size);
                bytesToWrite *= segment_size;
                if (out_file.Write(iBuffer, bytesToWrite) != bytesToWrite)
                {   
                    PLOG(PL_FATAL, "\nnpc: unexpected error writing to output file: %s\n", GetErrorString()); 
                    return false;
                }
            }
        }
        else
        {
            // Output interleaved segment directly to "out_file" one segment at a time
            // "Seek" to interleaver offset
            if (!out_file.Seek(interleaverOffset))
            {
                PLOG(PL_FATAL, "\nnpc: unexpected output file seek error: %s\n", GetErrorString());
                return false;
            }
            // And write segment to output file
            if (out_file.Write(segment, segment_size) != segment_size)
            {
                PLOG(PL_FATAL, "npc: unexpected error writing to output file: %s\n", GetErrorString());
                return false;
            }   
            outputSegmentId++;
             
        }
        if (++progressCounter >= progressThreshold)
        {
            if (progressPercent < 9)
                PLOG(PL_ALWAYS, "\b\b\b%d%%)", progressPercent + 1);
            else if (progressPercent < 99)
                PLOG(PL_ALWAYS, "\b\b\b\b%d%%)", progressPercent + 1);
            if (progressPercent < 99) progressPercent++;
            progressCounter = 0;
        }      
    } 
    if (progressPercent < 10)
        PLOG(PL_ALWAYS, "\b\b\b100%%)\n");
    else 
        PLOG(PL_ALWAYS, "\b\b\b\b100%%)\n");
    
    in_file.Close();
    out_file.Close();
    
    ProtoSystemTime(t2);
    
    // Deallocate our interleaver/parity buffers
    delete[] iBuffer;
    iBuffer = NULL;
    delete[] parityVec;
    parityVec = NULL;
    
    PLOG(PL_INFO, "NormPrecodeApp::Encode() encoding time: %ld usec\n", DIFF_T(t2, t1));
    
    return true;
            
}  // end NormPrecodeApp::Encode()

bool NormPrecodeApp::Decode()
{
    // 1) Determine file size and init interleaving
    NormFile::Offset inputFileSize = in_file.GetSize();
    NormFile::Offset numInputSegments = inputFileSize / segment_size;
    if (0 != (inputFileSize % segment_size))
    {
        PLOG(PL_FATAL, "npc: error: input file size not integral number of given <segmentSize>\n");
        return false;
    }
    // Reverse calculate the FEC blocking
    NormFile::Offset numFecBlocks = numInputSegments / (num_data + num_parity);
    unsigned int fecBlockSize = num_data;
    unsigned int lastFecBlockSize = (unsigned int)(numInputSegments % (num_data + num_parity));
    if (0 != lastFecBlockSize)
    {
        ASSERT(lastFecBlockSize > num_parity);
        lastFecBlockSize -= num_parity;
        numFecBlocks++;
    }
    NormFile::Offset lastFecBlockId = numFecBlocks - 1;
    // Calculate interleaver dimensions from file size
    // set "interleaver_size", etc
    InitInterleaver(numInputSegments);
    
    // 2) init FEC decoder
#ifdef USE_MDP_FEC
    NormDecoderMDP decoder;
#else
    NormDecoderRS8 decoder;
#endif // if/else USE_MDP_FEC
    unsigned int dataSegmentSize = segment_size - 4;  // leaves space for our CRC
    if (!decoder.Init(num_data, num_parity, dataSegmentSize))
    {
        PLOG(PL_FATAL, "npc: error initializing decoder\n");
        return false;   
    }
    
    
    // 3) allocate interleaving/FEC decoding buffer and erasure locs array
    // Determine number of segments to allocate for FEC encoding and
    // interleaver buffering if applicable
    char* iBuffer = NULL;
    bool useBuffering = false;
    NormFile::Offset interleaverBytes = interleaver_size * segment_size;
    
    if ((interleaverBytes <= i_buffer_max))// && 
        //((num_data + num_parity) <= interleaver_size) &&
        //((num_data + num_parity) <= numInputSegments))
    {
        // Allocate buffering for full interleaver block and parity
        PLOG(PL_INFO, "npc: allocating interleaver buffer ...\n");
        iBuffer = new char[interleaverBytes + ((num_data + num_parity) * segment_size)];
        if (NULL != iBuffer)
            useBuffering = true;
        else
            PLOG(PL_WARN, "npc: warning:  couldn't allocate full interleaver buffer: %s\n", GetErrorString());
    }
    if (NULL == iBuffer)
    {
        // Just try to allocate FEC vecs
        iBuffer = new char[(num_data + num_parity) * segment_size];
        if (NULL == iBuffer)
        {
            PLOG(PL_FATAL, "npc: error: couldn't allocate parity buffer: %s\n", GetErrorString());
            return false;
        }
    }      
    
    char** fecVec = new char*[num_data + num_parity];
    if (NULL == fecVec)
    {
        PLOG(PL_FATAL, "npc: error: couldn't allocate parity buffer: %s\n", GetErrorString());
        return false;
    }
    if (!useBuffering)
    {
        // Go ahead and assign
        char* fptr = iBuffer;
        for (unsigned int i = 0; i < (num_data + num_parity); i++)
        {
            fecVec[i] = fptr;
            fptr += segment_size;
        }
    }
     
    
    unsigned int* erasureLocs = new unsigned int[num_parity];
    if (NULL == erasureLocs)
    {
        PLOG(PL_FATAL, "npc: new erasure location array error: %s\n", GetErrorString());
        return false;
    }
    
    
    PLOG(PL_FATAL, "npc: decoding file ... (progress:   0%%)");
    // State to track/display decoding progress
    NormFile::Offset progressThreshold = numInputSegments / 100;
    double progressIncrement = 100.0;
    if (progressThreshold > 1)
         progressIncrement = (double)numInputSegments / (double)progressThreshold;
    else
        progressThreshold = numInputSegments; // small number of segments
    NormFile::Offset progressCounter = 0;
    int progressPercent = 0;
    
    
    // Read and decode each block in "in_file"
    NormFile::Offset fecBlockId = 0;
    NormFile::Offset outFileSize = 0;
    
    NormFile::Offset lastInterleaverBlockId = numInputSegments / interleaver_size;
    NormFile::Offset lastInterleaverBytes = (numInputSegments % interleaver_size) * segment_size;
    NormFile::Offset interleaverBlockId = 0;
    
    unsigned int erasureCount = 0;
    unsigned int segmentCount = 0;
    
    enum State {READING, ADVANCING, DECODING};
    State state = READING;
    NormFile::Offset inputSegmentId = 0;
    while ((inputSegmentId < numInputSegments) || (DECODING == state))
    {
        switch (state)
        {
            case READING:
                if (useBuffering)
                {
                    // Read in a full interleaver block
                    NormFile::Offset bytesToRead; 
                    if (interleaverBlockId != lastInterleaverBlockId)
                        bytesToRead = interleaverBytes;
                    else
                        bytesToRead = lastInterleaverBytes;
                    if (in_file.Read(iBuffer, bytesToRead) != bytesToRead)
                    {
                        PLOG(PL_FATAL, "\nnpc: error reading input file: %s\n", GetErrorString());
                        return false;
                    }
                    interleaverBlockId++;
                    state = ADVANCING;
                }
                else
                {
                    // Read in a FEC block one segment at a time.
                    unsigned int numData = (fecBlockId != lastFecBlockId) ? fecBlockSize : lastFecBlockSize;
                    for (unsigned int i = 0 ; i < (numData + num_parity); i++)
                    {
                        // Calc offset (de-interleave) and seek
                        NormFile::Offset interleaverOffset = ComputeInterleaverOffset(inputSegmentId, numInputSegments);
                        // seek to interleaver offset
                        if (!in_file.Seek(interleaverOffset))
                        {
                            PLOG(PL_FATAL, "\nnpc: unexpected input file seek error: %s\n", GetErrorString());
                            return false;
                        }
                        // Read segment
                        if (in_file.Read(fecVec[i], segment_size) != segment_size)
                        {
                            PLOG(PL_FATAL, "\nnpc: unexpected error reading input file: %s\n", GetErrorString());
                            return false;
                        }
                        inputSegmentId++;
                        
                        // Validate checksum (detects errors/ erasures)
                        UINT32 checksum = ComputeCRC32(fecVec[i], dataSegmentSize);
                        checksum = htonl(checksum);
                        if (0 != memcmp(&checksum, fecVec[i] + dataSegmentSize, 4))
                        {
                            PLOG(PL_TRACE, "\nnpc: bad checksum! (found erasure)\n");
                            erasureLocs[erasureCount++] = i;
                            if (erasureCount > num_parity)
                            {
                                PLOG(PL_FATAL, "\nnpc: decoding encountered block with too many errors!\n");
                                return false;
                            }
                            memset(fecVec[i], 0, dataSegmentSize);
                        }
                    }
                    state = DECODING;
                }
                break;
            
            case ADVANCING:
            {
                ASSERT(useBuffering);
                unsigned int numData = (fecBlockId != lastFecBlockId) ? fecBlockSize : lastFecBlockSize;
                unsigned int priorSegmentCount = segmentCount;
                for (; segmentCount < (numData + num_parity); segmentCount++)
                {
                    NormFile::Offset interleaverOffset = ComputeInterleaverOffset(inputSegmentId, numInputSegments);
                    fecVec[segmentCount] = iBuffer + (interleaverOffset % interleaverBytes);
                    inputSegmentId++;
                    // Validate checksum (detects errors/ erasures)
                    UINT32 checksum = ComputeCRC32(fecVec[segmentCount], dataSegmentSize);
                    checksum = htonl(checksum);
                    if (0 != memcmp(&checksum, fecVec[segmentCount] + dataSegmentSize, 4))
                    {
                        PLOG(PL_TRACE, "\nnpc: bad checksum! (found erasure)\n");
                        erasureLocs[erasureCount++] = segmentCount;
                        if (erasureCount > num_parity)
                        {
                            PLOG(PL_FATAL, "\nnpc: decoding encountered block with too many errors!\n");
                            return false;
                        }
                        memset(fecVec[segmentCount], 0, dataSegmentSize);
                    }
                    if (0 == (inputSegmentId % interleaver_size)) 
                    {
                        if ((segmentCount + 1) == (numData + num_parity))
                            segmentCount++;  // we really finished
                        break;    
                    }
                }
                if (segmentCount < (numData + num_parity))
                {
                    segmentCount++;
                    // We only got a partial FEC block, so copy remainder to our extra storage space
                    char* fptr = iBuffer + interleaverBytes + (priorSegmentCount * segment_size);
                    for (unsigned int i = priorSegmentCount; i < segmentCount; i++)
                    {
                        memcpy(fptr, fecVec[i], segment_size);
                        fecVec[i] = fptr;
                        fptr += segment_size;

                    }
                    state = READING;  // we need more data from file to complete fec block
                }
                else
                {
                    state = DECODING;
                }
                break;
            }
                
            case DECODING:
            {
                // Now decode
                unsigned int numData = (fecBlockId != lastFecBlockId) ? fecBlockSize : lastFecBlockSize;
                if (0 != erasureCount)
                    decoder.Decode(fecVec, numData, erasureCount, erasureLocs);
                for (unsigned int i = 0; i < numData; i++)
                {
                    unsigned int segmentSize = segment_size - 4;  // don't write the CRC tail
                    if((0 == fecBlockId) && (0 == i))
                    {
                        // First segment of first block is our "meta_data" with file size info   
                        switch (sizeof(NormFile::Offset))
                        {
                            case 8:
                                memcpy(&outFileSize, fecVec[0], 8);
                                outFileSize = ntoho(outFileSize);
                                break;
                            case 4:
                                memcpy(&outFileSize, fecVec[0] + 4, 4);
                                outFileSize = ntoho(outFileSize);
                                break;
                            default:
                                PLOG(PL_FATAL, "\nnpc: error: unsupported file offset size\n");
                                return false;
                        }
                        if (!out_file.IsOpen())
                        {
                            // Use meta-data file name
                            char outFileName[PATH_MAX+1];
                            unsigned int maxLen = (PATH_MAX < (segment_size - 12)) ? PATH_MAX : (segment_size - 12);
                            outFileName[maxLen] = '\0';
                            strncpy(outFileName, fecVec[0]+8, maxLen);  
                            if (!out_file.Open(outFileName, O_WRONLY | O_CREAT | O_TRUNC))
                            {
                                PLOG(PL_FATAL, "\nnpc: error opening output file: %s\n", GetErrorString());
                                return false;
                            } 
                        }
                        continue;
                    }
                    else if ((lastFecBlockId == fecBlockId) && ((numData - 1) == i))
                    {
                        // Last segment, so calculate "lastSegmentSize"
                        segmentSize = (unsigned int)(outFileSize % segmentSize); 
                    }
                    if (out_file.Write(fecVec[i], segmentSize) != segmentSize)
                    {
                        PLOG(PL_FATAL, "\nnpc: unexpected error writing to output file: %s\n", GetErrorString());
                        return false;
                    }
                }
                erasureCount = 0;
                segmentCount = 0;
                fecBlockId++;
                state = useBuffering ? ADVANCING : READING;
                break;
            }
                
        }  // end switch (state)
        
        if (++progressCounter >= progressThreshold)
        {
            if (progressPercent < 9)
                PLOG(PL_ALWAYS, "\b\b\b%d%%)", progressPercent + 1);
            else if (progressPercent < 99)
                PLOG(PL_ALWAYS, "\b\b\b\b%d%%)", progressPercent + 1);
            if (progressPercent < 99) progressPercent++;
            progressCounter = 0;
        }    
        
    }  // end while (inputSegmentId < numInputSegments)
    
    if (progressPercent < 10)
        PLOG(PL_ALWAYS, "\b\b\b100%%)\n");
    else 
        PLOG(PL_ALWAYS, "\b\b\b\b100%%)\n");
    
    // Cleanup, cleanup
    delete[] iBuffer;
    delete[] fecVec;
    delete[] erasureLocs;
    
    return true;
}  // end NormPrecodeApp::Decode()




/*****************************************************************/
/*                                                               */
/* CRC LOOKUP TABLE                                              */
/* ================                                              */
/* The following CRC lookup table was generated automagically    */
/* by the Rocksoft^tm Model CRC Algorithm Table Generation       */
/* Program V1.0 using the following model parameters:            */
/*                                                               */
/*    Width   : 4 bytes.                                         */
/*    Poly    : 0x04C11DB7L                                      */
/*    Reverse : TRUE.                                            */
/*                                                               */
/* For more information on the Rocksoft^tm Model CRC Algorithm,  */
/* see the document titled "A Painless Guide to CRC Error        */
/* Detection Algorithms" by Ross Williams                        */
/* (ross@guest.adelaide.edu.au.). This document is likely to be  */
/* in the FTP archive "ftp.adelaide.edu.au/pub/rocksoft".        */
/*                                                               */
/*****************************************************************/

const UINT32 NormPrecodeApp::CRC32_TABLE[256] =
{
 0x00000000L, 0x77073096L, 0xEE0E612CL, 0x990951BAL,
 0x076DC419L, 0x706AF48FL, 0xE963A535L, 0x9E6495A3L,
 0x0EDB8832L, 0x79DCB8A4L, 0xE0D5E91EL, 0x97D2D988L,
 0x09B64C2BL, 0x7EB17CBDL, 0xE7B82D07L, 0x90BF1D91L,
 0x1DB71064L, 0x6AB020F2L, 0xF3B97148L, 0x84BE41DEL,
 0x1ADAD47DL, 0x6DDDE4EBL, 0xF4D4B551L, 0x83D385C7L,
 0x136C9856L, 0x646BA8C0L, 0xFD62F97AL, 0x8A65C9ECL,
 0x14015C4FL, 0x63066CD9L, 0xFA0F3D63L, 0x8D080DF5L,
 0x3B6E20C8L, 0x4C69105EL, 0xD56041E4L, 0xA2677172L,
 0x3C03E4D1L, 0x4B04D447L, 0xD20D85FDL, 0xA50AB56BL,
 0x35B5A8FAL, 0x42B2986CL, 0xDBBBC9D6L, 0xACBCF940L,
 0x32D86CE3L, 0x45DF5C75L, 0xDCD60DCFL, 0xABD13D59L,
 0x26D930ACL, 0x51DE003AL, 0xC8D75180L, 0xBFD06116L,
 0x21B4F4B5L, 0x56B3C423L, 0xCFBA9599L, 0xB8BDA50FL,
 0x2802B89EL, 0x5F058808L, 0xC60CD9B2L, 0xB10BE924L,
 0x2F6F7C87L, 0x58684C11L, 0xC1611DABL, 0xB6662D3DL,
 0x76DC4190L, 0x01DB7106L, 0x98D220BCL, 0xEFD5102AL,
 0x71B18589L, 0x06B6B51FL, 0x9FBFE4A5L, 0xE8B8D433L,
 0x7807C9A2L, 0x0F00F934L, 0x9609A88EL, 0xE10E9818L,
 0x7F6A0DBBL, 0x086D3D2DL, 0x91646C97L, 0xE6635C01L,
 0x6B6B51F4L, 0x1C6C6162L, 0x856530D8L, 0xF262004EL,
 0x6C0695EDL, 0x1B01A57BL, 0x8208F4C1L, 0xF50FC457L,
 0x65B0D9C6L, 0x12B7E950L, 0x8BBEB8EAL, 0xFCB9887CL,
 0x62DD1DDFL, 0x15DA2D49L, 0x8CD37CF3L, 0xFBD44C65L,
 0x4DB26158L, 0x3AB551CEL, 0xA3BC0074L, 0xD4BB30E2L,
 0x4ADFA541L, 0x3DD895D7L, 0xA4D1C46DL, 0xD3D6F4FBL,
 0x4369E96AL, 0x346ED9FCL, 0xAD678846L, 0xDA60B8D0L,
 0x44042D73L, 0x33031DE5L, 0xAA0A4C5FL, 0xDD0D7CC9L,
 0x5005713CL, 0x270241AAL, 0xBE0B1010L, 0xC90C2086L,
 0x5768B525L, 0x206F85B3L, 0xB966D409L, 0xCE61E49FL,
 0x5EDEF90EL, 0x29D9C998L, 0xB0D09822L, 0xC7D7A8B4L,
 0x59B33D17L, 0x2EB40D81L, 0xB7BD5C3BL, 0xC0BA6CADL,
 0xEDB88320L, 0x9ABFB3B6L, 0x03B6E20CL, 0x74B1D29AL,
 0xEAD54739L, 0x9DD277AFL, 0x04DB2615L, 0x73DC1683L,
 0xE3630B12L, 0x94643B84L, 0x0D6D6A3EL, 0x7A6A5AA8L,
 0xE40ECF0BL, 0x9309FF9DL, 0x0A00AE27L, 0x7D079EB1L,
 0xF00F9344L, 0x8708A3D2L, 0x1E01F268L, 0x6906C2FEL,
 0xF762575DL, 0x806567CBL, 0x196C3671L, 0x6E6B06E7L,
 0xFED41B76L, 0x89D32BE0L, 0x10DA7A5AL, 0x67DD4ACCL,
 0xF9B9DF6FL, 0x8EBEEFF9L, 0x17B7BE43L, 0x60B08ED5L,
 0xD6D6A3E8L, 0xA1D1937EL, 0x38D8C2C4L, 0x4FDFF252L,
 0xD1BB67F1L, 0xA6BC5767L, 0x3FB506DDL, 0x48B2364BL,
 0xD80D2BDAL, 0xAF0A1B4CL, 0x36034AF6L, 0x41047A60L,
 0xDF60EFC3L, 0xA867DF55L, 0x316E8EEFL, 0x4669BE79L,
 0xCB61B38CL, 0xBC66831AL, 0x256FD2A0L, 0x5268E236L,
 0xCC0C7795L, 0xBB0B4703L, 0x220216B9L, 0x5505262FL,
 0xC5BA3BBEL, 0xB2BD0B28L, 0x2BB45A92L, 0x5CB36A04L,
 0xC2D7FFA7L, 0xB5D0CF31L, 0x2CD99E8BL, 0x5BDEAE1DL,
 0x9B64C2B0L, 0xEC63F226L, 0x756AA39CL, 0x026D930AL,
 0x9C0906A9L, 0xEB0E363FL, 0x72076785L, 0x05005713L,
 0x95BF4A82L, 0xE2B87A14L, 0x7BB12BAEL, 0x0CB61B38L,
 0x92D28E9BL, 0xE5D5BE0DL, 0x7CDCEFB7L, 0x0BDBDF21L,
 0x86D3D2D4L, 0xF1D4E242L, 0x68DDB3F8L, 0x1FDA836EL,
 0x81BE16CDL, 0xF6B9265BL, 0x6FB077E1L, 0x18B74777L,
 0x88085AE6L, 0xFF0F6A70L, 0x66063BCAL, 0x11010B5CL,
 0x8F659EFFL, 0xF862AE69L, 0x616BFFD3L, 0x166CCF45L,
 0xA00AE278L, 0xD70DD2EEL, 0x4E048354L, 0x3903B3C2L,
 0xA7672661L, 0xD06016F7L, 0x4969474DL, 0x3E6E77DBL,
 0xAED16A4AL, 0xD9D65ADCL, 0x40DF0B66L, 0x37D83BF0L,
 0xA9BCAE53L, 0xDEBB9EC5L, 0x47B2CF7FL, 0x30B5FFE9L,
 0xBDBDF21CL, 0xCABAC28AL, 0x53B39330L, 0x24B4A3A6L,
 0xBAD03605L, 0xCDD70693L, 0x54DE5729L, 0x23D967BFL,
 0xB3667A2EL, 0xC4614AB8L, 0x5D681B02L, 0x2A6F2B94L,
 0xB40BBE37L, 0xC30C8EA1L, 0x5A05DF1BL, 0x2D02EF8DL
};  // end NormPrecodeApp::CRC32_TABLE

UINT32 NormPrecodeApp::ComputeCRC32(const char* buffer, unsigned int buflen)
{
    const UINT32 CRC32_XINIT = 0xFFFFFFFFL; // initial value
    const UINT32 CRC32_XOROT = 0xFFFFFFFFL; // final xor value 
    UINT32 result = CRC32_XINIT;
    for (unsigned int i = 0; i < buflen; i++)
        result = CRC32_TABLE[(result ^ *buffer++) & 0xFFL] ^ (result >> 8);
    // return XOR out value 
    result ^= CRC32_XOROT;
    ASSERT(0 != result);
    return result;
}  // end NormPrecodeApp::ComputeCRC32()
