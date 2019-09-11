
// norm.cpp - Command-line NORM application

#include "protoLib.h"
#include "normSession.h"

#include <stdio.h>   // for stdout/stderr printouts
#include <signal.h>  // for SIGTERM/SIGINT handling
#include <errno.h>


// Command-line application using Protolib EventDispatcher
class NormApp : public NormController
{
    public:
        NormApp();
        virtual ~NormApp();
		bool OnStartup();
        int MainLoop() {return dispatcher.Run();}
        void Stop(int exitCode) {dispatcher.Stop(exitCode);}
        void OnShutdown();
        
        bool ProcessCommand(const char* cmd, const char* val);
		bool ParseCommandLine(int argc, char* argv[]);
        
    private:
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        CmdType CommandType(const char* cmd);
    
        virtual void Notify(NormController::Event event,
                        class NormSessionMgr* sessionMgr,
                        class NormSession*    session,
                        class NormServerNode* server,
                        class NormObject*     object);
        
        void InstallTimer(ProtocolTimer* timer) 
            {dispatcher.InstallTimer(timer);}
        bool OnIntervalTimeout();
    
        static const char* const cmd_list[];

        static void SignalHandler(int sigNum);
    
        EventDispatcher     dispatcher;        
        NormSessionMgr      session_mgr;
        NormSession*        session;
        NormStreamObject*   stream;
        
        // application parameters
        FILE*               input;  // input stream
        FILE*               output; // output stream
        char                input_buffer[512];
        unsigned int        input_index;
        unsigned int        input_length;
        
        // NormSession parameters
        char*               address;        // session address
        UINT16              port;           // session port number
        UINT8               ttl;
        double              tx_rate;        // bits/sec
        UINT16              segment_size;
        UINT8               ndata;
        UINT8               nparity;
        UINT8               auto_parity;
        unsigned long       tx_buffer_size; // bytes
        unsigned long       rx_buffer_size; // bytes
            
        NormFileList        tx_file_list;
        double              tx_object_interval;
        int                 tx_repeat_count;
        double              tx_repeat_interval;
        NormFileList        rx_file_cache;
        char*               rx_cache_path;
        
        ProtocolTimer       interval_timer;  
        
        // protocol debug parameters
        bool                tracing;
        double              tx_loss;
        double              rx_loss;
    
}; // end class NormApp

NormApp::NormApp()
 : session(NULL), stream(NULL), input(NULL), output(NULL), 
   input_index(0), input_length(0),
   address(NULL), port(0), ttl(3), tx_rate(64000.0),
   segment_size(1024), ndata(32), nparity(16), auto_parity(0),
   tx_buffer_size(1024*1024), rx_buffer_size(1024*1024),
   tx_object_interval(0.0), tx_repeat_count(0), tx_repeat_interval(0.0),
   rx_cache_path(NULL),
   tracing(false), tx_loss(0.0), rx_loss(0.0)
{
    // Init tx_timer for 1.0 second interval, infinite repeats
    session_mgr.Init(EventDispatcher::TimerInstaller, &dispatcher,
                     EventDispatcher::SocketInstaller, &dispatcher,
                     this);    
    interval_timer.Init(0.0, 0, (ProtocolTimerOwner*)this, 
                        (ProtocolTimeoutFunc)&NormApp::OnIntervalTimeout);
}

NormApp::~NormApp()
{
    if (address) delete address;
    if (rx_cache_path) delete rx_cache_path;
}


const char* const NormApp::cmd_list[] = 
{
    "+debug",
    "+log",
    "-trace",
    "+txloss",
    "+rxloss",
    "+address",
    "+ttl",
    "+rate",
    "+input",
    "+output",
    "+sendfile",
    "+interval",
    "+repeatcount",
    "+rinterval",  // repeat interval
    "+rxcachedir",
    "+segment",
    "+block",
    "+parity",
    "+auto",
    "+txbuffer",
    "+rxbuffer",
    NULL         
};
    
bool NormApp::ProcessCommand(const char* cmd, const char* val)
{
    CmdType type = CommandType(cmd);
    ASSERT(CMD_INVALID != type);
    unsigned int len = strlen(cmd);
    if ((CMD_ARG == type) && !val)
    {
        DMSG(0, "NormApp::ProcessCommand(%s) missing argument\n", cmd);
        return false;        
    }
    
    if (!strncmp("debug", cmd, len))
    {
        
        int debugLevel = atoi(val);
        if ((debugLevel < 0) || (debugLevel > 12))
        {
            DMSG(0, "NormApp::ProcessCommand(segment) invalid debug level!\n");   
            return false;
        }
        SetDebugLevel(debugLevel);
    }
    else if (!strncmp("log", cmd, len))
    {
        OpenDebugLog(val);
    }
    else if (!strncmp("trace", cmd, len))
    {
        tracing = true;
        if (session) session->SetTrace(true);
    }
    else if (!strncmp("txloss", cmd, len))
    {
        double txLoss = atof(val);
        if (txLoss < 0)
        {
            DMSG(0, "NormApp::ProcessCommand(txloss) invalid txRate!\n");   
            return false;
        }
        tx_loss = txLoss;
        if (session) session->SetTxLoss(txLoss);
    }
    else if (!strncmp("rxloss", cmd, len))
    {
        double rxLoss = atof(val);
        if (rxLoss < 0)
        {
            DMSG(0, "NormApp::ProcessCommand(rxloss) invalid txRate!\n");   
            return false;
        }
        rx_loss = rxLoss;
        if (session) session->SetRxLoss(rxLoss);
    }
    else if (!strncmp("address", cmd, len))
    {
        unsigned int len = strlen(val);
        if (address) delete address;
        if (!(address = new char[len+1]))
        {
            DMSG(0, "NormApp::ProcessCommand(address) allocation error:%s\n",
                strerror(errno)); 
            return false;
        }
        strcpy(address, val);
        char* ptr = strchr(address, '/');
        if (!ptr)
        {
            delete address;
            address = NULL;
            DMSG(0, "NormApp::ProcessCommand(address) missing port number!\n");   
            return false;
        }
        *ptr++ = '\0';
        int portNum = atoi(ptr);
        if ((portNum < 1) || (portNum > 65535))
        {
            delete address;
            address = NULL;
            DMSG(0, "NormApp::ProcessCommand(address) invalid port number!\n");   
            return false;
        }
        port = portNum;
    }
    else if (!strncmp("ttl", cmd, len))
    {
        int ttlTemp = atoi(val);
        if ((ttlTemp < 1) || (ttlTemp > 255))
        {
            DMSG(0, "NormApp::ProcessCommand(ttl) invalid value!\n");   
            return false;
        }
        ttl = ttlTemp;
    }
    else if (!strncmp("rate", cmd, len))
    {
        double txRate = atof(val);
        if (txRate < 0)
        {
            DMSG(0, "NormApp::ProcessCommand(rate) invalid txRate!\n");   
            return false;
        }
        tx_rate = txRate;
        if (session) session->SetTxRate(txRate);
    }
    else if (!strncmp("input", cmd, len))
    {
        if (!(input = fopen(val, "rb")))
        {
            DMSG(0, "NormApp::ProcessCommand(input) fopen() error: %s\n",
                    strerror(errno));
            return false;   
        }
    }
    else if (!strncmp("output", cmd, len))
    {
        if (!(output = fopen(val, "wb")))
        {
            DMSG(0, "NormApp::ProcessCommand(output) fopen() error: %s\n",
                    strerror(errno));
            return false;   
        }
    }
    else if (!strncmp("sendfile", cmd, len))
    {
        if (!tx_file_list.Append(val))
        {
            DMSG(0, "NormApp::ProcessCommand(sendfile) Error appending \"%s\" "
                    "to tx file list.\n", val);
            return false;   
        }
    }
    else if (!strncmp("interval", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &tx_object_interval)) 
            tx_object_interval = -1.0;
        if (tx_object_interval < 0.0)
        {
            DMSG(0, "NormApp::ProcessCommand(interval) Invalid tx object interval: %s\n",
                     val);
            tx_object_interval = 0.0;
            return false;
        }
    }    
    else if (!strncmp("repeat", cmd, len))
    {
        tx_repeat_count = atoi(val);  
    }
    else if (!strncmp("rinterval", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &tx_repeat_interval)) 
            tx_repeat_interval = -1.0;
        if (tx_repeat_interval < 0.0)
        {
            DMSG(0, "NormApp::ProcessCommand(rinterval) Invalid tx repeat interval: %s\n",
                     val);
            tx_repeat_interval = 0.0;
            return false;
        }
    }   
    else if (!strncmp("rxcachedir", cmd, len))
    {
        unsigned int length = strlen(val);   
        // Make sure there is a trailing DIR_DELIMITER
        if (DIR_DELIMITER != val[length-1]) 
            length += 2;
        else
            length += 1;
        if (!(rx_cache_path = new char[length]))
        {
             DMSG(0, "NormApp::ProcessCommand(rxcachedir) alloc error: %s\n",
                    strerror(errno));
            return false;  
        }
        strcpy(rx_cache_path, val);
        rx_cache_path[length-2] = DIR_DELIMITER;
        rx_cache_path[length-1] = '\0';
    }
    else if (!strncmp("segment", cmd, len))
    {
        int segmentSize = atoi(val);
        if ((segmentSize < 0) || (segmentSize > 8000))
        {
            DMSG(0, "NormApp::ProcessCommand(segment) invalid segment size!\n");   
            return false;
        }
        segment_size = segmentSize;
    }
    else if (!strncmp("block", cmd, len))
    {
        int blockSize = atoi(val);
        if ((blockSize < 1) || (blockSize > 255))
        {
            DMSG(0, "NormApp::ProcessCommand(block) invalid block size!\n");   
            return false;
        }
        ndata = blockSize;
    }
    else if (!strncmp("parity", cmd, len))
    {
        int numParity = atoi(val);
        if ((numParity < 0) || (numParity > 254))
        {
            DMSG(0, "NormApp::ProcessCommand(parity) invalid value!\n");   
            return false;
        }
        nparity = numParity;
    }
    else if (!strncmp("auto", cmd, len))
    {
        int autoParity = atoi(val);
        if ((autoParity < 0) || (autoParity > 254))
        {
            DMSG(0, "NormApp::ProcessCommand(auto) invalid value!\n");   
            return false;
        }
        auto_parity = autoParity;
        if (session) session->ServerSetAutoParity(autoParity);
    }
    else if (!strncmp("txbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &tx_buffer_size))
        {
            DMSG(0, "NormApp::ProcessCommand(txbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("rxbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &rx_buffer_size))
        {
            DMSG(0, "NormApp::ProcessCommand(rxbuffer) invalid value!\n");   
            return false;
        }
    }
    return true;
}  // end NormApp::ProcessCommand()


NormApp::CmdType NormApp::CommandType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    unsigned int len = strlen(cmd);
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
}  // end NormApp::CommandType()


bool NormApp::ParseCommandLine(int argc, char* argv[])
{
    int i = 1;
    while ( i < argc)
    {
        CmdType cmdType = CommandType(argv[i]);   
        switch (cmdType)
        {
            case CMD_INVALID:
                DMSG(0, "NormApp::ParseCommandLine() Invalid command:%s\n", 
                        argv[i]);
                return false;
            case CMD_NOARG:
                if (!ProcessCommand(argv[i], NULL))
                {
                    DMSG(0, "NormApp::ParseCommandLine() ProcessCommand(%s) error\n", 
                            argv[i]);
                    return false;
                }
                i++;
                break;
            case CMD_ARG:
                if (!ProcessCommand(argv[i], argv[i+1]))
                {
                    DMSG(0, "NormApp::ParseCommandLine() ProcessCommand(%s, %s) error\n", 
                            argv[i], argv[i+1]);
                    return false;
                }
                i += 2;
                break;
        }
    }
    return true;
}  // end NormApp::ParseCommandLine()

void NormApp::Notify(NormController::Event event,
                     class NormSessionMgr* sessionMgr,
                     class NormSession*    session,
                     class NormServerNode* server,
                     class NormObject*     object)
{
    switch (event)
    {
        case TX_QUEUE_EMPTY:
           // Write to stream as needed
            //DMSG(0, "NormApp::Notify(TX_QUEUE_EMPTY) ...\n");
            if (object && (object == stream))
            {
                bool flush = false;
                if (input)
                {
                    if (input_index >= input_length)
                    {
                        size_t result = fread(input_buffer, sizeof(char), 512, input);
                        if (result)
                        {
                            input_index = 0;
                            input_length = result;
                        }
                        else
                        {
                            input_length = input_index = 0;
                            if (feof(input))
                            {
                                DMSG(0, "norm: input end-of-file.\n");
                                fclose(input);
                                input = NULL;
                                flush = true;   
                            }
                            else if (ferror(input))
                            {
                                DMSG(0, "norm: input error:%s\n", strerror(errno));
                                clearerr(input);
                                break;
                            }
                        } 
                    }
                    input_index += stream->Write(input_buffer+input_index, 
                                                 input_length-input_index,
                                                 flush);
                }  // end if (input)
            }
            else
            {
                // Can queue a new object for transmission  
                if (interval_timer.Interval() > 0.0)
                {
                    InstallTimer(&interval_timer);            
                }
                else
                {
                    OnIntervalTimeout();
                } 
            }
            break;
           
        case RX_OBJECT_NEW:
        {
            //TRACE("NormApp::Notify(RX_OBJECT_NEW) ...\n");
            // It's up to the app to "accept" the object
            switch (object->GetType())
            {
                case NormObject::STREAM:
                {
                    const NormObjectSize& size = object->Size();
                    if (!((NormStreamObject*)object)->Accept(size.LSB()))
                    {
                        DMSG(0, "NormApp::Notify(RX_OBJECT_NEW) stream object accept error!\n");
                    }
                }
                break;                        
                case NormObject::FILE:
                {
                    if (rx_cache_path)
                    {
                        // (TBD) re-arrange so if we've already recv'd INFO,
                        // we can use that for file name right away ???
                        // (TBD) Manage recv file name collisions, etc ...
                        char fileName[PATH_MAX];
                        strcpy(fileName, rx_cache_path);
                        strcat(fileName, "normTempXXXXXX");
#ifdef WIN32
                        if (!_mktemp(fileName))
#else
                        int fd = mkstemp(fileName); 
                        if (fd >= 0)
                        {
                            close(fd);
                        }
                        else   
#endif // if/else WIN32         
                        {
                            DMSG(0, "NormApp::Notify(RX_OBJECT_NEW) Warning: mkstemp() error: %s\n",
                                    strerror(errno));  
                        } 
                        if (!((NormFileObject*)object)->Accept(fileName))
                        {
                            DMSG(0, "NormApp::Notify(RX_OBJECT_NEW) file object accept error!\n");
                        }
                    }
                    else
                    {
                        DMSG(0, "NormApp::Notify(RX_OBJECT_NEW) no rx cache for file\n");   
                    }
                }
                break;
                case NormObject::DATA: 
                    DMSG(0, "NormApp::Notify() FILE/DATA objects not _yet_ supported...\n");      
                    break;
            }   
            break;
        }
            
        case RX_OBJECT_INFO:
            //TRACE("NormApp::Notify(RX_OBJECT_INFO) ...\n");
            switch(object->GetType())
            {
                case NormObject::FILE:
                {
                    // Rename rx file using newly received info
                    char fileName[PATH_MAX];
                    strncpy(fileName, rx_cache_path, PATH_MAX);
                    UINT16 pathLen = strlen(rx_cache_path);
                    pathLen = MIN(pathLen, PATH_MAX);
                    UINT16 len = object->InfoLength();
                    len = MIN(len, (PATH_MAX - pathLen));
                    strncat(fileName, object->GetInfo(), len);
                    // Convert '/' in file info to directory delimiters
                    for (UINT16 i = pathLen; i < (pathLen+len); i++)
                    {
                        if ('/' == fileName[i]) 
                            fileName[i] = DIR_DELIMITER;
                    }
                    pathLen += len;
                    if (pathLen < PATH_MAX) fileName[pathLen] = '\0';
                    
                    // Deal with concurrent rx name collisions
                    // (TBD) and implement overwrite policy
                    //       and cache files in cache mode
                    
                    if (!((NormFileObject*)object)->Rename(fileName))
                    {
                        DMSG(0, "NormApp::Notify() Error renaming rx file: %s\n",
                                fileName);
                    }
                    break;
                }
                case NormObject::DATA:
                case NormObject::STREAM:
                    break;
            }  // end switch(object->GetType())
            break;
            
        case RX_OBJECT_UPDATE:
            //TRACE("NormApp::Notify(RX_OBJECT_UPDATE) ...\n");
            switch (object->GetType())
            {
                case NormObject::FILE:
                    // (TBD) update progress
                    break;
                
                case NormObject::STREAM:
                {
                    // Read the stream when it's updated  
                    ASSERT(output);
                    char buffer[256];
                    unsigned int nBytes;
                    while ((nBytes = ((NormStreamObject*)object)->Read(buffer, 256)))
                    {
                        unsigned int put = 0;
                        while (put < nBytes)
                        {
                            size_t result = fwrite(buffer, sizeof(char), nBytes, output);
                            fflush(output);
                            if (result)
                            {
                                put += result;   
                            }
                            else
                            {
                                if (ferror(output))
                                {
                                    if (EINTR == errno) 
                                    {
                                        clearerr(output);
                                        continue;
                                    }
                                    else
                                    {
                                        DMSG(0, "norm: output error:%s\n", strerror(errno));
                                        clearerr(output);
                                        break;
                                    }   
                                }
                            }   
                        }  // end while(put < nBytes)
                    }
                    break;
                }
                                        
                case NormObject::DATA: 
                    DMSG(0, "NormApp::Notify() FILE/DATA objects not _yet_ supported...\n");      
                    break;
            }  // end switch (object->GetType())
            break;
        case RX_OBJECT_COMPLETE:
        {
            switch(object->GetType())
            {
                case NormObject::FILE:
                    DMSG(0, "norm: Completed rx file: %s\n", ((NormFileObject*)object)->Path());
                    break;
                    
                case NormObject::STREAM:
                    ASSERT(0);
                    break;
                case NormObject::DATA:
                    ASSERT(0);
                    break;
            }
            break;
        }
    }  // end switch(event)
}  // end NormApp::Notify()


bool NormApp::OnIntervalTimeout()
{
    char fileName[PATH_MAX];
    if (tx_file_list.GetNextFile(fileName))
    {
        char pathName[PATH_MAX];
        tx_file_list.GetCurrentBasePath(pathName);
        unsigned int len = strlen(pathName);
        len = MIN(len, PATH_MAX);
        unsigned int maxLen = PATH_MAX - len;
        char* ptr = fileName + len;
        len = strlen(ptr);
        len = MIN(len, maxLen);
        // (TBD) Make sure len <= segment_size)
        char fileNameInfo[PATH_MAX];
        strncpy(fileNameInfo, ptr, len);
        // Normalize directory delimiters in file name info
        for (unsigned int i = 0; i < len; i++)
        {
            if (DIR_DELIMITER == fileNameInfo[i]) 
                fileNameInfo[i] = '/';
        }
        char temp[PATH_MAX];
        strncpy(temp, fileNameInfo, len);
        temp[len] = '\0';
        if (!session->QueueTxFile(fileName, fileNameInfo, len))
        {
            DMSG(0, "NormApp::OnIntervalTimeout() Error opening tx file: %s\n",
                    fileName);
            // Try the next file in the list
            return OnIntervalTimeout();
        }
        DMSG(0, "norm: File \"%s\" queued for transmission.\n", fileName);
        interval_timer.SetInterval(tx_object_interval);
    }
    else if (tx_repeat_count)
    {
        // (TBD) When repeating, remove previous instance from tx queue???
        if (tx_repeat_count > 0) tx_repeat_count--;
        tx_file_list.ResetIterator();
        if (tx_repeat_interval > tx_object_interval)
        {
            if (interval_timer.IsActive()) interval_timer.Deactivate();
            interval_timer.SetInterval(tx_repeat_interval = tx_object_interval);
            InstallTimer(&interval_timer);
            return false;       
        }
        else
        {
            return OnIntervalTimeout();
        }
    }
    else
    {
        DMSG(0, "norm: End of tx file list reached.\n");  
    }   
    return true;
}  // end NormApp::OnIntervalTimeout()

bool NormApp::OnStartup()
{
#ifdef WIN32
	if (!dispatcher.Win32Init())
	{
		fprintf(stderr, "norm:: Win32Init() error!\n");
		return false;
	}
#endif // WIN32

    signal(SIGTERM, SignalHandler);
    signal(SIGINT, SignalHandler);
    
    // Validate our application settings
    if (!address)
    {
        DMSG(0, "NormApp::OnStartup() Error! no session address given.");
        return false;
    }
    if (!input && !output && tx_file_list.IsEmpty() && !rx_cache_path)
    {
        DMSG(0, "NormApp::OnStartup() Error! no \"input\", \"output\", "
                "\"sendfile\", or \"rxcache\" given.");
        return false;
    }   
    
    // Create a new session on multicast group/port
    session = session_mgr.NewSession(address, port);
    if (session)
    {
        // Common session parameters
        session->SetTxRate(tx_rate);
        session->SetTrace(tracing);
        session->SetTxLoss(tx_loss);
        session->SetRxLoss(rx_loss);
        
        if (input || !tx_file_list.IsEmpty())
        {
            // StartServer(bufferMax, segmentSize, fec_ndata, fec_nparity)
            if (!session->StartServer(tx_buffer_size, segment_size, ndata, nparity))
            {
                DMSG(0, "NormApp::OnStartup() start server error!\n");
                session_mgr.Destroy();
                return false;
            }
            session->ServerSetAutoParity(auto_parity);
                        
            if (input)
            {
                // Open a stream object to write to (QueueTxStream(stream bufferSize))
                stream = session->QueueTxStream(tx_buffer_size);
                if (!stream)
                {
                    DMSG(0, "NormApp::OnStartup() queue tx stream error!\n");
                    session_mgr.Destroy();
                    return false;
                }
            }
        }
        
        if (output || rx_cache_path)
        {
            TRACE("starting client ...\n");
            // StartClient(bufferMax (per-sender))
            if (!session->StartClient(rx_buffer_size))
            {
                DMSG(0, "NormApp::OnStartup() start client error!\n");
                session_mgr.Destroy();
                return false;
            }
        }
        return true;
    }
    else
    {
        DMSG(0, "NormApp::OnStartup() new session error!\n");
        return false;
    }
}  // end NormApp::OnStartup()

void NormApp::OnShutdown()
{
    session_mgr.Destroy();
    if (input) fclose(input);
    input = NULL;
    if (output) fclose(output);
    output = NULL;
}  // end NormApp::OnShutdown()


// Out application instance (global for SignalHandler)
NormApp theApp; 

// Use "main()" for UNIX and WIN32 console apps, 
// "WinMain()" for non-console WIN32
// (VC++ uses the "_CONSOLE_" macro to indicate build type)

#if defined(WIN32) && !defined(_CONSOLE_)
int PASCAL WinMain(HINSTANCE instance, HINSTANCE prevInst, LPSTR cmdline, int cmdshow)
{
    // (TBD) transform WinMain "cmdLine" to (argc, argv[]) values
    int argc = 0;
    char* argv[] = NULL;
#else
int main(int argc, char* argv[])
{
#endif
#ifdef WIN32
	// Hack to determine if Win32 console application was launched
    // independently or from a pre-existing console window
	bool pauseForUser = false;
	HANDLE hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	if (INVALID_HANDLE_VALUE != hConsoleOutput)
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(hConsoleOutput, &csbi);
		pauseForUser = ((csbi.dwCursorPosition.X==0) && (csbi.dwCursorPosition.Y==0));
		if ((csbi.dwSize.X<=0) || (csbi.dwSize.Y <= 0)) pauseForUser = false;
	}
	else
	{
		// We're not a "console" application, so create one
		// This could be commented out or made a command-line option
		OpenDebugWindow();
		pauseForUser = true;
	}
#endif // WIN32
    
	int exitCode = 0;
    if (theApp.ParseCommandLine(argc, argv))
    {
        if (theApp.OnStartup())
        {
		    exitCode = theApp.MainLoop();
            theApp.OnShutdown();
            fprintf(stderr, "norm: Done.\n");
        }
        else
        {
             fprintf(stderr, "norm: Error initializing application!\n");
             exitCode = -1;  
        }   
    }
    else
    {
        fprintf(stderr, "norm: Error parsing command line!\n");
        exitCode = -1;
    }
    
#ifdef WIN32
		// If Win32 console is going to disappear, pause for user before exiting
		if (pauseForUser)
		{
			printf ("Program Finished - Hit <Enter> to exit");
			getchar();
		}
#endif // WIN32   
	return exitCode;  // exitCode contains "signum" causing exit
}  // end main();

void NormApp::SignalHandler(int sigNum)
{
    switch(sigNum)
    {
        case SIGTERM:
        case SIGINT:
            theApp.Stop(sigNum);  // causes theApp's main loop to exit
            break;
            
        default:
            fprintf(stderr, "norm: Unexpected signal: %d\n", sigNum);
            break; 
    }  
}  // end NormApp::SignalHandler()
