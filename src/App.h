#pragma once

#include "Core.h"
#include "Log.h"

enum class LogLevel : uint8
{
	None,
	Normal,
	Verbose,
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

	String                          mMainWindowTitle = "Asset Cooker";
	void*                           mMainWindowHwnd  = nullptr;
	bool                            mExitRequested   = false;
	bool                            mExitReady       = false;
	String                          mRuleFilePath    = "rules.toml";
	LogLevel                        mLogFSActivity   = LogLevel::Normal;
	struct Log                      mLog;
	String                          mLogDirectory = "Logs";
	FILE*                           mLogFile      = nullptr;
	String                          mInitError;
};

inline App gApp;