#include "App.h"
#include "Debug.h"

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

	if (gIsDebuggerAttached())
		breakpoint;
	else
		MessageBoxA(mMainWindowHwnd, inMessage.data(), "Fatal Error", MB_OK | MB_ICONERROR | MB_APPLMODAL);

	exit(1);
}