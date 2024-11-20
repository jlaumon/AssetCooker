/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Debug.h"

#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/threads.h"


// Get last error as a string.
FixedString512 GetLastErrorString()
{
	DWORD error = GetLastError();
	FixedString512 str;

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
	str.Append(FixedString32(" (0x{:08x})", error));

	return str;
}


// Set the name of the current thread.
void gSetCurrentThreadName(const wchar_t* inName)
{
	SetThreadDescription(GetCurrentThread(), inName);
}