
// norm.cpp - Command-line NORM application

#include "protoLib.h"
#include "normSession.h"

#include <stdio.h>   // for stdout/stderr printouts
#include <signal.h>  // for SIGTERM/SIGINT handling

class NormApp : public NormController
{
    public:
        NormApp();
        virtual ~NormApp();
		bool OnStartup();
        int MainLoop() {return dispatcher.Run();}
        void Stop(int exitCode) {dispatcher.Stop(exitCode);}
        void OnShutdown();
		
    private:
            
        virtual void Notify(NormController::Event event,
                        class NormSessionMgr* sessionMgr,
                        class NormSession*    session,
                        class NormServerNode* server,
                        class NormObject*     object);

        static void SignalHandler(int sigNum);
    
        EventDispatcher     dispatcher;        
        NormSessionMgr      session_mgr;
        NormSession*        session;
        NormStreamObject*   stream;
    
}; // end class NormApp


void NormApp::Notify(NormController::Event event,
                     class NormSessionMgr* sessionMgr,
                     class NormSession*    session,
                     class NormServerNode* server,
                     class NormObject*     object)
{
    switch (event)
    {
        case TX_QUEUE_EMPTY:
           //TRACE("NormApp::Notify(TX_QUEUE_EMPTY) ...\n");
           if (object == stream)
           {
                char text[256];
                memset(text, 'a', 256);
                stream->Write(text, 256);
           }
           break;
           
        case RX_OBJECT_NEW:
            //TRACE("NormApp::Notify(RX_OBJECT_NEW) ...\n");
            {
                switch (object->GetType())
                {
                    case NormObject::STREAM:
                    {
                        const NormObjectSize& size = object->Size();
                        if (!((NormStreamObject*)object)->Accept(size.LSB()))
                        {
                            TRACE("stream object accept error!\n");
                        }
                    }
                    break;                        
                    case NormObject::FILE:
                    case NormObject::DATA: 
                        TRACE("NormApp::Notify() FILE/DATA objects not supported...\n");      
                        break;
                }   
            }
            break;
            
        case RX_OBJECT_UPDATE:
            //TRACE("NormApp::Notify(RX_OBJECT_UPDATE) ...\n");
            switch (object->GetType())
            {
                case NormObject::STREAM:
                {
                    // Read the stream   
                    char buffer[2048];
                    unsigned int nBytes;
                    while ((nBytes = ((NormStreamObject*)object)->Read(buffer, 2048)))
                    {
                        for (unsigned int i =0; i < nBytes; i++)
                        {
                            if ('a' != buffer[i])
                            {
                                TRACE("NormApp::Notify() bad data received!\n");
                                break;
                            }
                        }
                        buffer[32] = '\0';
                        DMSG(0, "NormApp::Notify() stream read %u bytes: \"%s\"\n", nBytes, buffer);
                    }
                
                }
                break;
                                        
                case NormObject::FILE:
                case NormObject::DATA: 
                    TRACE("NormApp::Notify() FILE/DATA objects not supported...\n");      
                    break;
            }
            break;
    }
}

NormApp::NormApp()
 : session(NULL), stream(NULL)
{
    // Init tx_timer for 1.0 second interval, infinite repeats
    session_mgr.Init(EventDispatcher::TimerInstaller, &dispatcher,
                     EventDispatcher::SocketInstaller, &dispatcher,
                     this);
}

NormApp::~NormApp()
{
    
}

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
    
    
    session = session_mgr.NewSession("224.225.1.5", 5005);
    if (session)
    {
        if (!session->StartServer(1024*1024, 256, 20, 8))
        {
            DMSG(0, "NormApp::OnStartup() start server error!\n");
            session_mgr.Destroy();
            return false;
        }
        session->ServerSetAutoParity(4);
        //session->SetLoopback(true);
        
        // Open a stream object to write to
        stream = session->QueueTxStream(10*1024);
        if (!stream)
        {
            DMSG(0, "NormApp::OnStartup() queue tx stream error!\n");
            session_mgr.Destroy();
            return false;
        }
        char text[256];
        memset(text, 'a', 256);
        stream->Write(text, 256);
        return true;
    }
    else
    {
        return false;
    }
}  // end NormApp::OnStartup()

void NormApp::OnShutdown()
{
    session_mgr.Destroy();
}  // end NormApp::OnShutdown()


// Out application instance (global for SignalHandler)
NormApp theApp; 

// Use "main()" for UNIX and WIN32 console apps, 
// "WinMain()" for non-console WIN32
// (VC++ uses the "_CONSOLE_ macro to indicate build type)

#if defined(WIN32) && !defined(_CONSOLE)
int PASCAL WinMain(HINSTANCE instance, HINSTANCE prevInst, LPSTR cmdline, int cmdshow)
#else
int main(int argc, char* argv[])
#endif
{
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
	if (theApp.OnStartup())
    {
		exitCode = theApp.MainLoop();
        theApp.OnShutdown();
        fprintf(stderr, "norm: Done.\n");
#ifdef WIN32
		// If Win32 console is going to disappear, pause for user before exiting
		if (pauseForUser)
		{
			printf ("Program Finished - Hit <Enter> to exit");
			getchar();
		}
#endif // WIN32
    }
    else
    {
         fprintf(stderr, "norm: Error initializing application!\n");
         return -1;  
    }      
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
