 

#include <windows.h>
#include <stdio.h>	 // for perror()
#include <string.h>  // for memset()
#include <process.h>  // for _beginthreadex(), _endthreadex()

// This class creates a child thread to read STDIN into a buffer.
// An event is set that can be monitored by the parent to know
// when input is available.  Mutexes are used to regulate the
// thread's reading (i.e., flow control).
class Win32InputHandler
{
	public:
		Win32InputHandler();
		~Win32InputHandler();

		bool Open(int buflen = 4096);
		void Close();

		// The event handle returned here can be used in
		// a call like WaitForSignalObject() to get notified
		// when there is data to be read.
		HANDLE GetEventHandle() const
			{return input_event;}

		// This does not block and the input_event
		// handle is a cue for when to call it
		// Returns number of bytes copied into "buffer"
		// (-1 is returned upon input error (input closed))
		int ReadData(char* buffer, int numBytes);

	private:
		static unsigned int __stdcall DoThreadOpen(void* param);
		unsigned int Run();

		HANDLE				input_handle;
		HANDLE				input_event;
		HANDLE				thread_handle;
		bool				input_ready;
		char*				input_buffer;
		int					input_buflen;
		int					input_index;
		int					input_outdex;
		CONDITION_VARIABLE	buffer_vacancy;
		CRITICAL_SECTION	buffer_lock;	
		bool				is_running;
		int					input_length;
		bool				input_error;
		
};  // end class Win32InputHandler

Win32InputHandler::Win32InputHandler()
	: input_handle(NULL), input_event(NULL), thread_handle(NULL),
	  input_ready(false), input_buffer(NULL), input_buflen(0),
	  input_index(0), input_outdex(0), is_running(false),
	  input_length(0), input_error(false)
{

}

Win32InputHandler::~Win32InputHandler()
{
	if (is_running) Close();
}

bool Win32InputHandler::Open(int buflen)
{
	if (is_running) Close();
	if (NULL != input_buffer) 
    {
        input_buffer = NULL;
        delete[] input_buffer;
    }
	input_buflen = input_index = input_outdex = input_length = 0;
	input_error = false;
	if (NULL != input_event) CloseHandle(input_event);

	// Get the STDIN handle whatever it may be
	input_handle = GetStdHandle(STD_INPUT_HANDLE);
	/*if (FILE_TYPE_CHAR == GetFileType(input_handle))
	{
		// The STDIN is a console
		DWORD consoleMode;
		bool result = GetConsoleMode(input_handle, &consoleMode);
		consoleMode &= ~(ENABLE_LINE_INPUT | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT);
		SetConsoleMode(input_handle, consoleMode);
	}*/

	// Create an event handle, non-signaled initially
	input_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == input_event)
	{
		perror("Win32InputHandler::Open() CreateEvent() error");
		return false;
	}
	if (NULL == (input_buffer = new char[buflen]))
	{
		perror("Win32InputHandler::Open() new input_buffer error");
		CloseHandle(input_event);
		input_event = NULL;
		return false;
	}
	input_buflen = buflen;
	InitializeConditionVariable(&buffer_vacancy);
	InitializeCriticalSection(&buffer_lock);
	is_running = true;
	if (0 == (thread_handle = (HANDLE)_beginthreadex(NULL, 0, DoThreadOpen, this, 0, NULL)))
	{
		perror("Win32InputHandler::Open() _beginthreadex() error");
		DeleteCriticalSection(&buffer_lock);
		delete[] input_buffer;
		input_buffer = NULL;
		CloseHandle(input_event);
		input_event = NULL;
		return false;
	}
	return true;
}  // end Win32InputHandler::Open()

void  Win32InputHandler::Close()
{
	EnterCriticalSection(&buffer_lock);
	is_running = false;
	WakeConditionVariable(&buffer_vacancy);  // in case it was full
	LeaveCriticalSection(&buffer_lock);
	WaitForSingleObject(thread_handle, INFINITE);
	DeleteCriticalSection(&buffer_lock);
	delete[] input_buffer;
	input_buffer = NULL;
	CloseHandle(input_event);
	input_event = NULL;
}  // end Win32InputHandler::Close()

unsigned int __stdcall Win32InputHandler::DoThreadOpen(void* param)
{
	Win32InputHandler* inputHandler = reinterpret_cast<Win32InputHandler*>(param);
	unsigned int exitStatus = inputHandler->Run();
	_endthreadex(exitStatus);
	return exitStatus;
}  // end  Win32InputHandler::DoThreadOpen()

unsigned int Win32InputHandler::Run()
{
	// This loop is for the child thread to read input one byte at a time
	// (Since consoles/anonymous pipes don't support async i/o)
	// The input bytes are copied to a circularly-managed "input_buffer"
	// that can be accessed via the "ReadData()" method by the parent
	while (is_running)
	{
		// Read input one byte at a time and buffer
		DWORD dwRead = 0;
		BOOL result = ReadFile(input_handle, input_buffer+input_index, 1, &dwRead, NULL);
		EnterCriticalSection(&buffer_lock);
		if (result)
		{
			if (0 != dwRead)
			{
				input_length++;
				input_index++;
				if (input_index >= input_buflen)
					input_index = 0;
			}
			else
			{
				LeaveCriticalSection(&buffer_lock);
				continue;
			}
		}
		else
		{
			// What do we do on error?!
			input_error = true;
			is_running = false;
		}
		// Signals the parent of data ready (or error)
		if (!input_ready)
		{
			SetEvent(input_event);
			input_ready = true;
		}
		// If the buffer has been filled, this blocks this child
		// loop until buffer space is available or it is stopped.
		while ((input_index == input_outdex) && (0 != input_length) && is_running)
			SleepConditionVariableCS(&buffer_vacancy, &buffer_lock, INFINITE);
		LeaveCriticalSection(&buffer_lock);
	}  // end while(is_running)
	return (input_error ? 1 : 0);
}  // end Win32InputHandler::Run()

// Called by parent to gobble up data (non-blocking read)
// Returns number of bytes read or -1 on error (e.g., STDIN pipe closed)
int Win32InputHandler::ReadData(char* buffer, int numBytes)
{
	EnterCriticalSection(&buffer_lock); 
	if ((0 != input_length) && (0 != numBytes))
	{
		if (numBytes > input_length)
			numBytes = input_length;
		input_length -= numBytes;
		if (0 == input_length)
		{
			// On error condition, the event is left triggered
			// after the input_buffer is emptied so parent gets
			// all data buffered before the error (e.g., eof) 
			// occurred.
			if (input_ready && !input_error)
			{
				input_ready = false;
				ResetEvent(input_event);
			}
		}
		// Copy "numBytes" of available data to caller's "buffer"
		// (two-step process when input_buffer wraps)
		unsigned int offset = input_outdex;
		input_outdex += numBytes;
		if (input_outdex >= input_buflen)
		{
			input_outdex -= input_buflen;
			unsigned int count = numBytes - input_outdex;
			memcpy(buffer, input_buffer+offset, count);
			if (0 != input_outdex)
				memcpy(buffer+count, input_buffer, input_outdex);
		}
		else
		{
			memcpy(buffer, input_buffer + offset, numBytes);
		}
	}
	else
	{
		// Doing the error check
		if (input_error && (0 != numBytes))
			numBytes = -1;  // to indicate an error has occurred and child is dead
		else
			numBytes = 0;
	}
	// This wakes up the child if it is sleeping because the buffer was full
	if (numBytes > 0)
		WakeConditionVariable(&buffer_vacancy);
	LeaveCriticalSection(&buffer_lock);
	return numBytes;
}  // end Win32InputHandler::ReadData()
