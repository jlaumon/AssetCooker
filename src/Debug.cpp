#include "Debug.h"

#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/threads.h"

bool gIsDebuggerAttached()
{
	return IsDebuggerPresent();
}

// Get last error as a string.
TempString512 GetLastErrorString()
{
	DWORD error = GetLastError();
	TempString512 str;

	if (error != ERROR_SUCCESS)
	{
		// Note: We use capacity - 1 here to make sure there's always room for an extra null terminator.
		DWORD written_chars = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
										  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str.mBuffer, str.cCapacity - 1, nullptr);

		// Make sure there is a null terminator (FormatMessage does not always add one).
		if (written_chars > 0)
		{
			if (str.mBuffer[written_chars - 1] != 0)
			{
				// Add one.
				str.mBuffer[written_chars] = 0;
				str.mSize                  = written_chars;
			}
			else
			{
				// There's already one.
				str.mSize = written_chars - 1;
			}
		}

		// There's usually an EOL at the end, remove it.
		while (gEndsWith(str, "\n") || gEndsWith(str, "\r"))
		{
			str.mSize--;
			str.mBuffer[str.mSize] = 0;
		}
	}
	else
	{
		str.Append("Success.");
	}

	// Add the error code as an int.
	str.Append(TempString32(" (0x{:08x})", error));

	return str;
}


// Set the name of the current thread.
void gSetCurrentThreadName(const wchar_t* inName)
{
	SetThreadDescription(GetCurrentThread(), inName);
}