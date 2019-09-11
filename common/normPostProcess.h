#ifndef _NORM_POST_PROCESS
#define _NORM_POST_PROCESS


class NormPostProcessor
{
    public:
        NormPostProcessor();
        ~NormPostProcessor();
        
        bool IsEnabled() {return (0 != process_argv);}
        bool SetCommand(const char* cmd);
        bool ProcessFile(const char* path);
        void Kill();
        bool IsActive()
        {
#ifdef UNIX
            return (0 != process_id);
#endif  // UNIX            
        }
        
        void HandleSIGCHLD();
        
    private:
        const char**    process_argv;
        unsigned int    process_argc;
#ifdef UNIX
        int             process_id;
#endif
#ifdef WIN32
        HANDLE          process_handle;  
#endif                
};  // end class NormPostProcessor

#endif // _NORM_POST_PROCESS
