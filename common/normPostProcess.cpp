#include "normPostProcess.h"
#include "protoLib.h"

#include <errno.h>
#include <signal.h>
#include <ctype.h>
#ifdef UNIX
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif // UNIX

typedef void (*sighandler_t)(int);

NormPostProcessor::NormPostProcessor()
 : process_argv(NULL)
#ifdef UNIX
   ,process_id(0)
#endif // UNIX
{

}

NormPostProcessor::~NormPostProcessor()
{
    if (IsActive()) Kill();
    SetCommand(NULL);
}

bool NormPostProcessor::SetCommand(const char* cmd)
{
    
    // 1) Delete old command resources
    if (process_argv)
    {
        const char** arg = process_argv;
        while (*arg)
        {
            delete *arg;
            arg++;   
        }   
        delete process_argv;
        process_argv = NULL;
        process_argc = 0;
    }
   
    // 2) Count the number of tokens
    if (!cmd) return true;          // post processing disabled
    if (!strcmp(cmd, "none")) return SetCommand(NULL);
    const char* ptr = cmd;
    unsigned int argCount = 0;
    while (isspace(*ptr) && ('\0' != *ptr)) ptr++;
    while ('\0' != *ptr)
    {
        argCount++;
        while (!isspace(*ptr) && ('\0' != *ptr)) ptr++;
    }
    if (!argCount) return true;   // post processing disabled
    
    
    // 3) Allocate new process_argv array (2 extra slots, one for "target",
    //    and one for terminating NULL pointer.
    if (!(process_argv = new const char*[argCount+2]))
    {
        DMSG(0, "NormPostProcessor::SetCommand new(process_argv) error: %s\n",
                strerror(errno)); 
        return false;  
    }
    memset((void*)process_argv, 0, (argCount+2)*sizeof(char**));
    process_argc = argCount;
            
    // 4) Fill in process_argv[] array with individual args
    ptr = cmd;
    while (isspace(*ptr) && ('\0' != *ptr)) ptr++;
    unsigned int index = 0;
    while ('\0' != *ptr)
    {
        const char* argStart = ptr;
        while (!isspace(*ptr) && ('\0' != *ptr)) ptr++;
        unsigned int argLength = ptr - argStart;
        char* arg;
        if (!(arg = new char[argLength+1]))
        {
            SetCommand(NULL);
            DMSG(0, "NormPostProcessor::SetCommand new(process_arg) error: %s\n",
                    strerror(errno)); 
            return false;   
        }
        strncpy(arg, argStart, argLength);
        arg[argLength] = '\0';
        process_argv[index] = arg;
    }
    return true;
}  // end NormPostProcessor::SetCommand()

bool NormPostProcessor::ProcessFile(const char* path)
{
    
    if (IsActive()) Kill();
    
#ifdef UNIX
    // 1) temporarily disable signal handling
    sighandler_t sigtermHandler = signal(SIGTERM, SIG_DFL);
    sighandler_t sigintHandler = signal(SIGINT, SIG_DFL);
    sighandler_t sigchldHandler = signal(SIGCHLD, SIG_DFL);  
    
    process_argv[process_argc] = path;
      
    switch((process_id = fork()))
    {
        case -1:    // error
            DMSG(0, "NormPostProcessor::ProcessFile fork() error: %s\n", strerror(errno));
            process_id = 0;
            process_argv[process_argc] = NULL;
            return false;

        case 0:     // child
            if (execvp((char*)process_argv[0], (char**)process_argv) < 0)
            {
		        DMSG(0, "NormPostProcessor::ProcessFile execvp() error: %s\n", strerror(errno));
                exit(-1);
            }
            break;

        default:    // parent
            process_argv[process_argc] = NULL;
            // Restore signal handlers for parent
            signal(SIGTERM, sigtermHandler);
            signal(SIGINT, sigintHandler);
            signal(SIGCHLD, sigchldHandler);
            break;
    }
#endif // UNIX    
    return true;
}  // end NormPostProcessor::ProcessFile()
    
void NormPostProcessor::Kill()
{
    if (!IsActive()) return;
#ifdef UNIX
    int count = 0;
    while((kill(process_id, SIGTERM) != 0) && count < 10)
	{ 
	    if (errno == ESRCH) break;
	    count++;
	    DMSG(0, "NormPostProcessor::Kill kill() error: %s\n", strerror(errno));
	}
	count = 0;
    int status;
	while((waitpid(process_id, &status, 0) != process_id) && count < 10)
	{
	    if (errno == ECHILD) break;
	    count++;
	    DMSG(0, "NormPostProcessor::Kill waitpid() error: %s\n", strerror(errno));
	}
	process_id = 0;  
#endif  // UNIX
}  // end NormPostProcessor::Kill()

void NormPostProcessor::HandleSIGCHLD()
{
    // See if processor exited itself
    int status;
    if (wait(&status) == process_id) process_id = 0;
}  // end NormPostProcessor::HandleSIGCHLD()
