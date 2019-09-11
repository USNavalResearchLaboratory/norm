
#ifndef _NORM_POST_PROCESS
#define _NORM_POST_PROCESS

#include "protoDefs.h"  // for NULL
        
class NormPostProcessor
{
    public:
        NormPostProcessor();
        virtual ~NormPostProcessor();
        // Implement this per derivation
        static NormPostProcessor* Create();
        
        bool IsEnabled() {return (NULL != process_argv);}
        bool SetCommand(const char* cmd);
        void GetCommand(char* buffer, unsigned int buflen);
        
        virtual bool ProcessFile(const char* path) = 0;
        virtual void Kill() = 0;
        virtual bool IsActive() = 0;
        virtual void OnDeath() {}; 
        
    protected:
        char**          process_argv;
        unsigned int    process_argc;
};  // end class NormPostProcessor

#endif // _NORM_POST_PROCESS
