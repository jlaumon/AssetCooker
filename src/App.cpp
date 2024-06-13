#include "App.h"

#include "ConfigReader.h"
#include "UserPreferencesReader.h"
#include "RuleReader.h"
#include "Debug.h"
#include "CookingSystem.h"
#include "UI.h"

#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/file.h"
#include "win32/window.h"
#include "win32/io.h"
#include "win32/threads.h"

#include <algorithm> // for std::sort

void App::Init()
{
	mLog.mAutoAddTime = true;
	Log("Bonjour.");

	// The application includes a manifest that should make the default code page be UTF8 (if at least Windows 10 1903).
	// This in turn should mean that most ANSI win32 functions actually support UTF8.
	// TODO: use A version of windows functions wherever possible
	Log("UTF8 is {}.", GetACP() == CP_UTF8 ? "supported. Noice" : "not supported");

	// Read the config file.
	gReadConfigFile("config.toml");

	// Read the user prefs file.
	gReadUserPreferencesFile(mUserPrefsFilePath);

	// Open the log file after reading the config since it can change the log directory.
	OpenLogFile();

	// Lock a system-wide mutex to prevent multiple instances of the app from running at the same time.
	// The name of the mutex is what matters, so make sure it's unique by including a UUID
	// and the configurable window title (to allow multiple instances if they are cooking different projects).
	mSingleInstanceMutex = CreateMutexA(nullptr, FALSE, TempString256("Asset Cooker eb835e40-e91e-4cfb-8e71-a68d3367bb7e {}", mMainWindowTitle).AsCStr());
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		FatalError("An instance of Asset Cooker is already running. Too many Cooks!");

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

	Log("Au revoir.");
	CloseLogFile();
}


void App::RequestExit()
{
	mExitRequested = true;

	// At the moment we're ready immediately, but in the future we'll need to save cooking state which might take a long time.
	mExitReady = true;
}

bool App::IsExitRequested()
{
	return mExitRequested;
}

bool App::IsExitReady()
{
	return mExitReady;
}


void App::FatalErrorV(StringView inFmt, fmt::format_args inArgs)
{
	// Make sure a single thread triggers the pop-up.
	static std::mutex blocker;
	blocker.lock();

	// Log the error first.
	LogErrorV(inFmt, inArgs);

	if (gIsDebuggerAttached())
		breakpoint;
	else
		MessageBoxA(nullptr, TempString512(inFmt, inArgs).AsCStr(), TempString512("{} - Fatal Error!", mMainWindowTitle).AsCStr(), MB_OK | MB_ICONERROR | MB_APPLMODAL);

	LogError("Fatal error, exiting now.");
	quick_exit(1);
}


void App::LogV(StringView inFmt, fmt::format_args inArgs, LogType inType)
{
	// Add to the in-memory log.
	StringView formatted_str = mLog.Add(inType, inFmt, inArgs);

	// Convert to wide char to write to the debug output.
	wchar_t message_buffer[4096];
	auto wchar_message = gUtf8ToWideChar(formatted_str, message_buffer);
	gAssert(wchar_message); // Buffer too small? Why would you log something that long?

	if (wchar_message)
	{
		// Write the message to the output.
		OutputDebugStringW(message_buffer);
	}

	// Also log to a file.
	if (mLogFile)
	{
		fwrite(formatted_str.data(), 1, formatted_str.size(), mLogFile);

		// Flush immediately for now (it's probably not worse than OutputDebugString).
		fflush(mLogFile);
	}
}


void App::OpenLogFile()
{
	constexpr StringView log_file_prefix = "AssetCooker_";
	constexpr StringView log_file_ext    = ".log";

	// Make sure the log dir exists.
	CreateDirectoryA(mLogDirectory.c_str(), nullptr);

	// Build the log file name.
	LocalTime     current_time = gGetLocalTime();
	TempString256 new_log_file("{}\\{}{:04}-{:02}-{:02}_{:02}-{:02}-{:02}{}",
		mLogDirectory, log_file_prefix,
		current_time.mYear, current_time.mMonth, current_time.mDay,
		current_time.mHour, current_time.mMinute, current_time.mSecond,
		log_file_ext);

	// Open the log file.
	{
		// Do it inside the log lock because we want to dump the current log into the file before more is added.
		std::lock_guard lock(mLog.mMutex);

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
		LogError(R"(Failed to open log file "{}" - {})", new_log_file, GetLastErrorString());

	// Clean up old log files.
	{
		// Max number of log files to keep, delete the oldest ones if we have more.
		constexpr int        max_log_files   = 5;

		// List all the log files.
		std::vector<String> log_files;
		{
			WIN32_FIND_DATAA find_file_data;
			HANDLE find_handle = FindFirstFileA(TempString256("{}\\*", mLogDirectory).AsCStr(), &find_file_data);
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
						log_files.push_back(find_file_data.cFileName);
					}
					
				} while (FindNextFileA(find_handle, &find_file_data) != 0);
			}
			FindClose(find_handle);
		}
		
		if (log_files.size() > max_log_files)
		{
			// Sort to make sure the oldest ones are first (because the date is in the name).
			std::sort(log_files.begin(), log_files.end());

			// Delete the files.
			int num_to_delete = (int)log_files.size() - max_log_files;
			for (int i = 0; i < num_to_delete; ++i)
			{
				TempString256 path("{}\\{}", mLogDirectory, log_files[i]);
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