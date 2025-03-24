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

enum class LogLevel : uint8
{
	None,
	Normal,
	Verbose,
	_Count,
};


constexpr StringView gToStringView(LogLevel inVar)
{
	constexpr StringView cStrings[]
	{
		"None",
		"Normal",
		"Verbose",
	};
	static_assert(gElemCount(cStrings) == (size_t)LogLevel::_Count);

	return cStrings[(int)inVar];
};


// Manages the initialization and life time of the application.
struct App
{
	void Init();
	void Exit();

	void RequestExit();
	bool IsExitRequested() const;
	bool IsExitReady() const;

	bool HasInitError() const { return !mInitError.Empty(); }
	void SetInitError(StringView inText) { mInitError = inText; }

	// Use the macros gAppLog/gAppFatalError to get format validation instead of calling the methods directly.
	void                            _Log(LogType inType, StringView inFormat, ...);
	void                            _LogV(LogType inType, StringView inFormat, va_list inArgs);
	[[noreturn]] void               _FatalError(StringView inFormat, ...);

	void                            OpenLogFile();
	void                            CloseLogFile();

	static StringView GetRedirectorPath();
	static void SetRedirectorPath(StringView path);

	String                          mMainWindowTitle       = "Asset Cooker";
	void*                           mMainWindowHwnd        = nullptr;
	void*                           mNotifMenuHmenu        = nullptr;
	bool                            mMainWindowIsMinimized = false;
	bool                            mExitRequested         = false;
	bool                            mExitReady             = false;
	String                          mConfigFilePath        = "config.toml";
	String                          mUserPrefsFilePath     = "prefs.toml";
	String                          mRuleFilePath          = "rules.toml";
	LogLevel                        mLogFSActivity         = LogLevel::Normal;
	struct Log                      mLog;
	FILE*                           mLogFile        = nullptr;
	String                          mLogDirectory   = "Logs";
	String                          mCacheDirectory = "Cache";
	String                          mInitError;

	bool                            mHideWindowOnMinimize       = true; // Hide the window when minimizing it.
	bool                            mStartMinimized             = false; // Start with the window minimized (or hidden if mHideWindowOnMinimize is set).
	NotifEnabled                    mEnableNotifOnHideWindow    = NotifEnabled::Always; // Show a notification when the window is hidden saying that Asset Cooker is still running.
	NotifEnabled                    mEnableNotifOnCookingFinish = NotifEnabled::WhenMinimized; // Show a notification when cooking finishes.
	NotifEnabled                    mEnableNotifOnCookingError  = NotifEnabled::Always; // Show a notification when a cooking error occurs (even if cooking isn't finished yet).
	NotifEnabled                    mEnableNotifSound           = NotifEnabled::Always; // Play a sound when a notification is shown.

	OwnedHandle                     mSingleInstanceMutex; // Inter-process mutex making sure we can't run multiple instances of AssetCooker at the same time

private:
	static StringView mRedirectorPath;
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