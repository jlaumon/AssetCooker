/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Debug.h"

#include <Bedrock/StringFormat.h>
#include <App.h>

#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/threads.h"
#include "win32/file.h"
#include "win32/dbghelp.h"
#include "win32/process.h"


// Linking Dbghelp is required for MiniDumpWriteDump
#pragma comment(lib, "Dbghelp.lib")


// Get last error as a string.
TempString GetLastErrorString()
{
	DWORD error = GetLastError();
	TempString str;

	if (error != ERROR_SUCCESS)
	{
		str.Reserve(1024);

		// Note: FormatMessageA always writes the null terminator, and write nothing if there's not enough room for it.
		// The value returned is the number of char written excluding the null terminator(!).
		DWORD string_size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
										  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str.Data(), str.Capacity(), nullptr);

		str.Resize(string_size);

		// There's usually an EOL at the end, remove it.
		while (str.EndsWith("\n") || str.EndsWith("\r"))
		{
			str.RemoveSuffix(1);
		}
	}
	else
	{
		str.Append("Success.");
	}

	// Add the error code as an int.
	gAppendFormat(str, " (0x%08x)", error);

	str.ShrinkToFit();
	return str;
}



bool gWriteDump(StringView inPath, uint32 inCrashingThreadID, _EXCEPTION_POINTERS* inExceptionInfo)
{
	OwnedHandle dump_file = CreateFileA(inPath.AsCStr(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!dump_file.IsValid())
	{
		gAppLogError("Failed to create dump file %s - %s", inPath.AsCStr(), GetLastErrorString().AsCStr());
		return false;
	}

	DWORD       process_id = GetCurrentProcessId();
	OwnedHandle process    = GetCurrentProcess();

	_MINIDUMP_EXCEPTION_INFORMATION minidump_info = 
	{
		.ThreadId = inCrashingThreadID,
		.ExceptionPointers = inExceptionInfo,
		.ClientPointers = TRUE
	};

	_MINIDUMP_TYPE dump_type = MiniDumpNormal;

	if (gApp.mDumpMode == DumpMode::Full)
		dump_type = MiniDumpWithFullMemory;

	if (!MiniDumpWriteDump(process, process_id, dump_file, dump_type, inExceptionInfo ? &minidump_info : nullptr, nullptr, nullptr))
	{
		gAppLogError("MiniDumpWriteDump failed - %s", GetLastErrorString().AsCStr());
		return false;
	}

	gAppLog("Sucessfully saved crash dump: %s", inPath.AsCStr());

	return true;
}


