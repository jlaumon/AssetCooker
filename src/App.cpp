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

#include <algorithm> // for std::sort
#include <stdarg.h>

void App::Init()
{
	mLog.mAutoAddTime = true;
	gAppLog("Bonjour.");

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

	gAppLog("Au revoir.");
	CloseLogFile();
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
	TempString new_log_file = gTempFormat("%s\\%s%04u-%02u-%02u_%02u-%02u-%02u%s",
		mLogDirectory.AsCStr(), log_file_prefix.AsCStr(),
		current_time.mYear, current_time.mMonth, current_time.mDay,
		current_time.mHour, current_time.mMinute, current_time.mSecond,
		log_file_ext.AsCStr());

	// Open the log file.
	{
		// Do it inside the log lock because we want to dump the current log into the file before more is added.
		LockGuard lock(mLog.mMutex);

		mLogFile = fopen(new_log_file.AsCStr(), "wt");
		
		if (mLogFile)
		{
			// If there were logs already, write them to the file now.
			for (auto& line : mLog.mLines)
				fwrite(line.mData, 1, line.mSize, mLogFile);

			fflush(mLogFile);
		}
	}

	if (mLogFile == nullptr)
		gAppLogError(R"(Failed to open log file "%s" - %s)", new_log_file.AsCStr(), GetLastErrorString().AsCStr());

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