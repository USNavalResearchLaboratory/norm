#ifndef _NORM_FILE
#define _NORM_FILE

// This module defines some simple classes for manipulating files.
// Unix and Win32 platforms are supported.  Routines for iterating
// over directories are also provided.  And a file/directory list
// class is provided to manage a list of files.

#ifdef WIN32
//#include <io.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif // if/else WIN32

#ifdef _WIN32_WCE
#include <stdio.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif // if/else _WIN32_WCE

// From PROTOLIB
#include "protokit.h"    // for Protolib stuff

// (TBD) Rewrite this implementation to use 
// native WIN32 APIs on that platform !!!

#ifdef _WIN32_WCE
// Here we enumerate some stuff to 
// make the NormFile work on WinCE
enum
{
    O_CREAT  = 0x01,
    O_TRUNC  = 0x02,    
    O_RDONLY = 0x04,
    O_WRONLY = 0x08,
    O_RDWR   = 0x10,
    O_BINARY = 0x20
};
#endif // _WIN32_WCE
        
class NormFile
{
    public:
#ifdef WIN32
		typedef __int64 Offset;
#else
		typedef off_t Offset;
#endif // if/else WIN32/UNIX
        enum Type {INVALID, NORMAL, DIRECTORY};        
        NormFile();
        ~NormFile();
        bool Open(const char* path, int theFlags);
        bool Lock();
        void Unlock();
        bool Rename(const char* oldName, const char* newName);
        static bool Unlink(const char *path);
        void Close();
        bool IsOpen() const 
        {
#ifdef _WIN32_WCE
            return (NULL != file_ptr);
#else
            return (fd >= 0);
#endif // _WIN32_WCE
        }
        size_t Read(char* buffer, size_t len);
        size_t Write(const char* buffer, size_t len);
        bool Seek(Offset theOffset);
		NormFile::Offset GetOffset() const {return (offset);}
		NormFile::Offset GetSize() const;
        bool Pad(Offset theOffset);  // if file size is less than theOffset, writes a byte to force filesize
        
        // static helper methods
        static NormFile::Type GetType(const char *path);
		static NormFile::Offset GetSize(const char* path);
        static time_t GetUpdateTime(const char* path);
        static bool IsLocked(const char *path);
         
        static bool Exists(const char* path)
        {
#ifdef WIN32
#ifdef _UNICODE
            wchar_t wideBuffer[MAX_PATH];
            size_t count = mbstowcs(wideBuffer, path, strlen(path)+1);
            return (0xFFFFFFFF != GetFileAttributes(wideBuffer));
#else
            return (0xFFFFFFFF != GetFileAttributes(path));
#endif // if/else _UNICODE
#else
            return (0 == access(path, F_OK));
#endif  // if/else WIN32
        }
        
        static bool IsWritable(const char* path)
        {
#ifdef WIN32
#ifdef _UNICODE
            wchar_t wideBuffer[MAX_PATH];
            size_t count = mbstowcs(wideBuffer, path, strlen(path)+1);
            DWORD attr = GetFileAttributes(wideBuffer);
#else
            DWORD attr = GetFileAttributes(path);
#endif // if/else _UNICODE
	        return ((0xFFFFFFFF == attr) ? 
                        false : (0 == (attr & FILE_ATTRIBUTE_READONLY)));
#else
            return (0 == access(path, W_OK));
#endif // if/else WIN32
        }
       
    
    // Members
    //private:
#ifdef _WIN32_WCE
        FILE*   file_ptr;
#else
        int     fd;
#endif // if/else _WIN32_WCE
        int     flags;
#ifdef WIN32
		__int64 offset;
#else
        off_t   offset;
#endif // if/else WIN32/UNIX
};  // end class NormFile


        
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
            if (0 == initTime)
            {
                struct timeval currentTime;
                ProtoSystemTime(currentTime);
                this_time = currentTime.tv_sec;   
            }
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
				NormFile::Offset Size() const {return size;}
                virtual bool GetNextFile(char*   thePath,
                                         bool    reset,
                                         bool    updatesOnly,
                                         time_t  lastTime,
                                         time_t  thisTime,
                                         time_t& bigTime);
                    
            protected:        
                const char* Path() {return path;}
            
                char			 path[PATH_MAX];
				NormFile::Offset size;
                FileItem*		 prev;
                FileItem*		 next;
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
