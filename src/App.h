#pragma once

#include "Core.h"
#include "Log.h"

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

	bool HasInitError() const { return !mInitError.empty(); }
	void SetInitError(StringView inText) { mInitError = inText; }

	template <class... taArgs>
	[[noreturn]] void               FatalError(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs) { FatalErrorV(inFmt.get(), fmt::make_format_args(inArgs...)); }
	[[noreturn]] void               FatalErrorV(StringView inFmt, fmt::format_args inArgs = {});

	template <class... taArgs> void Log(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs) { LogV(inFmt.get(), fmt::make_format_args(inArgs...)); }
	void                            LogV(StringView inFmt, fmt::format_args inArgs = {}, LogType inType = LogType::Normal);
	template <class... taArgs> void LogError(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs) { LogErrorV(inFmt.get(), fmt::make_format_args(inArgs...)); }
	void                            LogErrorV(StringView inFmt, fmt::format_args inArgs = {}) { LogV(inFmt, inArgs, LogType::Error); }

	void                            OpenLogFile();
	void                            CloseLogFile();

	String                          mMainWindowTitle   = "Asset Cooker";
	void*                           mMainWindowHwnd    = nullptr;
	bool                            mExitRequested     = false;
	bool                            mExitReady         = false;
	String                          mUserPrefsFilePath = "prefs.toml";
	String                          mRuleFilePath      = "rules.toml";
	LogLevel                        mLogFSActivity     = LogLevel::Normal;
	struct Log                      mLog;
	FILE*                           mLogFile        = nullptr;
	String                          mLogDirectory   = "Logs";
	String                          mCacheDirectory = "Cache";
	String                          mInitError;
};

inline App gApp;