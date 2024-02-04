#pragma once

#include "Core.h"
#include "Log.h"

#include <string>

constexpr const char* cAppName = "Asset Cooker";

enum class LogLevel : uint8
{
	None,
	Normal,
	Verbose,
};

// Manages the initialization and life time of the application.
struct App
{
	void Init(void* inHwnd);

	void RequestExit();
	bool IsExitRequested();
	bool IsExitReady();

	bool HasInitError() const { return !mInitError.empty(); }
	void SetInitError(StringView inText) { mInitError = inText; }

	template <class... taArgs>
	[[noreturn]] void               FatalError(std::format_string<taArgs...> inFmt, const taArgs&... inArgs) { FatalErrorV(inFmt.get(), std::make_format_args(inArgs...)); }
	[[noreturn]] void               FatalErrorV(std::string_view inFmt, std::format_args inArgs = {});

	template <class... taArgs> void Log(std::format_string<taArgs...> inFmt, const taArgs&... inArgs) { LogV(inFmt.get(), std::make_format_args(inArgs...)); }
	void                            LogV(std::string_view inFmt, std::format_args inArgs = {}, LogType inType = LogType::Normal);
	template <class... taArgs> void LogError(std::format_string<taArgs...> inFmt, const taArgs&... inArgs) { LogErrorV(inFmt.get(), std::make_format_args(inArgs...)); }
	void                            LogErrorV(std::string_view inFmt, std::format_args inArgs = {}) { LogV(inFmt, inArgs, LogType::Error); }

	void*                           mMainWindowHwnd = nullptr;
	bool                            mExitRequested  = false;
	bool                            mExitReady      = false;
	LogLevel                        mLogFSActivity  = LogLevel::None;
	struct Log                      mLog;
	String                          mInitError;
};

inline App gApp;