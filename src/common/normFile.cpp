#include "normFile.h"

#include <string.h>  // for strerror()
#include <stdio.h>   // for rename()
#ifdef WIN32
#ifndef _WIN32_WCE
#include <errno.h>
#include <direct.h>
#include <share.h>
#include <io.h>
#endif // !_WIN32_WCE
#else
#include <unistd.h>
// Most don't have the dirfd() function
#ifndef HAVE_DIRFD
static inline int dirfd(DIR *dir) {return (dir->dd_fd);}
#endif // HAVE_DIRFD    
#endif // if/else WIN32

#ifdef HAVE_FLOCK
    #include <sys/file.h>
#elif defined(HAVE_LOCKF)
    #include <unistd.h>
#endif

#ifndef _WIN32_WCE
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif // !_WIN32_WCE

NormFile::NormFile()
#ifdef _WIN32_WCE
    : file_ptr(NULL)
#else
    : fd(-1)
#endif // if/else _WIN32_WCE
{    
}

NormFile::~NormFile()
{
    if (IsOpen()) Close();
}  // end NormFile::~NormFile()


// This should be called with a full path only!
bool NormFile::Open(const char* thePath, int theFlags)
{
    ASSERT(!IsOpen());	
    if (theFlags & O_CREAT)
    {
        // Create sub-directories as needed.
        char tempPath[PATH_MAX];
        strncpy(tempPath, thePath, PATH_MAX);
        char* ptr = strrchr(tempPath, PROTO_PATH_DELIMITER);
        if (NULL != ptr) 
        {
            *ptr = '\0';
            ptr = NULL;
            while (!NormFile::Exists(tempPath))
            {
                char* ptr2 = ptr;
                ptr = strrchr(tempPath, PROTO_PATH_DELIMITER);
                if (ptr2) *ptr2 = PROTO_PATH_DELIMITER;
                if (ptr)
                {
                    *ptr = '\0';
                }
                else
                {
                    ptr = tempPath;
                    break;
                }
            }
        }
        if (ptr && ('\0' == *ptr)) *ptr++ = PROTO_PATH_DELIMITER;
        while (ptr)
        {
            ptr = strchr(ptr, PROTO_PATH_DELIMITER);
            if (ptr) *ptr = '\0';
#ifdef WIN32
#ifdef _WIN32_WCE
#ifdef _UNICODE
            wchar_t wideBuffer[MAX_PATH];
            size_t pathLen = strlen(tempPath) + 1;
            if (pathLen > MAX_PATH) pathLen = MAX_PATH;
            mbstowcs(wideBuffer, tempPath, pathLen);
            if (!CreateDirectory(wideBuffer, NULL))
#else
            if (!CreateDirectory(tempPath, NULL))
#endif  // if/else _UNICODE
#else
            if (_mkdir(tempPath))
#endif // if/else _WIN32_WCE
#else
            if (mkdir(tempPath, 0755))
#endif // if/else WIN32/UNIX
            {
                PLOG(PL_FATAL, "NormFile::Open() mkdir(%s) error: %s\n",
                        tempPath, GetErrorString());
                return false;  
            }
            if (ptr) *ptr++ = PROTO_PATH_DELIMITER;
        }
    }    	
#ifdef WIN32
    // Make sure we're in binary mode (important for WIN32)
	theFlags |= O_BINARY;
#ifdef _WIN32_WCE
    if (theFlags & O_RDONLY)
        file_ptr = fopen(thePath, "rb");
    else
        file_ptr = fopen(thePath, "w+b");
    if (NULL != file_ptr)
#else
    // Allow sharing of read-only files but not of files being written
	if (theFlags & O_RDONLY)
		fd = _sopen(thePath, theFlags, _SH_DENYNO);
    else
		fd = _open(thePath, theFlags, 0640);
    if(fd >= 0)
#endif // if/else _WIN32_WCE
    {
        offset = 0;
		flags = theFlags;
        return true;  // no error
    }
    else
    {       
        PLOG(PL_FATAL, "Error opening file \"%s\": %s\n", thePath, GetErrorString());
		flags = 0;
        return false;
    }
#else     
    if((fd = open(thePath, theFlags, 0640)) >= 0)
    {
        offset = 0;
        return true;  // no error
    }
    else
    {    
        PLOG(PL_FATAL, "norm: Error opening file \"%s\": %s\n", 
                             thePath, GetErrorString());
        return false;
    }
#endif // if/else WIN32
}  // end NormFile::Open()

void NormFile::Close()
{
    if (IsOpen())
    {
#ifdef WIN32
#ifdef _WIN32_WCE
        fclose(file_ptr);
        file_ptr = NULL;
#else
        _close(fd);
        fd = -1;
#endif // if/else _WIN32_WCE
#else
        close(fd);
        fd = -1;
#endif // if/else WIN32
    }
}  // end NormFile::Close()


// Routines to try to get an exclusive lock on a file
bool NormFile::Lock()
{
#ifndef WIN32    // WIN32 files are automatically locked
    fchmod(fd, 0640 | S_ISGID);
#ifdef HAVE_FLOCK
    if (flock(fd, LOCK_EX | LOCK_NB))
        return false;
    else
#else
#ifdef HAVE_LOCKF
    if (0 != lockf(fd, F_LOCK, 0))  // assume lockf if not flock
        return false;
    else
#endif // HAVE_LOCKF
#endif // if/else HAVE_FLOCK
#endif // !WIN32
        return true;
}  // end NormFile::Lock()

void NormFile::Unlock()
{
#ifndef WIN32
#ifdef HAVE_FLOCK
    if (0 != flock(fd, LOCK_UN))
    {
        PLOG(PL_ERROR, "NormFile::Unlock() flock(%d) error: %s\n", fd, GetErrorString());
    }
#else
#ifdef HAVE_LOCKF
    if (0 != lockf(fd, F_ULOCK, 0))
    {
        PLOG(PL_ERROR, "NormFile::Unlock() lockf() error: %s\n", GetErrorString());
    }
#endif // HAVE_LOCKF
#endif // if/elseHAVE_FLOCK
    fchmod(fd, 0640);
#endif // !WIN32
}  // end NormFile::UnLock()

bool NormFile::Rename(const char* oldName, const char* newName)
{
    if (!strcmp(oldName, newName)) return true;  // no change required
    // Make sure the new file name isn't an existing "busy" file
    // (This also builds sub-directories as needed)
    if (NormFile::IsLocked(newName)) 
    {
        PLOG(PL_FATAL, "NormFile::Rename() error: file is locked\n");
        return false;    
    }
#ifdef WIN32
    // In Win32, the new file can't already exist
	if (NormFile::Exists(newName)) 
    {
#ifdef _WIN32_WCE
#ifdef _UNICODE
        wchar_t wideBuffer[MAX_PATH];
        size_t pathLen = strlen(newName) + 1;
        if (pathLen > MAX_PATH) pathLen = MAX_PATH;
        mbstowcs(wideBuffer, newName, pathLen);
        if (0 == DeleteFile(wideBuffer))
#else
        if (0 == DeleteFile(newName))
#endif // if/else _UNICODE
        {
            PLOG(PL_FATAL, "NormFile::Rename() DeleteFile() error: %s\n", GetErrorString());
            return false;
        }
#else
        if (0 != _unlink(newName))
        {
            PLOG(PL_FATAL, "NormFile::Rename() _unlink() error: %s\n", GetErrorString());
            return false;
        }
#endif // if/else _WIN32_WCE
    }
    // In Win32, the old file can't be open
	int oldFlags = 0;
	if (IsOpen())
	{
		oldFlags = flags;
		oldFlags &= ~(O_CREAT | O_TRUNC);  // unset these
		Close();
	}  
#endif  // WIN32
    // Create sub-directories as needed.
    char tempPath[PATH_MAX];
    strncpy(tempPath, newName, PATH_MAX);
    char* ptr = strrchr(tempPath, PROTO_PATH_DELIMITER);
    if (ptr) *ptr = '\0';
    ptr = NULL;
    while (!NormFile::Exists(tempPath))
    {
        char* ptr2 = ptr;
        ptr = strrchr(tempPath, PROTO_PATH_DELIMITER);
        if (ptr2) *ptr2 = PROTO_PATH_DELIMITER;
        if (ptr)
        {
            *ptr = '\0';
        }
        else
        {
            ptr = tempPath;
            break;
        }
    }
    if (ptr && ('\0' == *ptr)) *ptr++ = PROTO_PATH_DELIMITER;
    while (ptr)
    {
        ptr = strchr(ptr, PROTO_PATH_DELIMITER);
        if (ptr) *ptr = '\0';
#ifdef WIN32
#ifdef _WIN32_WCE
#ifdef _UNICODE
            wchar_t wideBuffer[MAX_PATH];
            size_t pathLen = strlen(tempPath) + 1;
            if (pathLen > MAX_PATH) pathLen = MAX_PATH;
            mbstowcs(wideBuffer, tempPath, pathLen);
            if (!CreateDirectory(wideBuffer, NULL))
#else
            if (!CreateDirectory(tempPath, NULL))
#endif  // if/else _UNICODE
#else
            if (0 != _mkdir(tempPath))
#endif // if/else _WIN32_WCE
#else
        if (mkdir(tempPath, 0755))
#endif // if/else WIN32/UNIX
        {
            PLOG(PL_FATAL, "NormFile::Rename() mkdir(%s) error: %s\n",
                    tempPath, GetErrorString());
            return false;  
        }
        if (ptr) *ptr++ = PROTO_PATH_DELIMITER;
    }  
#ifdef _WIN32_WCE
#ifdef _UNICODE
    wchar_t wideOldName[MAX_PATH];
    wchar_t wideNewName[MAX_PATH];
    mbstowcs(wideOldName, oldName, MAX_PATH);
    mbstowcs(wideNewName, newName, MAX_PATH);
    if (!MoveFile(wideOldName, wideNewName))
#else
    if (!MoveFile(oldName, newName))
#endif // if/else _UNICODE
    {
        PLOG(PL_ERROR, "NormFile::Rename() MoveFile() error: %s\n", GetErrorString());
#else
    if (rename(oldName, newName))
    {
        PLOG(PL_ERROR, "NormFile::Rename() rename() error: %s\n", GetErrorString());	
#endif // if/else _WIN32_WCE
#ifdef WIN32
        if (oldFlags) 
        {
            if (Open(oldName, oldFlags))
                offset = 0;
            else
                PLOG(PL_ERROR, "NormFile::Rename() error re-opening file w/ old name\n");
        }
#endif // WIN32        
        return false;
    }
    else
    {
#ifdef WIN32
        // (TBD) Is the file offset OK doing this???
        if (oldFlags) 
        {
            if (Open(newName, oldFlags))
                offset = 0;
            else
                PLOG(PL_ERROR, "NormFile::Rename() error opening file w/ new name\n");
        }
#endif // WIN32
        return true;
    }
}  // end NormFile::Rename()

size_t NormFile::Read(char* buffer, size_t len)
{
    ASSERT(IsOpen());
    
    size_t got = 0;
    while (got < len)
    {
#ifdef WIN32
#ifdef _WIN32_WCE
        size_t result = fread(buffer+got, 1, len-got, file_ptr);
#else
        size_t result = _read(fd, buffer+got, (unsigned int)(len-got));
#endif // if/else _WIN32_WCE
#else
        ssize_t result = read(fd, buffer+got, len-got);
#endif // if/else WIN32
        if (result <= 0)
        {
#ifndef _WIN32_WCE
            if (EINTR != errno)
#endif // !_WIN32_WCE
            {
                PLOG(PL_FATAL, "NormFile::Read() read(%d) result:%d error:%s (offset:%d)\n", len, result, GetErrorString(), offset);
                return 0;
            }
        }
        else
        {
            got += result;
            offset += (Offset)result;
        }
    }  // end while (have < want)
    return got;
}  // end NormFile::Read()

size_t NormFile::Write(const char* buffer, size_t len)
{
    ASSERT(IsOpen());
    size_t put = 0;
    while (put < len)
    {
#ifdef WIN32
#ifdef _WIN32_WCE
        size_t result = fwrite(buffer+put, 1, len-put, file_ptr);
#else
        size_t result = _write(fd, buffer+put, (unsigned int)(len-put));
#endif // if/else _WIN32_WCE
#else
        size_t result = write(fd, buffer+put, len-put);
#endif // if/else WIN32
        if (result <= 0)
        {
 #ifndef _WIN32_WCE
            if (EINTR != errno)
#endif // !_WIN32_WCE
            {
                PLOG(PL_FATAL, "NormFile::Write() write(%d) result:%d error: %s\n", len, result, GetErrorString());
                return 0;
            }
        }
        else
        {
            offset += (Offset)result;
            put += result;
        }
    }  // end while (put < len)
    return put;
}  // end NormFile::Write()

bool NormFile::Seek(Offset theOffset)
{
    ASSERT(IsOpen());
#ifdef WIN32
#ifdef _WIN32_WCE
    // (TBD) properly support big files on WinCE
    Offset result = fseek(file_ptr, (long)theOffset, SEEK_SET);
#else
    Offset result = _lseeki64(fd, theOffset, SEEK_SET);
#endif // if/else _WIN32_WCE
#else
    Offset result = lseek(fd, theOffset, SEEK_SET);
#endif // if/else WIN32
    if (result == (Offset)-1)
    {
        PLOG(PL_FATAL, "NormFile::Seek() lseek() error: %s\n", GetErrorString());
        return false;
    }
    else
    {
        offset = result;
        return true; // no error
    }
}  // end NormFile::Seek()

bool NormFile::Pad(Offset theOffset)
{
    if (theOffset > GetSize())
    {
        if (Seek(theOffset - 1))
        {
            char byte = 0;
            if (1 != Write(&byte, 1))
            {
                PLOG(PL_FATAL, "NormFile::Pad() write error: %s\n", GetErrorString());
                return false;
            }
        }
        else
        {
            PLOG(PL_FATAL, "NormFile::Pad() seek error: %s\n", GetErrorString());
            return false;
        }
    }
    return true; 
}  // end NormFile::Pad()

NormFile::Offset NormFile::GetSize() const
{
    ASSERT(IsOpen());
#ifdef _WIN32_WCE
    DWORD fileSize = GetFileSize(_fileno(file_ptr), NULL);
    return ((Offset)fileSize);
#else
#ifdef WIN32
    struct _stati64 info;                  // instead of "struct _stat"
    int result = _fstati64(fd, &info);     // instead of "_fstat()"
#else
    struct stat info;
    int result = fstat(fd, &info);
#endif // if/else WIN32
    if (result)
    {
        PLOG(PL_FATAL, "Error getting file size: %s\n", GetErrorString());
        return 0;   
    }
    else
    {
        return info.st_size;
    }
#endif // if/else _WIN32_WCE
}  // end NormFile::GetSize()


/***********************************************
 * The NormDirectoryIterator classes is used to
 * walk directory trees for file transmission
 */

NormDirectoryIterator::NormDirectoryIterator()
    : current(NULL)
{

}

NormDirectoryIterator::~NormDirectoryIterator()
{
    Close();
}

bool NormDirectoryIterator::Open(const char *thePath)
{
    if (current) Close();
#ifdef WIN32
#ifdef _WIN32_WCE
    if (thePath && !NormFile::Exists(thePath))
#else
    if (thePath && _access(thePath, 0))
#endif // if/else _WIN32_WCE
#else
    if (thePath && access(thePath, X_OK))
#endif // if/else WIN32
    {
        PLOG(PL_FATAL, "NormDirectoryIterator: can't access directory: %s\n", thePath);
        return false;
    }
    current = new NormDirectory(thePath);
    if (current && current->Open())
    {
        path_len = (int)strlen(current->Path());
        path_len = MIN(PATH_MAX, path_len);
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormDirectoryIterator: can't open directory: %s\n", thePath);
        if (current) delete current;
        current = NULL;
        return false;
    }
}  // end NormDirectoryIterator::Init()

void NormDirectoryIterator::Close()
{
    NormDirectory* d;
    while ((d = current))
    {
        current = d->parent;
        d->Close();
        delete d;
    }
}  // end NormDirectoryIterator::Close()

bool NormDirectoryIterator::GetPath(char* pathBuffer)
{
    if (current)
    {
        NormDirectory* d = current;
        while (d->parent) d = d->parent;
        strncpy(pathBuffer, d->Path(), PATH_MAX);
        return true;
    }
    else
    {
        pathBuffer[0] = '\0';
        return false;
    }
}

#ifdef WIN32
bool NormDirectoryIterator::GetNextFile(char* fileName)
{
	if (!current) return false;
	bool success = true;
	while(success)
	{
		WIN32_FIND_DATA findData;
		if (current->hSearch == (HANDLE)-1)
		{
			// Construct search string
			current->GetFullName(fileName);
			strcat(fileName, "\\*");
#ifdef _UNICODE
            wchar_t wideBuffer[MAX_PATH];
            mbstowcs(wideBuffer, fileName, MAX_PATH);
            if ((HANDLE)-1 == 
			    (current->hSearch = FindFirstFile(wideBuffer, &findData)))
#else
			if ((HANDLE)-1 == 
			    (current->hSearch = FindFirstFile(fileName, &findData)))
#endif // if/else _UNICODE
			    success = false;
			else
				success = true;
		}
		else
		{
			success = (0 != FindNextFile(current->hSearch, &findData));
		}

		// Do we have a candidate file?
		if (success)
		{
#ifdef _UNICODE
            char cFileName[MAX_PATH];
            wcstombs(cFileName, findData.cFileName, MAX_PATH);
            char* ptr = strrchr(cFileName, PROTO_PATH_DELIMITER);
#else
			char* ptr = strrchr(findData.cFileName, PROTO_PATH_DELIMITER);
#endif // if/else _UNICODE
			if (ptr)
				ptr += 1;
			else
#ifdef _UNICODE
                ptr = cFileName;
#else
				ptr = findData.cFileName;
#endif // if/else _UNICODE

			// Skip "." and ".." directories
			if (ptr[0] == '.')
			{
				if ((1 == strlen(ptr)) ||
					((ptr[1] == '.') && (2 == strlen(ptr))))
				{
					continue;
				}
			}
			current->GetFullName(fileName);
			strcat(fileName, ptr);
			NormFile::Type type = NormFile::GetType(fileName);
			if (NormFile::NORMAL == type)
			{
                size_t nameLen = strlen(fileName);
                nameLen = MIN(PATH_MAX, nameLen);
				nameLen -= path_len;
				memmove(fileName, fileName+path_len, nameLen);
				if (nameLen < PATH_MAX) fileName[nameLen] = '\0';
				return true;
			}
			else if (NormFile::DIRECTORY == type)
			{

				NormDirectory *dir = new NormDirectory(ptr, current);
				if (dir && dir->Open())
				{
					// Push sub-directory onto stack and search it
					current = dir;
					return GetNextFile(fileName);
				}
				else
				{
					// Couldn't open try next one
					if (dir) delete dir;
				}
			}
			else
			{
				// NormFile::INVALID file, try next one
			}
		}  // end if(success)
	}  // end while(success)

	// if parent, popup a level and continue search
	if (current->parent)
	{
		current->Close();
		NormDirectory *dir = current;
		current = current->parent;
		delete dir;
		return GetNextFile(fileName);
	}
	else
	{
		current->Close();
		delete current;
		current = NULL;
		return false;
	}	
}  // end NormDirectoryIterator::GetNextFile()  (WIN32)
#else
bool NormDirectoryIterator::GetNextFile(char* fileName)
{   
    if (!current) return false;
    struct dirent *dp;
    while((dp = readdir(current->dptr)))
    {
        // Make sure it's not "." or ".."
        if (dp->d_name[0] == '.')
        {
            if ((1 == strlen(dp->d_name)) ||
                ((dp->d_name[1] == '.' ) && (2 == strlen(dp->d_name))))
            {
                continue;  // skip "." and ".." directory names
            }
        }
        current->GetFullName(fileName);
        strcat(fileName, dp->d_name);
        NormFile::Type type = NormFile::GetType(fileName);        
        if (NormFile::NORMAL == type)
        {
            size_t nameLen = strlen(fileName);
            nameLen = MIN(PATH_MAX, nameLen);
            nameLen -= path_len;
            memmove(fileName, fileName+path_len, nameLen);
            if (nameLen < PATH_MAX) fileName[nameLen] = '\0';
            return true;
        } 
        else if (NormFile::DIRECTORY == type)
        {
            NormDirectory *dir = new NormDirectory(dp->d_name, current);
            if (dir && dir->Open())
            {
                // Push sub-directory onto stack and search it
                current = dir;
                return GetNextFile(fileName);
            }
            else
            {
                // Couldn't open this one, try next one
                if (dir) delete dir;
            }
        }
        else
        {
            // NormFile::INVALID, try next one
        }
    }  // end while(readdir())
   
    // Pop up a level and recursively continue or finish if done
    if (current->parent)
    {
        char path[PATH_MAX];
        current->parent->GetFullName(path);
        current->Close();
        NormDirectory *dir = current;
        current = current->parent;
        delete dir;
        return GetNextFile(fileName);
    }
    else
    {
        current->Close();
        delete current;
        current = NULL;
        return false;  // no more files remain
    }      
}  // end NormDirectoryIterator::GetNextFile() (UNIX)
#endif // if/else WIN32

NormDirectoryIterator::NormDirectory::NormDirectory(const char*    thePath, 
                                                    NormDirectory* theParent)
    : parent(theParent),
#ifdef WIN32
    hSearch((HANDLE)-1)
#else
    dptr(NULL)
#endif // if/else WIN32 
{
    strncpy(path, thePath, PATH_MAX);
    size_t len  = MIN(PATH_MAX, strlen(path));
    if ((len < PATH_MAX) && (PROTO_PATH_DELIMITER != path[len-1]))
    {
        path[len++] = PROTO_PATH_DELIMITER;
        if (len < PATH_MAX) path[len] = '\0';
    }
}

NormDirectoryIterator::NormDirectory::~NormDirectory()
{
    Close();
}

bool NormDirectoryIterator::NormDirectory::Open()
{
    Close();  // in case it's already open   
    char fullName[PATH_MAX];
    GetFullName(fullName);  
    // Get rid of trailing PROTO_PATH_DELIMITER
    size_t len = MIN(PATH_MAX, strlen(fullName));
    if (PROTO_PATH_DELIMITER == fullName[len-1]) fullName[len-1] = '\0';
#ifdef WIN32
#ifdef _UNICODE
    wchar_t wideBuffer[MAX_PATH];
    mbstowcs(wideBuffer, fullName, MAX_PATH);
    DWORD attr = GetFileAttributes(wideBuffer);
#else
    DWORD attr = GetFileAttributes(fullName);
#endif // if/else _UNICODE
	if (0xFFFFFFFF == attr)
		return false;
	else if (attr & FILE_ATTRIBUTE_DIRECTORY)
		return true;
	else
		return false;
#else
    if((dptr = opendir(fullName)))
        return true;
    else    
        return false;
#endif // if/else WIN32
    
} // end NormDirectoryIterator::NormDirectory::Open()

void NormDirectoryIterator::NormDirectory::Close()
{
#ifdef WIN32
    if (hSearch != (HANDLE)-1) 
	{
		FindClose(hSearch);
		hSearch = (HANDLE)-1;
	}
#else
    if (dptr)
    {
        closedir(dptr);
        dptr = NULL;
    }
#endif  // if/else WIN32
}  // end NormDirectoryIterator::NormDirectory::Close()


void NormDirectoryIterator::NormDirectory::GetFullName(char* ptr)
{
    ptr[0] = '\0';
    RecursiveCatName(ptr);
}  // end NormDirectoryIterator::NormDirectory::GetFullName()

void NormDirectoryIterator::NormDirectory::RecursiveCatName(char* ptr)
{
    if (parent) parent->RecursiveCatName(ptr);
    size_t len = MIN(PATH_MAX, strlen(ptr));
    strncat(ptr, path, PATH_MAX-len);
}  // end NormDirectoryIterator::NormDirectory::RecursiveCatName()

// Below are some static routines for getting file/directory information

// Is the named item a valid directory or file (or neither)??
NormFile::Type NormFile::GetType(const char* path)
{
#ifdef WIN32
#ifdef _UNICODE
    wchar_t wideBuffer[MAX_PATH];
    mbstowcs(wideBuffer, path, MAX_PATH);
    DWORD attr = GetFileAttributes(wideBuffer);
#else
    DWORD attr = GetFileAttributes(path);
#endif // if/else _UNICODE
	if (0xFFFFFFFF == attr)
		return INVALID;  // error
	else if (attr & FILE_ATTRIBUTE_DIRECTORY)
		return DIRECTORY;
	else
		return NORMAL;
#else
    struct stat file_info;  
    if (stat(path, &file_info)) 
        return INVALID;  // stat() error
    else if ((S_ISDIR(file_info.st_mode)))
        return DIRECTORY;
    else 
        return NORMAL;
#endif // if/else WIN32
}  // end NormFile::GetType()

NormFile::Offset NormFile::GetSize(const char* path)
{
#ifdef _WIN32_WCE
    WIN32_FIND_DATA findData;
#ifdef _UNICODE
    wchar_t wideBuffer[MAX_PATH];
    mbstowcs(wideBuffer, path, MAX_PATH);
    if (INVALID_HANDLE_VALUE != FindFirstFile(wideBuffer, &findData))
#else
    if (INVALID_HANDLE_VALUE != FindFirstFile(path, &findData))
#endif // if/else _UNICODE
    {
		Offset fileSize = findData.nFileSizeLow;
		if (sizeof(Offset) > 4)
			fileSize |= ((Offset)findData.nFileSizeHigh) << (8*sizeof(DWORD));
        return fileSize;
    }
    else
    {
        PLOG(PL_ERROR, "Error getting file size: %s\n", GetErrorString());
        return 0;
    }
#else
#ifdef WIN32
    struct _stati64 info;               // instead of "struct _stat"
    int result = _stati64(path, &info); // instead of "_stat()"
#else
    struct stat info;
    int result = stat(path, &info);
#endif // if/else WIN32    
    if (result)
    {
        //DMSG(0, "Error getting file size: %s\n", GetErrorString());
        return 0;   
    }
    else
    {
        return info.st_size;
    }
#endif // if/else _WIN32_WCE
}  // end NormFile::GetSize()


time_t NormFile::GetUpdateTime(const char* path)
{
#ifdef _WIN32_WCE
    WIN32_FIND_DATA findData;
#ifdef _UNICODE
    wchar_t wideBuffer[MAX_PATH];
    mbstowcs(wideBuffer, path, MAX_PATH);
    if (INVALID_HANDLE_VALUE != FindFirstFile(wideBuffer, &findData))
#else
    if (INVALID_HANDLE_VALUE != FindFirstFile(path, &findData))
#endif // if/else _UNICODE
    {
        ULARGE_INTEGER ctime = {findData.ftCreationTime.dwLowDateTime,
                                findData.ftCreationTime.dwHighDateTime};
        ULARGE_INTEGER atime = {findData.ftLastAccessTime.dwLowDateTime,
                                findData.ftLastAccessTime.dwHighDateTime};
        ULARGE_INTEGER mtime = {findData.ftLastWriteTime.dwLowDateTime,
                                findData.ftLastWriteTime.dwHighDateTime};
        if (ctime.QuadPart < atime.QuadPart) ctime = atime;
        if (ctime.QuadPart < mtime.QuadPart) ctime = mtime;
        const ULARGE_INTEGER epochTime = {0xD53E8000, 0x019DB1DE};
        ctime.QuadPart -= epochTime.QuadPart;
        // Convert sytem time to seconds
        ctime.QuadPart /= 10000000;
        return ctime.LowPart;
    }
    else
    {
        PLOG(PL_ERROR, "Error getting file size: %s\n", GetErrorString());
        return 0;
    }
#else
#ifdef WIN32
    struct _stati64 info;               // instead of "struct _stat"
    int result = _stati64(path, &info); // instead of "_stat()"
#else
    struct stat info; 
    int result = stat(path, &info);
#endif // if/else WIN32   
    if (result) 
    {
        return (time_t)0;  // stat() error
    }
    else 
    {
#ifdef WIN32
        // Hack because Win2K and Win98 seem to work differently
		//__time64_t updateTime = MAX(info.st_ctime, info.st_atime);
        time_t updateTime = MAX(info.st_ctime, info.st_atime);
		updateTime = MAX(updateTime, info.st_mtime);
		return ((time_t)updateTime);
#else
        return info.st_ctime; 
#endif // if/else WIN32
    } 
#endif // if/else _WIN32_WCE
}  // end NormFile::GetUpdateTime()

bool NormFile::IsLocked(const char* path)
{
    // If file doesn't exist, it's not locked
    if (!Exists(path)) return false;      
    NormFile testFile;
#ifdef WIN32
    if(!testFile.Open(path, O_WRONLY | O_CREAT))
#else
    if(!testFile.Open(path, O_WRONLY | O_CREAT))    
#endif // if/else WIN32
    {
        return true;
    }
    else if (testFile.Lock())
    {
        // We were able to lock the file successfully
        testFile.Unlock();
        testFile.Close();
        return false;
    }
    else
    {
        testFile.Close();
        return true;
    }
}  // end NormFile::IsLocked()

bool NormFile::Unlink(const char* path)
{
    // Don't unlink a file that is open (locked)
    if (NormFile::IsLocked(path))
    {
        return false;
    }

#ifdef _WIN32_WCE
#ifdef _UNICODE
    wchar_t wideBuffer[MAX_PATH];
    mbstowcs(wideBuffer, path, MAX_PATH);
    if (0 == DeleteFile(wideBuffer))
#else
    if (0 == DeleteFile(path))
#endif // if/else _UNICODE
    {
        PLOG(PL_FATAL, "NormFile::Unlink() DeletFile() error: %s\n", GetErrorString());
        return false;
    }
#else
#ifdef WIN32
    if (_unlink(path))
#else
    if (unlink(path))
#endif // if/else WIN32
    {
        PLOG(PL_FATAL, "NormFile::Unlink() unlink error: %s\n", GetErrorString());
        return false;
    }
#endif // if/else _WIN32_WCE
    else
    {
        return true;
    }

}  // end NormFile::Unlink()

NormFileList::NormFileList()
 : this_time(0), big_time(0), last_time(0),
   updates_only(false), head(NULL), tail(NULL), next(NULL)
{ 
}
        
NormFileList::~NormFileList()
{
    Destroy();
}

void NormFileList::Destroy()
{
    while ((next = head))
    {
        head = next->next;
        delete next;   
    }
    tail = NULL;
}  // end NormFileList::Destroy()

bool NormFileList::Append(const char* path)
{
    FileItem* theItem = NULL;
    switch(NormFile::GetType(path))
    {
        case NormFile::NORMAL:
            theItem = new FileItem(path);
            break;
        case NormFile::DIRECTORY:
            theItem = new DirectoryItem(path);
            break;
        default:
            // Allow non-existent files for update_only mode
            // (TBD) allow non-existent directories?
            if (updates_only) 
            {
                theItem = new FileItem(path);
            }
            else
            {
                PLOG(PL_FATAL, "NormFileList::Append() Bad file/directory name: %s\n",
                        path);
                return false;
            }
            break;
    }
    if (theItem)
    {
        theItem->next = NULL;
        if ((theItem->prev = tail))
            tail->next = theItem;
        else
            head = theItem;
        tail = theItem;
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormFileList::Append() Error creating file/directory item: %s\n",
                GetErrorString());
        return false;
    }
}  // end NormFileList::Append()

bool NormFileList::Remove(const char* path)
{
    FileItem* nextItem = head;
    size_t pathLen = strlen(path);
    pathLen = MIN(pathLen, PATH_MAX);
    while (nextItem)
    {
        size_t nameLen = strlen(nextItem->Path());
        nameLen = MIN(nameLen, PATH_MAX);
        nameLen = MAX(nameLen, pathLen);
        if (!strncmp(path, nextItem->Path(), nameLen))
        {
            if (nextItem == next) next = nextItem->next;
            if (nextItem->prev)
                nextItem->prev = next = nextItem->next;
            else
                head = nextItem->next;
            if (nextItem->next)
                nextItem->next->prev = nextItem->prev;
            else
                tail = nextItem->prev;
            return true;
        }
    }
    return false;
}  // end NormFileList::Remove()

bool NormFileList::GetNextFile(char* pathBuffer)
{
    if (!next)
    {
        next = head;
        reset = true;
    }
    if (next)
    {
        if (next->GetNextFile(pathBuffer, reset, updates_only,
                              last_time, this_time, big_time))
        {
            reset = false;
            return true;
        }
        else
        {
            if (next->next)
            {
                next = next->next;
                reset = true;
                return GetNextFile(pathBuffer);
            }
            else
            {
                reset = false;
                return false;  // end of list
            }   
        }
    }
    else
    {
        return false;  // empty list
    }
}  // end NormFileList::GetNextFile()

void NormFileList::GetCurrentBasePath(char* pathBuffer)
{
    if (next)
    {
        if (NormFile::DIRECTORY == next->GetType())
        {
            strncpy(pathBuffer, next->Path(), PATH_MAX);
            size_t len = strlen(pathBuffer);
            len = MIN(len, PATH_MAX);
            if (PROTO_PATH_DELIMITER != pathBuffer[len-1])
            {
                if (len < PATH_MAX) pathBuffer[len++] = PROTO_PATH_DELIMITER;
                if (len < PATH_MAX) pathBuffer[len] = '\0';
            }   
        }
        else  // NormFile::NORMAL
        {
            const char* ptr = strrchr(next->Path(), PROTO_PATH_DELIMITER);
            if (ptr++)
            {
                size_t len = ptr - next->Path();
                strncpy(pathBuffer, next->Path(), len);
                pathBuffer[len] = '\0';
            }
            else
            {
                pathBuffer[0] = '\0';
            }
        }        
    }
    else
    {
        pathBuffer[0] = '\0';
    }
}  // end NormFileList::GetBasePath()


NormFileList::FileItem::FileItem(const char* thePath)
 : prev(NULL), next(NULL)
{
    size_t len = strlen(thePath);
    len = MIN(len, PATH_MAX);
    strncpy(path, thePath, PATH_MAX);
    size = NormFile::GetSize(thePath);    
} 

NormFileList::FileItem::~FileItem()
{
}

bool NormFileList::FileItem::GetNextFile(char*   thePath,
                                         bool    reset,
                                         bool    updatesOnly,
                                         time_t  lastTime,
                                         time_t  thisTime,
                                         time_t& bigTime)
{
    if (reset)
    {
        if (updatesOnly)
        {
            time_t updateTime = NormFile::GetUpdateTime(thePath);
            if (updateTime > bigTime) bigTime = updateTime;
            if ((updateTime <= lastTime) || (updateTime > thisTime))
                return false;
        }
        strncpy(thePath, path, PATH_MAX);
        return true;
    }
    else
    {
        return false;
    }
}  // end NormFileList::FileItem::GetNextFile()

NormFileList::DirectoryItem::DirectoryItem(const char* thePath)
 : NormFileList::FileItem(thePath)
{    
    
}

NormFileList::DirectoryItem::~DirectoryItem()
{
    diterator.Close();
}

bool NormFileList::DirectoryItem::GetNextFile(char*   thePath,
                                              bool    reset,
                                              bool    updatesOnly,
                                              time_t  lastTime,
                                              time_t  thisTime,
                                              time_t& bigTime)
{
     if (reset)
     {
         /* For now we are going to poll all files in a directory individually
           since directory update times aren't always changed when files are
           are replaced within the directory tree ... uncomment this code
           if you only want to check directory nodes that have had their
           change time updated
        if (updates_only)
        {
            // Check to see if directory has been touched
            time_t update_time = MdpFileGetUpdateTime(path);
            if (updateTime > bigTime) *bigTime = updateTime;
            if ((updateTime <= lastTime) || (updateTime > thisTime))
                return false;
        } */
        if (!diterator.Open(path))
        {
            PLOG(PL_FATAL, "NormFileList::DirectoryItem::GetNextFile() Directory iterator init error\n");
            return false;   
        } 
     }
     strncpy(thePath, path, PATH_MAX);
     size_t len = strlen(thePath);
     len = MIN(len, PATH_MAX);
     if ((PROTO_PATH_DELIMITER != thePath[len-1]) && (len < PATH_MAX))
     {
         thePath[len++] = PROTO_PATH_DELIMITER;
         if (len < PATH_MAX) thePath[len] = '\0';
     }  
     char tempPath[PATH_MAX];
     while (diterator.GetNextFile(tempPath))
     {
         size_t maxLen = PATH_MAX - len;
         strncat(thePath, tempPath, maxLen);
         if (updatesOnly)
         {
            time_t updateTime = NormFile::GetUpdateTime(thePath);
            if (updateTime > bigTime) bigTime = updateTime;
            if ((updateTime <= lastTime) || (updateTime > thisTime))
            {
                thePath[len] = '\0';
                continue;
            }
         }
         return true;
     }
     return false;
}  // end NormFileList::DirectoryItem::GetNextFile()
