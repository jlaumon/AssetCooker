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

	void FatalError(StringView inMessage);

	void Log(StringView inMessage, LogType inType = LogType::Normal);
	void LogError(StringView inMessage)									{ Log(inMessage, LogType::Error); }

	void DrawLog();

	void*      mMainWindowHwnd  = nullptr;
	bool       mExitRequested   = false;
	bool       mExitReady       = false;
	LogLevel   mLogScanActivity = LogLevel::None;
	LogLevel   mLogDiskActivity = LogLevel::None;
private:
	std::mutex mLogMutex;
	struct Log mLog;
};

inline App gApp;