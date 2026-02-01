/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include "FileUtils.h"
#include "Log.h"
#include "Notifications.h"

#include <Bedrock/String.h>
#include <Bedrock/Event.h>

enum class LogLevel : uint8
{
	None,
	Normal,
	Verbose,
	_Count,
};

StringView gToStringView(LogLevel inVar);


enum class DumpMode : int8
{
	Mini,
	Full,
	_Count,
};

StringView gToStringView(DumpMode inVar);


enum class SaveDumpOnCrash : int8
{
	No,
	Ask,
	Always,
	_Count,
};

StringView gToStringView(SaveDumpOnCrash inVar);


// Manages the initialization and life time of the application.
struct App
{
	void Init();
	void Exit();

	void RequestExit();
	bool IsExitRequested() const;

	bool HasInitError() const { return !mInitError.Empty(); }
	void SetInitError(StringView inText) { mInitError = inText; }

	// Use the macros gAppLog/gAppFatalError to get format validation instead of calling the methods directly.
	void                            _Log(LogType inType, StringView inFormat, ...);
	void                            _LogV(LogType inType, StringView inFormat, va_list inArgs);
	[[noreturn]] void               _FatalError(StringView inFormat, ...);

	void                            OpenLogFile();
	void                            CloseLogFile();

	String							mMainWindowTitle	   = "Asset Cooker";
	void*							mMainWindowHwnd		   = nullptr;
	void*							mNotifMenuHmenu		   = nullptr;
	bool							mMainWindowIsMinimized = false;
	bool							mNoUI				   = false;					 // If true, run without UI and automatically exit when cooking is finished.
	Event							mExitRequestedEvent	   = { Event::ManualReset }; // Event that gets set when exit is requested.
	String							mConfigFilePath		   = "config.toml";
	String							mUserPrefsFilePath	   = "prefs.toml";
	String							mRuleFilePath		   = "rules.toml";
	LogLevel						mLogFSActivity		   = LogLevel::Normal;
	struct Log						mLog;
	FILE*							mLogFile = nullptr;
	String							mLogFilePath;
	String							mLogDirectory	= "Logs";
	String							mCacheDirectory = "Cache";
	String							mInitError;

	bool                            mHideWindowOnMinimize       = true; // Hide the window when minimizing it.
	bool                            mStartMinimized             = false; // Start with the window minimized (or hidden if mHideWindowOnMinimize is set).
	NotifEnabled                    mEnableNotifOnHideWindow    = NotifEnabled::Always; // Show a notification when the window is hidden saying that Asset Cooker is still running.
	NotifEnabled                    mEnableNotifOnCookingFinish = NotifEnabled::WhenMinimized; // Show a notification when cooking finishes.
	NotifEnabled                    mEnableNotifOnCookingError  = NotifEnabled::Always; // Show a notification when a cooking error occurs (even if cooking isn't finished yet).
	NotifEnabled                    mEnableNotifSound           = NotifEnabled::Always; // Play a sound when a notification is shown.
	DumpMode                        mDumpMode                   = DumpMode::Mini;
	SaveDumpOnCrash                 mSaveDumpOnCrash            = SaveDumpOnCrash::Ask;

	OwnedHandle                     mSingleInstanceMutex; // Inter-process mutex making sure we can't run multiple instances of AssetCooker at the same time
};

inline App gApp;

#define gAppLog(format, ...)                            \
	do                                                  \
	{                                                   \
		(void)sizeof(printf(format, __VA_ARGS__));      \
		gApp._Log(LogType::Normal, format, __VA_ARGS__); \
	} while (false)

#define gAppLogError(format, ...)                      \
	do                                                 \
	{                                                  \
		(void)sizeof(printf(format, __VA_ARGS__));     \
		gApp._Log(LogType::Error, format, __VA_ARGS__); \
	} while (false)

#define gAppFatalError(format, ...)                \
	do                                             \
	{                                              \
		(void)sizeof(printf(format, __VA_ARGS__)); \
		gApp._FatalError(format, __VA_ARGS__);      \
	} while (false)


// Build a unique name to use with shared Win32 objects (Events, Mutex, etc.)
// Note: This is duplicated in asset_cooker_api.c to be able to open shared objects from another process.
TempString gGetAssetCookerIdentifier(StringView inConfigFilePath);