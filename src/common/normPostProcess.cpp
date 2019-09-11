#include "normPostProcess.h"
#include "protoDebug.h"

#include <ctype.h>
#include <string.h>

NormPostProcessor::NormPostProcessor()
 : process_argv(NULL), process_argc(0)
{

}

NormPostProcessor::~NormPostProcessor()
{
    SetCommand(NULL);
}

void NormPostProcessor::GetCommand(char* buffer, unsigned int buflen)
{
    if (process_argv && buffer)
    {
        
        unsigned int i = 0;
        while (buflen && (i < process_argc))
        {
            strncpy(buffer, process_argv[i], buflen);
            size_t len = strlen(process_argv[i]);
            if (len < buflen)
            {
                buflen -= (unsigned int)len;
                buffer += len;
            }
            else
            {
                buflen = 0;
                break;
            }
            i++;
            if (i < process_argc) 
            {
                *buffer++= ' '; 
                buflen--;
            }
        } 
    }
    else if (buflen && buffer)
    {
        if (buflen < 4)
            buffer[0] = '\0';
        else
            strncpy(buffer, "none", 4); 
        if (buflen > 4) buffer[4] = '\0';
    }
}  // end NormPostProcessor::GetCommand()

bool NormPostProcessor::SetCommand(const char* cmd)
{
    // 1) Delete old command resources
    if (process_argv)
    {
        char** arg = process_argv;
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
    if (NULL == cmd) return true;          // post processing disabled
    if (!strcmp(cmd, "none")) return SetCommand(NULL);
    const char* ptr = cmd;
    unsigned int argCount = 0;
    while (isspace(*ptr) && ('\0' != *ptr)) ptr++;
    while ('\0' != *ptr)
    {
        argCount++;
        while (!isspace(*ptr) && ('\0' != *ptr)) ptr++;
        while (isspace(*ptr) && ('\0' != *ptr)) ptr++;
    }
    if (!argCount) return true;   // post processing disabled
    
    // 3) Allocate new process_argv array (2 extra slots, one for "target",
    //    and one for terminating NULL pointer.
    if (!(process_argv = new char*[argCount+2]))
    {
        PLOG(PL_FATAL, "NormPostProcessor::SetCommand new(process_argv) error: %s\n",
                GetErrorString()); 
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
        unsigned int argLength = (unsigned int)(ptr - argStart);
        char* arg;
        if (!(arg = new char[argLength+1]))
        {
            SetCommand(NULL);
            PLOG(PL_FATAL, "NormPostProcessor::SetCommand new(process_arg) error: %s\n",
                    GetErrorString()); 
            return false;   
        }
        strncpy(arg, argStart, argLength);
        arg[argLength] = '\0';
        process_argv[index++] = arg;
        while (isspace(*ptr) && ('\0' != *ptr)) ptr++;
    }
    return true;
}  // end NormPostProcessor::SetCommand()
