#include "App.h"

#include "ConfigReader.h"
#include "RuleReader.h"
#include "Debug.h"
#include "CookingSystem.h"
#include "UI.h"

#include "win32/dbghelp.h"
#include "win32/misc.h"
#include "win32/window.h"

void App::Init(void* inHwnd)
{
	mMainWindowHwnd = inHwnd;

	mLog.mAutoAddTime = true;

	// The application includes a manifest that should make the default code page be UTF8 (if at least Windows 10 1903).
	// This in turn should mean that most ANSI win32 functions actually support UTF8.
	// TODO: use A version of windows functions wherever possible
	Log("UTF8 is {}.", GetACP() == CP_UTF8 ? "supported. Noice" : "not supported");

	// Read the config file.
	gReadConfigFile("config.toml");

	// Read the rule file.
	if (!HasInitError())
		gReadRuleFile(mRuleFilePath);

	// If all is good, start scanning files.
	if (!HasInitError())
		gFileSystem.StartMonitoring();
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


void App::FatalErrorV(std::string_view inFmt, std::format_args inArgs)
{
	// Make sure a single thread triggers the pop-up.
	static std::mutex blocker;
	blocker.lock();

	// Log the error first.
	LogErrorV(inFmt, inArgs);
	
	if (gIsDebuggerAttached())
		breakpoint;
	else
		// TODO: use wide chars
		MessageBoxA(mMainWindowHwnd, std::vformat(inFmt, inArgs).c_str(), "Fatal Error", MB_OK | MB_ICONERROR | MB_APPLMODAL);

	// TODO: will need to do a proper exit and save the database at some point...
	exit(1);
}


void App::LogV(std::string_view inFmt, std::format_args inArgs, LogType inType)
{
	// Add to the in-memory log.
	StringView formatted_str = mLog.Add(inType, inFmt, inArgs);

	// Convert to wide char to write to the debug output.
	wchar_t message_buffer[4096];
	auto wchar_message = gUtf8ToWideChar(formatted_str, message_buffer);
	gAssert(wchar_message); // Buffer too small? Why would you log something that long?

	if (wchar_message && wchar_message->size() < gElemCount(message_buffer) - 2)
	{
		// Add an end of line and null terminator.
		message_buffer[wchar_message->size() + 0] = L'\n';
		message_buffer[wchar_message->size() + 1] = L'\0';

		// Write the message to the output.
		OutputDebugStringW(message_buffer);
	}

	// TODO: also log to a file
}


