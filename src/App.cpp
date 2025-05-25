/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "App.h"

#include "ConfigReader.h"
#include "UserPreferencesReader.h"
#include "RuleReader.h"
#include "Debug.h"
#include "CookingSystem.h"
#include <Bedrock/Test.h>
#include <Bedrock/Mutex.h>
#include <Bedrock/StringFormat.h>
#include <Bedrock/String.h>
#include "UI.h"


#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/file.h"
#include "win32/window.h"
#include "win32/io.h"
#include "win32/threads.h"
#include "win32/process.h"
#include "win32/dbghelp.h"

#include <algorithm> // for std::sort
#include <stdarg.h>

#define SEM_FAILCRITICALERRORS (1)
#define SEM_NOGPFAULTERRORBOX  (2)
extern "C" UINT WINAPI SetErrorMode(UINT uMode);

extern "C" __declspec(dllimport) HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);

static LONG sExceptionHandler(_EXCEPTION_POINTERS* inExceptionInfo);

StringView gToStringView(LogLevel inVar)
{
	static constexpr StringView cStrings[]
	{
		"None",
		"Normal",
		"Verbose",
	};
	static_assert(gElemCount(cStrings) == gToUnderlying(LogLevel::_Count));

	return cStrings[(int)inVar];
}


StringView gToStringView(DumpMode inVar)
{
	static constexpr StringView cStrings[]
	{
		"Mini",
		"Full",
	};
	static_assert(gElemCount(cStrings) == gToUnderlying(DumpMode::_Count));

	return cStrings[(int)inVar];
}


StringView gToStringView(SaveDumpOnCrash inVar)
{
	static constexpr StringView cStrings[]
	{
		"No",
		"Ask",
		"Always",
	};
	static_assert(gElemCount(cStrings) == gToUnderlying(SaveDumpOnCrash::_Count));

	return cStrings[(int)inVar];
}


void App::Init()
{
	mLog.mAutoAddTime = true;
	gAppLog("Bonjour.");

	// Disable the default error message boxes (although hey seem to be disabled by default nowadays?)
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	// Add an exception handler to show our own error box, and potentially save a dump.
	SetUnhandledExceptionFilter(&sExceptionHandler);

	// The application includes a manifest that should make the default code page be UTF8 (if at least Windows 10 1903).
	// This in turn should mean that most ANSI win32 functions actually support UTF8.
	// TODO: use A version of windows functions wherever possible
	gAppLog("UTF8 is %s.", GetACP() == CP_UTF8 ? "supported. Noice" : "not supported");

	// Read the config file.
	gReadConfigFile(mConfigFilePath);

	// Read the user prefs file.
	gReadUserPreferencesFile(mUserPrefsFilePath);

	// Open the log file after reading the config since it can change the log directory.
	OpenLogFile();

	// Lock a system-wide mutex to prevent multiple instances of the app from running at the same time.
	// The name of the mutex is what matters, so make sure it's unique by including a UUID
	// and the configurable window title (to allow multiple instances if they are cooking different projects).
	mSingleInstanceMutex = CreateMutexA(nullptr, FALSE, gTempFormat("Asset Cooker eb835e40-e91e-4cfb-8e71-a68d3367bb7e %s", mMainWindowTitle.AsCStr()).AsCStr());
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		gAppFatalError("An instance of Asset Cooker is already running. Too many Cooks!");

	// Read the rule file.
	if (!HasInitError())
		gReadRuleFile(mRuleFilePath);

	// If all is good, start scanning files.
	if (!HasInitError())
		gFileSystem.StartMonitoring();
}


void App::Exit()
{
	gWriteUserPreferencesFile(mUserPrefsFilePath);

	// Remove exception handler.
	SetUnhandledExceptionFilter(nullptr);

	gAppLog("Au revoir.");
	CloseLogFile();
	mLog.Clear();

	mSingleInstanceMutex = {};
	mInitError			 = {};
}


void App::RequestExit()
{
	mExitRequested = true;

	// At the moment we're ready immediately, but in the future we'll need to save cooking state which might take a long time.
	mExitReady = true;
}

bool App::IsExitRequested() const
{
	return mExitRequested;
}

bool App::IsExitReady() const
{
	return mExitReady;
}

namespace Details
{
	// The va_list version is kept out of the header to avoid including stdarg.h in the header (or defining it manually) for now.
	void StringFormat(StringFormatCallback inAppendCallback, void* outString, const char* inFormat, va_list inArgs);
}


// Get the IDs of all the threads in the process.
TempVector<DWORD> gGetAllThreadIDs()
{
	OwnedHandle snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return {};

	TempVector<DWORD> thread_ids;
	DWORD             current_process = GetCurrentProcessId();
	THREADENTRY32     thread_entry    = { .dwSize = sizeof(thread_entry) };

	// Iterate all the threads in the system.
	if (Thread32First(snapshot, &thread_entry))
	{
		do
		{
			if (thread_entry.dwSize >= offsetof(THREADENTRY32, th32OwnerProcessID) + sizeof(thread_entry.th32OwnerProcessID))
			{
				// If it's the right process, add it to the list.
				if (thread_entry.th32OwnerProcessID == current_process)
				{
					thread_ids.PushBack(thread_entry.th32ThreadID);
				}
			}

			thread_entry.dwSize = sizeof(thread_entry);

		} while (Thread32Next(snapshot, &thread_entry));
	}

	return thread_ids;
}



void App::_FatalError(StringView inFormat, ...)
{
	// Make sure a single thread triggers the pop-up.
	static Mutex blocker;
	blocker.Lock();

	va_list args;
	va_start(args, inFormat);

	// Log the error first.
	_LogV(LogType::Error, inFormat, args);

	// Suspend all other threads.
	// This is to stop cooking/monitoring and also to avoid crashing when once we exit below.
	{
		TempVector<OwnedHandle> all_threads;
		DWORD                   current_thread_id = GetCurrentThreadId();
		for (DWORD thread_id : gGetAllThreadIDs())
		{
			if (thread_id == current_thread_id)
				continue; // Skip the current thread.

			OwnedHandle thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, thread_id);
			if (thread != nullptr)
				all_threads.PushBack(gMove(thread));
		}

		// Suspend the threads. This is asynchronous and can take a while.
		for (HANDLE thread : all_threads)
			SuspendThread(thread);

		// Try getting the context of each thread to force Windows to wait until the thread is actually suspended.
		for (HANDLE thread : all_threads)
		{
			CONTEXT context;
			GetThreadContext(thread, &context);
		}
	}

	if (gIsDebuggerAttached())
		BREAKPOINT;
	else
	{
		// Don't make popups when running tests.
		// TODO: also if running in command line mode/no UI mode
		if (!gIsRunningTest())
		{
			MessageBoxA(nullptr, 
				gTempFormatV(inFormat.AsCStr(), args).AsCStr(), 
				gTempFormat("%s - Fatal Error!", mMainWindowTitle.AsCStr()).AsCStr(), 
				MB_OK | MB_ICONERROR | MB_APPLMODAL);
		}
	}

	va_end(args);

	_Log(LogType::Error, "Fatal error, exiting now.");

	// Terminate the process now, don't try to cleanup anything.
	TerminateProcess(GetCurrentProcess(), 1);
}


void App::_Log(LogType inType, StringView inFormat, ...)
{
	va_list args;
	va_start(args, inFormat);

	_LogV(inType, inFormat, args);

	va_end(args);
}


void App::_LogV(LogType inType, StringView inFormat, va_list inArgs)
{
	StringView formatted_str = mLog.Add(inType, inFormat, inArgs);

	// If running tests, print to stdout (otherwise we don't care).
	// TODO: also if running in command line mode/no UI mode
	if (gIsRunningTest())
	{
		fwrite(formatted_str.Data(), 1, formatted_str.Size(), stdout);
	}

	// If there's a debugger attached, convert to wide char to write to the debug output.
	if (gIsDebuggerAttached())
	{
		wchar_t message_buffer[4096];
		auto wchar_message = gUtf8ToWideChar(formatted_str, message_buffer);
		gAssert(!wchar_message.empty()); // Buffer too small? Why would you log something that long?

		if (!wchar_message.empty())
		{
			// Write the message to the output.
			OutputDebugStringW(message_buffer);
		}
	}

	// Also log to a file.
	if (mLogFile)
	{
		fwrite(formatted_str.Data(), 1, formatted_str.Size(), mLogFile);

		// Flush immediately for now (it's probably not worse than OutputDebugString).
		fflush(mLogFile);
	}
}


void App::OpenLogFile()
{
	constexpr StringView log_file_prefix = "AssetCooker_";
	constexpr StringView log_file_ext    = ".log";

	// Make sure the log dir exists.
	CreateDirectoryA(mLogDirectory.AsCStr(), nullptr);

	// Build the log file name.
	LocalTime  current_time = gGetLocalTime();
	mLogFilePath = gTempFormat("%s\\%s%04u-%02u-%02u_%02u-%02u-%02u%s",
		mLogDirectory.AsCStr(), log_file_prefix.AsCStr(),
		current_time.mYear, current_time.mMonth, current_time.mDay,
		current_time.mHour, current_time.mMinute, current_time.mSecond,
		log_file_ext.AsCStr());

	// Open the log file.
	{
		// Do it inside the log lock because we want to dump the current log into the file before more is added.
		LockGuard lock(mLog.mMutex);

		mLogFile = fopen(mLogFilePath.AsCStr(), "wt");
		
		if (mLogFile)
		{
			// If there were logs already, write them to the file now.
			for (auto& line : mLog.mLines)
				fwrite(line.mData, 1, line.mSize, mLogFile);

			fflush(mLogFile);
		}
	}

	if (mLogFile == nullptr)
		gAppLogError(R"(Failed to open log file "%s" - %s)", mLogFilePath.AsCStr(), GetLastErrorString().AsCStr());

	// Clean up old log files.
	{
		// Max number of log files to keep, delete the oldest ones if we have more.
		constexpr int max_log_files   = 5;

		// List all the log files.
		TempVector<String> log_files;
		{
			WIN32_FIND_DATAA find_file_data;
			HANDLE find_handle = FindFirstFileA(gTempFormat("%s\\*", mLogDirectory.AsCStr()).AsCStr(), &find_file_data);
			if (find_handle != INVALID_HANDLE_VALUE)
			{
				do
				{
					// Ignore directories.
					if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						continue;

					// If it looks like a log file, add it to the list.
					if (gStartsWith(find_file_data.cFileName, log_file_prefix) &&
						gEndsWith(find_file_data.cFileName, log_file_ext))
					{
						log_files.PushBack(find_file_data.cFileName);
					}
					
				} while (FindNextFileA(find_handle, &find_file_data) != 0);
			}
			FindClose(find_handle);
		}
		
		if (log_files.Size() > max_log_files)
		{
			// Sort to make sure the oldest ones are first (because the date is in the name).
			std::sort(log_files.begin(), log_files.end());

			// Delete the files.
			int num_to_delete = log_files.Size() - max_log_files;
			for (int i = 0; i < num_to_delete; ++i)
			{
				TempString path = gTempFormat("%s\\%s", mLogDirectory.AsCStr(), log_files[i].AsCStr());
				DeleteFileA(path.AsCStr());
			}
		}
	}
}


void App::CloseLogFile()
{
	if (mLogFile == nullptr)
		return;

	fclose(mLogFile);
	mLogFile = nullptr;
}



StringView gExceptionCodeToStringView(DWORD inExceptionCode)
{
	switch (inExceptionCode)
	{
		case EXCEPTION_ACCESS_VIOLATION			: return "ACCESS_VIOLATION"			;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED	: return "ARRAY_BOUNDS_EXCEEDED"	;
		case EXCEPTION_BREAKPOINT				: return "BREAKPOINT"				;
		case EXCEPTION_DATATYPE_MISALIGNMENT	: return "DATATYPE_MISALIGNMENT"	;
		case EXCEPTION_FLT_DENORMAL_OPERAND		: return "FLT_DENORMAL_OPERAND"		;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO		: return "FLT_DIVIDE_BY_ZERO"		;
		case EXCEPTION_FLT_INEXACT_RESULT		: return "FLT_INEXACT_RESULT"		;
		case EXCEPTION_FLT_INVALID_OPERATION	: return "FLT_INVALID_OPERATION"	;
		case EXCEPTION_FLT_OVERFLOW				: return "FLT_OVERFLOW"				;
		case EXCEPTION_FLT_STACK_CHECK			: return "FLT_STACK_CHECK"			;
		case EXCEPTION_FLT_UNDERFLOW			: return "FLT_UNDERFLOW"			;
		case EXCEPTION_ILLEGAL_INSTRUCTION		: return "ILLEGAL_INSTRUCTION"		;
		case EXCEPTION_IN_PAGE_ERROR			: return "IN_PAGE_ERROR"			;
		case EXCEPTION_INT_DIVIDE_BY_ZERO		: return "INT_DIVIDE_BY_ZERO"		;
		case EXCEPTION_INT_OVERFLOW				: return "INT_OVERFLOW"				;
		case EXCEPTION_INVALID_DISPOSITION		: return "INVALID_DISPOSITION"		;
		case EXCEPTION_NONCONTINUABLE_EXCEPTION	: return "NONCONTINUABLE_EXCEPTION"	;
		case EXCEPTION_PRIV_INSTRUCTION			: return "PRIV_INSTRUCTION"			;
		case EXCEPTION_SINGLE_STEP				: return "SINGLE_STEP"				;
		case EXCEPTION_STACK_OVERFLOW			: return "STACK_OVERFLOW"			;
		default:								  return "";
	}
}

LONG sExceptionHandler(_EXCEPTION_POINTERS* inExceptionInfo)
{
	gAppLogError("Crash!");

	constexpr StringView dump_file_prefix = "AssetCooker_";
	constexpr StringView dump_file_ext    = ".dmp";
	LocalTime            current_time     = gGetLocalTime();

	// Build a path for the dump file.
	// Note: do this first because the error message also uses temp memory and we need to resize it.
	TempString dump_path = gTempFormat("%s\\%s%04u-%02u-%02u_%02u-%02u-%02u%s",
		gApp.mLogDirectory.AsCStr(), dump_file_prefix.AsCStr(),
		current_time.mYear, current_time.mMonth, current_time.mDay,
		current_time.mHour, current_time.mMinute, current_time.mSecond,
		dump_file_ext.AsCStr());

	// Note: inExceptionInfo should never be null, but testing it makes it easier to test this code.
	DWORD exception_code = inExceptionInfo ? inExceptionInfo->ExceptionRecord->ExceptionCode : 0;

	TempString error_message = gTempFormat("Exception %s (0x%08X)", 
		gExceptionCodeToStringView(exception_code).AsCStr(), 
		exception_code);

	gApp._Log(LogType::Error, error_message);

	if (exception_code == EXCEPTION_ACCESS_VIOLATION || exception_code == EXCEPTION_IN_PAGE_ERROR)
	{
		const int   action = (int)inExceptionInfo->ExceptionRecord->ExceptionInformation[0];
		const char* action_str;
		switch (action)
		{
		case 0:		action_str = "read";	break;
		case 1:		action_str = "write";	break;
		case 8:		action_str = "execute"; break;
		default:	action_str = "???";		break;
		}

		error_message.Append("\n");
		int prev_size = error_message.Size();
		gAppendFormat(error_message, "Trying to %s 0x%016llX.", action_str, inExceptionInfo->ExceptionRecord->ExceptionInformation[1]);

		gApp._Log(LogType::Error, StringView(error_message.SubStr(prev_size)));
	}

	bool dump_saved = false;

	// If we always want a dump, write it now.
	if (gApp.mSaveDumpOnCrash == SaveDumpOnCrash::Always)
	{
		dump_saved = gWriteDump(dump_path, GetCurrentThreadId(), inExceptionInfo);
		if (dump_saved)
			gAppendFormat(error_message, "\n\nDump saved as \"%s\".", dump_path.AsCStr());
		else
			gAppendFormat(error_message, "\n\nFailed to save dump. Check log.");
	}

	// If we need to ask, make a yes/no popup.
	if (gApp.mSaveDumpOnCrash == SaveDumpOnCrash::Ask)
	{
		error_message += "\n\nDo you want to save a crash dump?";

		int result = MessageBoxA(nullptr, 
			error_message.AsCStr(),
			(const char*)u8"Saperlipopette! Asset Cooker Crashed!", 
			MB_YESNO | MB_ICONERROR | MB_APPLMODAL);

		if (result == IDYES)
			dump_saved = gWriteDump(dump_path, GetCurrentThreadId(), inExceptionInfo);
	}
	else
	{
		// Otherwise an ok popup is enough.
		MessageBoxA(nullptr, 
			error_message.AsCStr(),
			(const char*)u8"Saperlipopette! Asset Cooker Crashed!", 
			MB_OK | MB_ICONERROR | MB_APPLMODAL);
	}

	// If a dump was saved, open the parent dir with the file selected.
	if (dump_saved)
	{
		TempString command = gTempFormat("/select, %s", dump_path.AsCStr());
		ShellExecuteA(nullptr, nullptr, "explorer", command.AsCStr(), nullptr, SW_SHOWDEFAULT);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}
