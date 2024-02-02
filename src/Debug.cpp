#include "Debug.h"

#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/threads.h"

bool gIsDebuggerAttached()
{
	return IsDebuggerPresent();
}

// Get last error as a string.
String GetLastErrorString()
{
	DWORD error = GetLastError();

	if (error == ERROR_SUCCESS)
		return {};

	// TODO: there is usually a \r\n at the end, remove it!
	char  buffer[512];
	DWORD buffer_len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
									  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer) - 1, nullptr);
	// Make sure there is a null terminator (FormatMessage does not always add one).
	buffer[buffer_len] = 0;
	return buffer;
}


// Set the name of the current thread.
void gSetCurrentThreadName(const wchar_t* inName)
{
	SetThreadDescription(GetCurrentThread(), inName);
}