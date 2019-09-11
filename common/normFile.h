#ifndef _NORM_FILE
#define _NORM_FILE

// This module defines some simple classes for manipulating files.
// Unix and Win32 platforms are supported.  Routines for iterating
// over directories are also provided.  And a file/directory list
// class is provided to manage a list of files.

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif // if/else WIN32

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

// From PROTOLIB
#include "sysdefs.h"  // for bool definition, DIR_DELIMITER, PATH_MAX, etc
#include "debug.h"    // for DEBUG stuff
        
class NormFile
{
    // Methods 
    public:
        enum Type {INVALID, NORMAL, DIRECTORY};        
        NormFile();
        ~NormFile();
        bool Open(const char* path, int theFlags);
        bool Lock();
        void Unlock();
        bool Rename(const char* oldName, const char* newName);
        bool Unlink(const char *path);
        void Close();
        bool IsOpen() const {return (fd >= 0);}
        int Read(char* buffer, int len);
        int Write(const char* buffer, int len);
        bool Seek(off_t theOffset);
        off_t GetOffset() const {return (offset);}
        off_t GetSize() const;
        
        // static helper methods
        static NormFile::Type GetType(const char *path);
        static off_t GetSize(const char* path);
        static time_t GetUpdateTime(const char* path);
        static bool IsLocked(const char *path);
         
        static bool Exists(const char* path)
        {
#ifdef WIN32
            return (0xFFFFFFFF != GetFileAttributes(path));
#else
            return (0 == access(path, F_OK));
#endif  // if/else WIN32
        }
        
        static bool IsWritable(const char* path)
        {
#ifdef WIN32
            DWORD attr = GetFileAttributes(path);
	        return ((0xFFFFFFFF == attr) ? 
                    false : (0 == (attr & FILE_ATTRIBUTE_READONLY)));
#else
            return (0 == access(path, W_OK));
        }
#endif // if/else WIN32
       
    
    // Members
    private:
        int     fd;
        int     flags;
        off_t   offset;
};
        
/******************************************
* The NormDirectory and NormDirectoryIterator classes
* are used to walk directory trees for file transmission
*/      

class NormDirectoryIterator
{
    public:
        NormDirectoryIterator();
        ~NormDirectoryIterator();
        bool Open(const char*thePath);
        void Close();
        bool GetPath(char* pathBuffer);
        // "buffer" should be PATH_MAX long!
        bool GetNextFile(char* buffer);
        
    private:            
        class NormDirectory
        {
            friend class NormDirectoryIterator;
            private:           
                char            path[PATH_MAX];
                NormDirectory*  parent;
        #ifdef WIN32
                HANDLE          hSearch;
        #else
                DIR*            dptr;
        #endif  // if/else WIN32    
                NormDirectory(const char *thePath, NormDirectory* theParent = NULL);
                ~NormDirectory();
                void GetFullName(char* namePtr);
                bool Open();
                void Close();

                const char* Path() const {return path;}
                void RecursiveCatName(char* ptr);
        };  // end class NormDirectoryIterator::NormDirectory    
            
        NormDirectory*  current;
        int             path_len;
};  // end class NormDirectoryIterator


class NormFileList
{
    public:
        NormFileList();
        ~NormFileList();
        void Destroy();
        bool IsEmpty() {return (NULL == head);}
        void ResetIterator() 
        {
            last_time = this_time;
            this_time = big_time;
            next = NULL;
            reset = true;
        }
        void InitUpdateTime(bool updatesOnly, time_t initTime = 0)
        {
            updates_only = updatesOnly;
            last_time = this_time = big_time = initTime;
        }
        
        bool Append(const char* path);
        bool Remove(const char* path);
        bool GetNextFile(char* pathBuffer);
        void GetCurrentBasePath(char* pathBuffer);
                     
    private:
        class FileItem
        {
            friend class NormFileList;
            public:
                FileItem(const char* thePath);
                virtual ~FileItem();
                NormFile::Type GetType() {return NormFile::GetType(path);}
                off_t Size() const {return size;}
                virtual bool GetNextFile(char*   thePath,
                                         bool    reset,
                                         bool    updatesOnly,
                                         time_t  lastTime,
                                         time_t  thisTime,
                                         time_t& bigTime);
                    
            protected:        
                const char* Path() {return path;}
            
                char        path[PATH_MAX];
                off_t       size;
                FileItem*   prev;
                FileItem*   next;
        };
        class DirectoryItem : public FileItem
        {
            friend class NormFileList;
            public:
                DirectoryItem(const char* thePath);
                ~DirectoryItem();
                virtual bool GetNextFile(char*   thePath,
                                         bool    reset,
                                         bool    updatesOnly,
                                         time_t  lastTime,
                                         time_t  thisTime,
                                         time_t& bigTime);    
            private:
                NormDirectoryIterator diterator;
        };    
        
        time_t          this_time;
        time_t          big_time;
        time_t          last_time;
        bool            updates_only;
        FileItem*       head;  
        FileItem*       tail;
        FileItem*       next;
        bool            reset;
};  // end class NormFileList

#endif // _NORM_FILE
