#pragma once

#include "Core.h"
#include "Log.h"

#include <string>

constexpr const char* cAppName = "Asset Cooker";

// Manages the initialization and life time of the application.
struct App
{
	void Init(void* inHwnd);

	void RequestExit();
	bool IsExitRequested();
	bool IsExitReady();

	void FatalError(StringView inMessage);

	void Log(StringView inMessage)		{ mLog.Add(inMessage); }
	void LogError(StringView inMessage)	{ mLog.Add(inMessage, LogType::Error); }

	struct Log mLog;
	void*      mMainWindowHwnd = nullptr;
	bool       mExitRequested  = false;
	bool       mExitReady      = false;
};

inline App gApp;