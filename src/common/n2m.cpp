// n2m.cpp - grab NORM packet "trace" lines from a NORM debug log
//           and transform them into pseudo MGEN log format
//           so our existing "trpr" program can be used for analyses

// This currently only fully function when there is a single sender and
// a single object is in the NORM log file

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifndef WIN32
#include <unistd.h>      
#include <sys/time.h>  // for gettimeofday()
#include <errno.h>
#endif // !WIN32

class FastReader
{
    public:
        enum Result {OK, ERROR_, DONE, TIMEOUT};
        FastReader();
        FastReader::Result Read(FILE* filePtr, char* buffer, unsigned int* len, 
                                double timeout = -1.0);
        FastReader::Result Readline(FILE* filePtr, char* buffer, unsigned int* len,
                                    double timeout = -1.0);

    private:
        enum {BUFSIZE = 2048};
        char         savebuf[BUFSIZE];
        char*        saveptr;
        unsigned int savecount;
};  // end class FastReader

#ifndef MIN
#define MIN(X,Y) ((X<Y)?X:Y)
#define MAX(X,Y) ((X>Y)?X:Y)
#endif // !MIN

void Usage()
{
    fprintf(stderr, "Usage:  n2m [data <blkSize>][input <logFile>\n");
}

int main(int argc, char* argv[])
{
    FastReader reader;
    unsigned int line = 0;
    int status = 0;
    
    bool firstRecvEvent = true;
    bool firstSendEvent = true;
    unsigned int lastSendSeq = 0;
    unsigned int lastRecvSeq = 0;
    unsigned int sendSeqOffset = 0;
    unsigned int recvSeqOffset = 0;
    
    FILE* infile = stdin;
    
    bool dataSeq = false;
    int blkSize = 0;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp("data", argv[i]))
        {
            
            i++;
            if (i < argc)
            {
                blkSize = atoi(argv[i]); 
                if (blkSize <= 0)
                {
                    fprintf(stderr, "n2m error: invalid block size\n");
                    return -1;
                }
            }
            else
            {
                fprintf(stderr, "Usage:  n2m [data <blkSize>]\n");
                return -1;
            }
            dataSeq = true;   
        }
        else if (!strcmp("input", argv[i]))
        {
            i++;
            if (NULL == (infile = fopen(argv[i], "r")))
            {
                fprintf(stderr, "Usage:  n2m [data <blkSize>]\n");
                return -1;
            }            
        }
        
    }
    
    while (1)
    {
        unsigned int numBytes = 1024;
        char buffer[1024];
        FastReader::Result result = reader.Readline(infile, buffer, &numBytes);
        if (FastReader::DONE == result)
        {
            break;
        }
        else if (FastReader::ERROR_ == result)
        {
            perror("n2m: error reading log");
            status = -1;
            break;
        }
        line++;
        // Make sure it is a "trace" line
        if (0 == numBytes) 
            continue;
        else if (0 != strncmp(buffer, "trace>", 6)) 
            continue;
        
        // Get the event time
        unsigned int hr, min;
        double sec;
        char* ptr = buffer + 6;
        if (3 != sscanf(ptr, "%u:%u:%lf", &hr, &min, &sec))
        {
            fprintf(stderr, "n2m: invalid trace \"time\" at line %u\n", line);
            break;
        }
        
        // RECV or SEND event?
        bool recvEvent = false;
        char* ptr2 = strstr(ptr, "src>");
        if (NULL != ptr2)
        {
            recvEvent = true;
            
        }
        else if (NULL == (ptr2 = strstr(ptr, "dst>")))
        {
            fprintf(stderr, "n2m: invalid trace \"src|dst\" at line %u\n", line);
            break;
        }
        ptr = ptr2 + 4;
        
        // Get address string
        char addr[64];
        if (1 != sscanf(ptr, "%s", addr))
        {
            fprintf(stderr, "n2m: invalid trace \"%s address\" at line %u\n", 
                            recvEvent ? "src" : "dst", line);
            break;
        }
        
        // Get sequence value
        unsigned int seq = 0;
        if (NULL != (ptr2 = strstr(ptr, "seq>")))
        {
            ptr2 += 4;
            if (1 != sscanf(ptr2, "%u", &seq))
            {
                fprintf(stderr, "n2m: invalid trace \"seq\" at line %u\n", line);
                break;
            }
            ptr = ptr2;
        }
        else
        {
            //fprintf(stderr, "n2m: no trace \"seq\" at line %u\n", line);
            seq = recvEvent ? lastRecvSeq : lastSendSeq;
        }
        
        
        if (recvEvent)
        {
            if (firstRecvEvent)
            {
                firstRecvEvent = false;
                recvSeqOffset = 0;
            }
            else
            {
                int delta = seq - lastRecvSeq;
                if ((delta < -100) || (delta > 32000))
                    recvSeqOffset += 65536;
            }
            lastRecvSeq = seq;
            seq += recvSeqOffset;
        }
        else
        {
            if (firstSendEvent)
            {
                firstSendEvent = false;
                sendSeqOffset = 0;
            }
            else
            {
                int delta = seq - lastSendSeq;
                if ((delta < -100) || (delta > 32000))
                    sendSeqOffset += 65536;
            }
            lastSendSeq = seq;
            seq += sendSeqOffset;
        }
        
        
        if (dataSeq)
        {
            // Only use DATA/PRTY packet! (this is only good for a single object!)
            ptr2 = strstr(ptr, "DATA");
            if (NULL == ptr2)
                ptr2 = strstr(ptr, "PRTY");
            if (NULL == ptr2) continue;  // skip to next line
            ptr = ptr2 + 5;
            // Calc seq from blk and seq
            unsigned int obj, blk, seg;
            if (3 != sscanf(ptr, "obj>%u blk>%u seg>%u", &obj, &blk, &seg))
            {
                fprintf(stderr, "n2m: invalid trace <obj:blk:seg> at line %u\n", line);
                break;
            }
            seq = blk * blkSize + seg;
            
            //int objDelta = (lastObj >= 0) ? (int)obj - lastObj : 
            
        }
        
        // Get len value
        if (NULL == (ptr2 = strstr(ptr, "len>")))
        {
            fprintf(stderr, "n2m: no trace \"len\" at line %u\n", line);
            break;
        }
        ptr = ptr2 + 4;
        unsigned int length;
        if (1 != sscanf(ptr, "%u", &length))
        {
            fprintf(stderr, "n2m: invalid trace \"len\" at line %u\n", line);
            break;
        }
        
        
        // Finally, output an MGEN log line
        
        
        if (recvEvent)
            printf("%u:%u:%lf RECV flow>0 seq>%u src>%s/0 dst>127.0.0.1/0 sent>%u:%u:%lf size>%u\n", 
                   hr, min, sec, seq, addr, hr, min, sec, length);
        else
             printf("%u:%u:%lf SEND flow>0 seq>%u dst>%s/0 size>%u\n", 
                    hr, min, sec, seq, addr, length);
        fflush(stdout);
    }
    
    if (infile != stdin) fclose(infile);
    return status;
}


FastReader::FastReader()
    : savecount(0)
{
    
}

FastReader::Result FastReader::Read(FILE*           filePtr, 
                                    char*           buffer, 
                                    unsigned int*   len,
                                    double          timeout)
{
    unsigned int want = *len;   
    if (savecount)
    {
        unsigned int ncopy = MIN(want, savecount);
        memcpy(buffer, saveptr, ncopy);
        savecount -= ncopy;
        saveptr += ncopy;
        buffer += ncopy;
        want -= ncopy;
    }
    while (want)
    {
        unsigned int result;
#ifndef WIN32 // no real-time TRPR for WIN32 yet
        if (timeout >= 0.0)
        {
            int fd = fileno(filePtr);
            fd_set input;
            FD_ZERO(&input);
            struct timeval t;
            t.tv_sec = (unsigned long)timeout;
            t.tv_usec = (unsigned long)((1.0e+06 * (timeout - (double)t.tv_sec)) + 0.5);
            FD_SET(fd, &input);
            int status = select(fd+1, &input, NULL, NULL, &t);
            switch(status)
            {
                case -1:
                    if (EINTR != errno) 
                    {
                        perror("trpr: FastReader::Read() select() error");
                        return ERROR_; 
                    }
                    else
                    {
                        continue;   
                    }
                    break;
                    
                case 0:
                    return TIMEOUT;
                    
                default:
                    result = fread(savebuf, sizeof(char), 1, filePtr);
                    break;
            } 
        }
        else
#endif // !WIN32
        {
            // Perform buffered read when there is no "timeout"
            result = fread(savebuf, sizeof(char), BUFSIZE, filePtr);
            // This check skips NULLs that have been read on some
            // use of trpr via tail from an NFS mounted file
            if (!isprint(*savebuf) && 
                ('\n' != *savebuf) && 
                ('\r' != *savebuf))
                    continue;
        }
        if (result)
        {
            unsigned int ncopy= MIN(want, result);
            memcpy(buffer, savebuf, ncopy);
            savecount = result - ncopy;
            saveptr = savebuf + ncopy;
            buffer += ncopy;
            want -= ncopy;
        }
        else  // end-of-file
        {
#ifndef WIN32
            if (ferror(filePtr))
            {
                if (EINTR == errno) continue;   
            }
#endif // !WIN32
            *len -= want;
            if (*len)
                return OK;  // we read at least something
            else
                return DONE; // we read nothing
        }
    }  // end while(want)
    return OK;
}  // end FastReader::Read()

// An OK text readline() routine (reads what will fit into buffer incl. NULL termination)
// if *len is unchanged on return, it means the line is bigger than the buffer and 
// requires multiple reads

FastReader::Result FastReader::Readline(FILE*         filePtr, 
                                        char*         buffer, 
                                        unsigned int* len, 
                                        double        timeout)
{   
    unsigned int count = 0;
    unsigned int length = *len;
    char* ptr = buffer;
    while (count < length)
    {
        unsigned int one = 1;
        switch (Read(filePtr, ptr, &one, timeout))
        {
            case OK:
                if (('\n' == *ptr) || ('\r' == *ptr))
                {
                    *ptr = '\0';
                    *len = count;
                    return OK;
                }
                count++;
                ptr++;
                break;
                
            case TIMEOUT:
                // On timeout, save any partial line collected
                if (count)
                {
                    savecount = MIN(count, BUFSIZE);
                    if (count < BUFSIZE)
                    {
                        memcpy(savebuf, buffer, count);
                        savecount = count;
                        saveptr = savebuf;
                        *len = 0;
                    }
                    else
                    {
                        memcpy(savebuf, buffer+count-BUFSIZE, BUFSIZE);
                        savecount = BUFSIZE;
                        saveptr = savebuf;
                        *len = count - BUFSIZE;
                    }
                }
                return TIMEOUT;
                
            case ERROR_:
                return ERROR_;
                
            case DONE:
                return DONE;
        }
    }
    // We've filled up the buffer provided with no end-of-line 
    return ERROR_;
}  // end stReader::Result FastReader::Readline()
