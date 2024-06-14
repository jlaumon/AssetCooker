#include "Core.h"
#include "Tests.h"
#include "Debug.h"
#include "App.h"
#include "FileUtils.h"

#ifdef ASSERTS_ENABLED

static thread_local bool sInsideAssert = false;

bool gReportAssert(const char* inCondition, const char* inFile, int inLine)
{
	// If gReportAssert triggers an assert, early out to avoid infinite recursion.
	if (sInsideAssert)
		return false;

	sInsideAssert = true;
	defer { sInsideAssert = false; };

	// If we are running a test, fail that test.
	if (gIsRunningTest())
	{
		gFailTest("gAssert", inCondition, gGetFileNamePart(inFile), inLine);

		// Don't break during tests, there's already a breakpoint inside gFailTest if a debugger is attached.
		return false;
	}
	
	// Log the assert.
	// TODO: log the entire callstack instead
	gApp.LogError("gAssert({}) broke ({}:{}).", inCondition, gGetFileNamePart(inFile), inLine);

	return true;
}


#endif