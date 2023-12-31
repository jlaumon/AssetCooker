#include "App.h"
#include "Debug.h"
#include "win32/dbghelp.h"

#include "win32/window.h"

void App::Init(void* inHwnd)
{
	mMainWindowHwnd = inHwnd;
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
	StringView formatted_str;

	// Add to the in-memory log.
	{
		std::lock_guard lock(mLogMutex);
		formatted_str = mLog.Add(inType, inFmt, inArgs);
	}

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


void App::DrawLog()
{
	std::lock_guard lock(mLogMutex);
	mLog.Draw();
}

