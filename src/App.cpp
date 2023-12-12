#include <App.h>

bool gExitRequested = false;
bool gExitReady = false;

void gRequestExit()
{
	gExitRequested = true;

	// At the moment we're ready immediately, but in the future we'll need to save cooking state which might take a long time.
	gExitReady = true;
}

bool gIsExitRequested()
{
	return gExitRequested;
}

bool gIsExitReady()
{
	return gExitReady;
}