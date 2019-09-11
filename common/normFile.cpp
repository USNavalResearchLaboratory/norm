
#include "normFile.h"

#include <string.h>  // for strerror()
#include <errno.h>   // for errno

#ifdef WIN32
#include <direct.h>
#include <share.h>
#else
#include <unistd.h>
// Most don't have the dirfd() function
#ifndef HAVE_DIRFD
static inline int dirfd(DIR *dir) {return (dir->dd_fd);}
#endif // HAVE_DIRFD    
#endif // if/else WIN32

NormFile::NormFile()
    : fd(-1)
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
        char* ptr = strrchr(tempPath, DIR_DELIMITER);
        if (ptr) *ptr = '\0';
        ptr = NULL;
        while (!NormFile::Exists(tempPath))
        {
            char* ptr2 = ptr;
            ptr = strrchr(tempPath, DIR_DELIMITER);
            if (ptr2) *ptr2 = DIR_DELIMITER;
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
        if (ptr && ('\0' == *ptr)) *ptr++ = DIR_DELIMITER;
        while (ptr)
        {
            ptr = strchr(ptr, DIR_DELIMITER);
            if (ptr) *ptr = '\0';
            if (mkdir(tempPath, 0755))
            {
                DMSG(0, "NormFile::Rename() mkdir(%s) error: %s\n",
                        tempPath, strerror(errno));
                return false;  
            }
            if (ptr) *ptr++ = DIR_DELIMITER;
        }
    }    	
#ifdef WIN32
    // Make sure we're in binary mode (important for WIN32)
	theFlags |= _O_BINARY;
    // Allow sharing of read-only files but not of files being written
	if (theFlags & _O_RDONLY)
		fd = _sopen(thePath, theFlags, _SH_DENYNO);
    else
		fd = _open(thePath, theFlags, 0640);
    if(fd >= 0)
    {
        offset = 0;
		flags = theFlags;
        return true;  // no error
    }
    else
    {       
        DMSG(0, "Error opening file \"%s\": %s\n", path,
                strerror(errno));
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
        DMSG(0, "norm: Error opening file \"%s\": %s\n", 
                             thePath, strerror(errno));
        return false;
    }
#endif // if/else WIN32
}  // end NormFile::Open()

void NormFile::Close()
{
    if (IsOpen())
    {
#ifdef WIN32
        _close(fd);
#else
        close(fd);
#endif // if/else WIN32
        fd = -1;
    }
}  // end NormFile::Close()


// Routines to try to get an exclusive lock on a file
bool NormFile::Lock()
{
#ifndef WIN32    // WIN32 files are automatically locked
    fchmod(fd, 0640 | S_ISGID);
#ifdef HAVE_FLOCK
    if (flock(fd, LOCK_EX | LOCK_NB))
#else
//#ifdef HAVE_LOCKF
    if (lockf(fd, F_LOCK, 0))  // assume lockf if not flock
//#endif // HAVE_LOCKF
#endif // if/else HAVE_FLOCK
        return false;
    else
#endif // !WIN32
        return true;
}  // end NormFile::Lock()

void NormFile::Unlock()
{
#ifndef WIN32
#ifdef HAVE_FLOCK
    flock(fd, LOCK_UN);
#else
#ifdef HAVE_LOCKF
    lockf(fd, F_ULOCK, 0);
#endif // HAVE_LOCKF
#endif // HAVE_FLOCK
    fchmod(fd, 0640);
#endif // !WIN32
}  // end NormFile::UnLock()

bool NormFile::Rename(const char* oldName, const char* newName)
{
    if (!strcmp(oldName, newName)) return true;  // no change required
    // Make sure the new file name isn't an existing "busy" file
    // (This also builds sub-directories as needed)
    if (NormFile::IsLocked(newName)) return false;    
#ifdef WIN32
    // In Win32, the new file can't already exist
	if (NormFile::Exists(newName) _unlink(newName);
    // In Win32, the old file can't be open
	int oldFlags = 0;
	if (IsOpen())
	{
		oldFlags = flags;
		oldFlags &= ~(O_CREAT | O_TRUNC);  // unset these
		Close();
	}  
#else
    // Create sub-directories as needed.
    char tempPath[PATH_MAX];
    strncpy(tempPath, newName, PATH_MAX);
    char* ptr = strrchr(tempPath, DIR_DELIMITER);
    if (ptr) *ptr = '\0';
    ptr = NULL;
    while (!NormFile::Exists(tempPath))
    {
        char* ptr2 = ptr;
        ptr = strrchr(tempPath, DIR_DELIMITER);
        if (ptr2) *ptr2 = DIR_DELIMITER;
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
    if (ptr && ('\0' == *ptr)) *ptr++ = DIR_DELIMITER;
    while (ptr)
    {
        ptr = strchr(ptr, DIR_DELIMITER);
        if (ptr) *ptr = '\0';
        if (mkdir(tempPath, 0755))
        {
            DMSG(0, "NormFile::Rename() mkdir(%s) error: %s\n",
                    tempPath, strerror(errno));
            return false;  
        }
        if (ptr) *ptr++ = DIR_DELIMITER;
    }   
#endif // if/else WIN32 
    if (rename(oldName, newName))
    {
        DMSG(0, "NormFile::Rename() rename() error: %s\n", strerror(errno));		
#ifdef WIN32
        //if (oldFlags) Open(oldName, oldFlags);
#endif // WIN32        
        return false;
        
    }
    else
    {
#ifdef WIN32
        // (TBD) Is the file offset OK doing this???
        //if (oldFlags) Open(newName, oldFlags);
#endif // WIN32
        return true;
    }
}  // end NormFile::Rename()

int NormFile::Read(char* buffer, int len)
{
    ASSERT(IsOpen());
#ifdef WIN32
    int result = _read(fd, buffer, len);
#else
    int result = read(fd, buffer, len);
#endif // if/else WIN32
    if (result > 0) offset += result;
    return result;
}  // end NormFile::Read()

int NormFile::Write(const char* buffer, int len)
{
    ASSERT(IsOpen());
#ifdef WIN32
    int result = _write(fd, buffer, len);
#else
    int result = write(fd, buffer, len);
#endif // if/else WIN32
    if (result > 0) offset += result;
    return result;
}  // end NormFile::Write()

bool NormFile::Seek(off_t theOffset)
{
    ASSERT(IsOpen());
#ifdef WIN32
    off_t result = _lseek(fd, theOffset, SEEK_SET);
#else
    off_t result = lseek(fd, theOffset, SEEK_SET);
#endif // if/else WIN32
    if (result == (off_t)-1)
    {
        DMSG(0, "NormFile::Seek() lseek() error: %s", strerror(errno));
        return false;
    }
    else
    {
        offset = result;
        return true; // no error
    }
}  // end NormFile::Seek()

off_t NormFile::GetSize() const
{
    ASSERT(IsOpen());
#ifdef WIN32
    struct _stat info;
    int result = _fstat(fd, &info);
#else
    struct stat info;
    int result = fstat(fd, &info);
#endif // if/else WIN32
    if (result)
    {
        DMSG(0, "Error getting file size: %s\n", strerror(errno));
        return 0;   
    }
    else
    {
        return info.st_size;
    }
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
    if (thePath && _access(thePath, 0))
#else
    if (thePath && access(thePath, X_OK))
#endif // if/else WIN32
    {
        DMSG(0, "NormDirectoryIterator: can't access directory: %s\n", thePath);
        return false;
    }
    current = new NormDirectory(thePath);
    if (current && current->Open())
    {
        path_len = strlen(current->Path());
        path_len = MIN(PATH_MAX, path_len);
        return true;
    }
    else
    {
        DMSG(0, "NormDirectoryIterator: can't open directory: %s\n", thePath);
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
		WIN32_findData findData;
		if (current->hSearch == (HANDLE)-1)
		{
			// Construct search string
			current->GetFullName(fileName);
			strcat(fileName, "\\*");
			if ((HANDLE)-1 == 
			    (current->hSearch = FindFirstFile(fileName, &findData)) )
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
			char* ptr = strrchr(findData.cFileName, DIR_DELIMITER);
			if (ptr)
				ptr += 1;
			else
				ptr = findData.cFileName;

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
                int nameLen = strlen(fileName);
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
            int nameLen = strlen(fileName);
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
    int len  = MIN(PATH_MAX, strlen(path));
    if ((len < PATH_MAX) && (DIR_DELIMITER != path[len-1]))
    {
        path[len++] = DIR_DELIMITER;
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
    // Get rid of trailing DIR_DELIMITER
    int len = MIN(PATH_MAX, strlen(fullName));
    if (DIR_DELIMITER == fullName[len-1]) fullName[len-1] = '\0';
#ifdef WIN32
    DWORD attr = GetFileAttributes(fullName);
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
    closedir(dptr);
    dptr = NULL;
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
    int len = MIN(PATH_MAX, strlen(ptr));
    strncat(ptr, path, PATH_MAX-len);
}  // end NormDirectoryIterator::NormDirectory::RecursiveCatName()

// Below are some static routines for getting file/directory information

// Is the named item a valid directory or file (or neither)??
NormFile::Type NormFile::GetType(const char* path)
{
#ifdef WIN32
    DWORD attr = GetFileAttributes(path);
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

off_t NormFile::GetSize(const char* path)
{
#ifdef WIN32
    struct _stat info;
    int result = _stat(path, &info);
#else
    struct stat info;
    int result = stat(path, &info);
#endif // if/else WIN32    
    if (result)
    {
        //DMSG(0, "Error getting file size: %s\n", strerror(errno));
        return 0;   
    }
    else
    {
        return info.st_size;
    }
}  // end NormFile::GetSize()

time_t NormFile::GetUpdateTime(const char* path)
{
#ifdef WIN32
    struct _stat info;
    int result = _stat(path, &info);
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
		time_t updateTime = MAX(info.st_ctime, info.st_atime);
		updateTime = MAX(updateTime, info.st_mtime);
		return updateTime;
#else
        return info.st_ctime; 
#endif // if/else WIN32
    } 
}  // end NormFile::GetUpdateTime()

bool NormFile::IsLocked(const char* path)
{
    // If file doesn't exist, it's not locked
    if (!Exists(path)) return false;      
    NormFile testFile;
#ifdef WIN32
    if(!testFile.Open(path, _O_WRONLY | _O_CREAT))
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
#ifdef WIN32
    else if (_unlink(path))
#else
    else if (unlink(path))
#endif // if/else WIN32
    {
        //DMSG(0, "NormFile::Unlink() unlink error: %s\n", strerror(errno));
        return false;
    }
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
                DMSG(0, "NormFileList::Append() Bad file/directory name: %s\n",
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
        DMSG(0, "NormFileList::Append() Error creating file/directory item: %s\n",
                strerror(errno));
        return false;
    }
}  // end NormFileList::Append()

bool NormFileList::Remove(const char* path)
{
    FileItem* nextItem = head;
    unsigned int pathLen = strlen(path);
    pathLen = MIN(pathLen, PATH_MAX);
    while (nextItem)
    {
        unsigned nameLen = strlen(nextItem->Path());
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
            unsigned int len = strlen(pathBuffer);
            len = MIN(len, PATH_MAX);
            if (DIR_DELIMITER != pathBuffer[len-1])
            {
                if (len < PATH_MAX) pathBuffer[len++] = DIR_DELIMITER;
                if (len < PATH_MAX) pathBuffer[len] = '\0';
            }   
        }
        else  // NormFile::NORMAL
        {
            const char* ptr = strrchr(next->Path(), DIR_DELIMITER);
            if (ptr++)
            {
                unsigned int len = ptr - next->Path();
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
    unsigned int len = strlen(thePath);
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
            DMSG(0, "NormFileList::DirectoryItem::GetNextFile() Directory iterator init error\n");
            return false;   
        } 
     }
     strncpy(thePath, path, PATH_MAX);
     unsigned int len = strlen(thePath);
     len = MIN(len, PATH_MAX);
     if ((DIR_DELIMITER != thePath[len-1]) && (len < PATH_MAX))
     {
         thePath[len++] = DIR_DELIMITER;
         if (len < PATH_MAX) thePath[len] = '\0';
     }  
     char tempPath[PATH_MAX];
     while (diterator.GetNextFile(tempPath))
     {
         unsigned int maxLen = PATH_MAX - len;
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
