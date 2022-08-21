 

#include "win32InputHandler.cpp"  // for class Win32InputHandler

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 4096

int main(int argc, char** argv)
{
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	if (argc > 1)
	{
		// Generate random output
		const char* text = "This is some text to output\n";
		int textlen = strlen(text);

		while (true)
		{
			int count = rand() % 100;
		    while (count > 0)
			{
				printf(text);
				DWORD dwWritten;
				BOOL fSuccess = WriteFile(hStdout, text, textlen, &dwWritten, NULL);
				count -= textlen;
			}

			int delay = (rand() % 1000);
			Sleep(1000);

		}

		return 0;
	}

	Win32InputHandler inputHandler;

	inputHandler.Open();

	while (true)
	{
		char buffer[BUFSIZE];
		WaitForSingleObject(inputHandler.GetEventHandle(), INFINITE);
		int numBytes = inputHandler.ReadData(buffer, 1024);
		if (numBytes < 0) break;

		DWORD dwWritten;
		BOOL fSuccess = WriteFile(hStdout, buffer, numBytes, &dwWritten, NULL);
	}
	inputHandler.Close();

    return 0;
}  //  end main()
