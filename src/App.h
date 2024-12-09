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
	bool IsExitRequested();
	bool IsExitReady();

	bool HasInitError() const { return !mInitError.Empty(); }
	void SetInitError(StringView inText) { mInitError = inText; }

	template <class... taArgs>
	[[noreturn]] void               FatalError(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs) { FatalErrorV(StringView(inFmt.get().data(), (int)inFmt.get().size()), fmt::make_format_args(inArgs...)); }
	[[noreturn]] void               FatalErrorV(StringView inFmt, fmt::format_args inArgs = {});

	template <class... taArgs> void Log(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs) { LogV(StringView(inFmt.get().data(), (int)inFmt.get().size()), fmt::make_format_args(inArgs...)); }
	void                            LogV(StringView inFmt, fmt::format_args inArgs = {}, LogType inType = LogType::Normal);
	template <class... taArgs> void LogError(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs) { LogErrorV(StringView(inFmt.get().data(), (int)inFmt.get().size()), fmt::make_format_args(inArgs...)); }
	void                            LogErrorV(StringView inFmt, fmt::format_args inArgs = {}) { LogV(inFmt, inArgs, LogType::Error); }

	void                            OpenLogFile();
	void                            CloseLogFile();

	String                          mMainWindowTitle       = "Asset Cooker";
	void*                           mMainWindowHwnd        = nullptr;
	void*                           mNotifMenuHmenu        = nullptr;
	bool                            mMainWindowIsMinimized = false;
	bool                            mExitRequested         = false;
	bool                            mExitReady             = false;
	String                          mUserPrefsFilePath     = "prefs.toml";
	String                          mRuleFilePath          = "rules.toml";
	LogLevel                        mLogFSActivity         = LogLevel::Normal;
	struct Log                      mLog;
	FILE*                           mLogFile        = nullptr;
	String                          mLogDirectory   = "Logs";
	String                          mCacheDirectory = "Cache";
	String                          mInitError;

	bool                            mHideWindowOnMinimize       = true; // Hide the window when minimizing it.
	NotifEnabled                    mEnableNotifOnHideWindow    = NotifEnabled::Always; // Show a notification when the window is hidden saying that Asset Cooker is still running.
	NotifEnabled                    mEnableNotifOnCookingFinish = NotifEnabled::WhenMinimized; // Show a notification when cooking finishes.
	NotifEnabled                    mEnableNotifOnCookingError  = NotifEnabled::Always; // Show a notification when a cooking error occurs (even if cooking isn't finished yet).
	NotifEnabled                    mEnableNotifSound           = NotifEnabled::Always; // Play a sound when a notification is shown.

	OwnedHandle                     mSingleInstanceMutex; // Inter-process mutex making sure we can't run multiple instances of AssetCooker at the same time
};

inline App gApp;