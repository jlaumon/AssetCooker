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


void App::FatalError(StringView inMessage)
{
	gAssert(gIsNullTerminated(inMessage));

	// Make sure a single thread triggers the pop-up.
	static std::mutex blocker;
	blocker.lock();

	// Log the error first.
	LogError(inMessage);
	
	if (gIsDebuggerAttached())
		breakpoint;
	else
		MessageBoxA(mMainWindowHwnd, inMessage.data(), "Fatal Error", MB_OK | MB_ICONERROR | MB_APPLMODAL);

	// TODO: will need to do a proper exit and save the database at some point...
	exit(1);
}


void App::Log(StringView inMessage, LogType inType/* = LogType::Normal*/)
{
	// Add to the in-memory log.
	{
		std::lock_guard lock(mLogMutex);
		mLog.Add(inMessage, inType);
	}

	// Add an end of line character.
	char message_buffer[4096];
	gConcat(message_buffer, inMessage, "\n\0");

	// Write the message to the output.
	OutputDebugStringA(message_buffer);

	// TODO: also log to a file
}


void App::DrawLog()
{
	std::lock_guard lock(mLogMutex);
	mLog.Draw();
}

