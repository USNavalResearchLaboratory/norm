

#include "normPostProcess.h"
#include "protoDebug.h"

#ifndef _WIN32_WCE
#include <ddeml.h>
#endif // !_WIN32_WCE
#include <shellapi.h>
#include <stdio.h>

class Win32PostProcessor : public NormPostProcessor
{
    public:
        ~Win32PostProcessor();

        bool ProcessFile(const char* path);
        void Kill();
        bool IsActive() {return (NULL != post_processor_handle);}

    private:
        friend class NormPostProcessor;
        Win32PostProcessor();
        bool Init();

#ifndef _WIN32_WCE
        static HDDEDATA CALLBACK DDEClientCallback(UINT uiType, UINT uiFmt, HCONV hConv, 
			                                       HSZ sz1, HSZ sz2, HDDEDATA hData,
			                                       DWORD lData1, DWORD lData2);
        // Post Processor DDE state
	    DWORD 		        lIdInst;   // DDE instance id
	    HCONV		        hConv;     // dde converstation handle
	    DWORD		        transaction_id;
	    DWORD		        window_id; // dde browser window id
	    PFNCALLBACK	        lpDDECallback;
#endif // !_WIN32_WCE
		HANDLE		        post_processor_handle;
};  // end class Win32PostProcessor


#ifndef _WIN32_WCE
HDDEDATA CALLBACK Win32PostProcessor::DDEClientCallback(UINT uiType, UINT uiFmt, HCONV hConv, 
			                                            HSZ sz1, HSZ sz2, HDDEDATA hData,
			                                            DWORD lData1, DWORD lData2)
{
    CONVINFO convInfo;
    convInfo.cb = sizeof(convInfo);
    bool gotInfo;
    if (FALSE != DdeQueryConvInfo(hConv, lData1, &convInfo))
    {
        gotInfo = true;
    }
    else
    {
        DMSG(8, "Win32PostProcessor::DDEClientCallback() DdeQueryInfo error\n");
        gotInfo = false;
    }
                
	switch(uiType)
	{
		case XTYP_XACT_COMPLETE:
        {
            if (gotInfo)
            {
                Win32PostProcessor* processor = (Win32PostProcessor*)convInfo.hUser;
                if (processor &&
                    (processor->hConv == hConv) &&
                    (processor->transaction_id == lData1))
                {
				    DWORD result;
				    DdeGetData(hData, (unsigned char*)&result, sizeof(DWORD), 0);
				    // Hack to make IExplore fake working
				    if (-2 == result)  // IExplore success
					    processor->window_id = -1;
				    else if (-3 == result)  // IExplore failure
					    processor->window_id = 0x0;
				    else
					    processor->window_id = result;
			    }
			    else
			    {
				    DMSG(0, "Win32PostProcessor::DDEClientCallback() Unknown DDE transaction ID! (proc:%p)\n",
                             processor);
			    }
            }
            break;
        }

        case XTYP_REGISTER:
            //TRACE("Win32PostProcessor::DDEClientCallback() XTYP_REGISTER \n");
            break;

        case XTYP_UNREGISTER:
            //TRACE("Win32PostProcessor::DDEClientCallback() XTYP_UNREGISTER (gotInfo:%d)\n", gotInfo);
            break;

		default:
			DMSG(4, "Win32PostProcessor::DDEClientCallback() Unknown DDE message type:%u\n", uiType);
            break;
	}
	return NULL;  // callback does nothing for now
}
#endif // !_WIN32_WCE

Win32PostProcessor::Win32PostProcessor()

 : 
#ifndef _WIN32_WCE
    lIdInst(0), hConv(NULL), window_id(-1),
#endif // !_WIN32_WCE
   post_processor_handle(NULL)
{
    
}

Win32PostProcessor::~Win32PostProcessor()
{
    Kill();
#ifndef _WIN32_WCE
    if (lIdInst)
    {
	    ::DdeUninitialize(lIdInst);
        lIdInst = 0;
    }
#endif // !_WIN32_WCE
}

NormPostProcessor* NormPostProcessor::Create()
{
    Win32PostProcessor* p = new Win32PostProcessor();
    if (p)
    {
        if (p->Init())
            return static_cast<NormPostProcessor*>(p);
        else
            delete p;
    }
    return NULL;
}  // end NormPostProcessor::Create()

bool Win32PostProcessor::Init()
{
#ifndef _WIN32_WCE
    // Init DDE
    if (0 != lIdInst)
    {
        // already exists
        return false;
    }
    else
    {
        if (::DdeInitialize(&lIdInst, 
                            (PFNCALLBACK)DDEClientCallback,
		                    APPCLASS_STANDARD | APPCMD_CLIENTONLY, 0ul))
        {
	        return false;
        }
    }
#endif // !_WIN32_WCE
    return true;
}  // end Win32PostProcessor::Init()


void Win32PostProcessor::Kill()
{
#ifndef _WIN32_WCE
    if (hConv) 
    {
        DdeDisconnect(hConv);
        hConv = NULL;
    }
#endif // !_WIN32_WCE
    if (post_processor_handle)
    {
        TerminateProcess(post_processor_handle, 0);
        CloseHandle(post_processor_handle);
        post_processor_handle = NULL;
    }
}  // end Win32PostProcessor::Kill()


bool Win32PostProcessor::ProcessFile(const char *path)
{
#ifndef _WIN32_WCE
    // DDE seems to be broken for the moment
    // (It does work if the serving applications is _already_ running
    //  so these two lines of code disable it for the moment)
    bool useDDE = false;
    if (useDDE)
    {
        if (NULL == hConv)
	    {
            HSZ szServerName = ::DdeCreateStringHandle(lIdInst, process_argv[0], CP_WINANSI);
		    HSZ szTopicName = ::DdeCreateStringHandle(lIdInst, "WWW_OpenURL", CP_WINANSI);
            DMSG(4, "Win32PostProcessor::ProcessFile() calling DdeConnect(WWW_OpenURL) ...\n");
		    if ((hConv = ::DdeConnect(lIdInst, szServerName, szTopicName, 0)))
                DMSG(4, "Win32PostProcessor::ProcessFile() DdeConnect() completed successfully.\n");
            else
                DMSG(4, "Win32PostProcessor::ProcessFile() DdeConnect() failed\n");
		    DdeFreeStringHandle(lIdInst, szTopicName);
            DdeFreeStringHandle(lIdInst, szServerName);
	    }

	    if (hConv)
	    {
            char ddeText[PATH_MAX+128];
		    sprintf(ddeText, "\"file://%s\",,%d,0,,,NETREPORT", path, window_id);
            HSZ  szParams = ::DdeCreateStringHandle(lIdInst, ddeText, CP_WINANSI);
            TRACE("Win32PostProcessor::ProcessFile() calling DdeClientTransaction() ...\n");
		    if(DdeClientTransaction(NULL, 0, hConv, szParams, CF_TEXT,
			                        XTYP_REQUEST, TIMEOUT_ASYNC, 
                                    &transaction_id))
		    {
                DdeSetUserHandle(hConv, transaction_id, (DWORD_PTR)this);
                DdeFreeStringHandle(lIdInst, szParams);
			    return true;
		    }
		    else
		    {	
                DMSG(0, "Win32PostProcessor::ProcessFile() DdeClientTransaction() failure ...\n");
			    DdeFreeStringHandle(lIdInst, szParams);
			    DdeDisconnect(hConv);
			    hConv = NULL;
		    }
	    }
    }  // end if (NULL != post_processor_handle)

	// DDE post processor evidently not running
	// Launch post processor with shell command
	// For now we assume the DDE server name & command name are the 
	// same and that the post processor command executable is in
	// the PATH
#endif // !_WIN32_WCE

	// Kill old PostProcessor first
    Kill();

    // Construct the command line
    char args[PATH_MAX+512];
    args[0] = '\0';
    for (unsigned int i = 1; i < process_argc; i++)
    {
        strcat("%s ", process_argv[i]);
    }
    strcat(args, path);	

#ifdef _UNICODE
    wchar_t cmdBuffer[PATH_MAX];
    mbstowcs(cmdBuffer, process_argv[0], PATH_MAX);
    wchar_t* cmdPtr = cmdBuffer;
    wchar_t argsBuffer[PATH_MAX+512];
    mbstowcs(argsBuffer, args, PATH_MAX+512);
    wchar_t* argPtr = argsBuffer;
    
#else
    char* cmdPtr = process_argv[0];
    char* argPtr = args;
#endif // if/else _UNICODE

    DMSG(4, "Win32PostProcessor::ProcessFile() execing \"%s %s\"\n", process_argv[0], args);
	
	SHELLEXECUTEINFO exeInfo;
	exeInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	exeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    exeInfo.hwnd = NULL; 
    exeInfo.lpVerb = NULL; 
    exeInfo.lpFile = cmdPtr; 
    exeInfo.lpParameters = argPtr; 
    exeInfo.lpDirectory = NULL;
    exeInfo.nShow = SW_SHOW;
	if (!ShellExecuteEx(&exeInfo))
	{
        post_processor_handle = NULL;
        DMSG(0, "Error launching post processor!\n\"%s %s %s\"", process_argv[0], args);
        return false;
	}
	else
	{
		post_processor_handle = exeInfo.hProcess; 
        return true;
	}
}  // end Win32PostProcessor::ProcessFile()
