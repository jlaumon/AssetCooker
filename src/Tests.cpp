/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Tests.h"
#include "App.h"
#include "Debug.h"

#include <vector>

struct Test
{
	StringView   mName;
	TestFunction mFunction = nullptr;
};

static std::vector<Test>& sGetAllTests()
{
	// Return a static variable to avoid issues with globals initialization order.
	static std::vector<Test> sAllTests;
	return sAllTests;
}


void gRegisterTest(StringView inName, TestFunction inFunction)
{
	gAssert(!inName.empty());

	sGetAllTests().push_back({ inName, inFunction });
}


static thread_local StringView sCurrentTestName;
static thread_local bool       sCurrentTestSuccess;

bool gIsRunningTest()
{
	return !sCurrentTestName.empty();
}


bool gRunTests()
{
	gApp.Log("Running all tests.");
	bool all_success = true;

	for (const Test& test : sGetAllTests())
	{
		sCurrentTestName    = test.mName;
		sCurrentTestSuccess = true;
		gApp.Log(R"(Test "{}" starting.)", test.mName);

		test.mFunction();

		gApp.Log(R"(Test "{}" finished: {}.)", test.mName, sCurrentTestSuccess ? "Success" : "Failure");
		all_success = all_success && sCurrentTestSuccess;
		sCurrentTestName = "";
	}

	return all_success;
}


void gFailTest(StringView inMacro, StringView inCode, StringView inFile, int inLine)
{
	gApp.LogError(R"({}({}) failed ({}:{}))", inMacro, inCode, inFile, inLine);

	sCurrentTestSuccess = false;

	// If a debugger is attached, break here.
	if (gIsDebuggerAttached())
		breakpoint;
}


