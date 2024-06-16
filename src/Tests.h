/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Strings.h"

// Tests are only enabled in debug. They are compiled but not registered in release (and should be optimized out).
#ifdef ASSERTS_ENABLED
#define TESTS_ENABLED
#endif

#ifdef TESTS_ENABLED
#define IF_TESTS_ENABLED(code) code
#else
#define IF_TESTS_ENABLED(code)
#endif


using TestFunction = void (*)();

void gRegisterTest(StringView inName, TestFunction inFunction); // Register a test. Called automatically by REGISTER_TEST.
bool gRunTests();												// Run all registered tests. Return true on success.
bool gIsRunningTest();											// Return true if the current thread is running a test.
void gFailTest(StringView inMacro, StringView inCode, StringView inFile, int inLine); // Fail current test. Called automatically by the TEST macros.


// Register a test.
// eg.
//
// REGISTER_TEST("Examples")
// {
//		TEST_TRUE(gGetTheAnswer() == 42);
//		TEST_FALSE(gIsTooManyCooks());
// };
struct TestDummy {
	StringView mName;
	consteval TestDummy(StringView inName)
	{
		gAssert(!inName.empty());
		mName = inName;
	}
};
struct TestRegisterer { TestRegisterer(StringView inName, TestFunction inFunction) { IF_TESTS_ENABLED(gRegisterTest(inName, inFunction)); } };
inline TestRegisterer operator*(TestDummy inDummy, TestFunction inFunction) { return { inDummy.mName, inFunction }; }
#define REGISTER_TEST(name) static auto TOKEN_PASTE(test_register, __LINE__) = TestDummy{ name } *[]()


consteval StringView gConstevalGetFileNamePart(StringView inPath)
{
	size_t file_start = inPath.find_last_of("\\/");
	if (file_start == StringView::npos)
		return inPath;

	return inPath.substr(file_start + 1);
}

#define TEST_TRUE(code) do { if (!(code)) gFailTest("TEST_TRUE", #code, gConstevalGetFileNamePart(__FILE__), __LINE__); } while(0)
#define TEST_FALSE(code) do { if (code) gFailTest("TEST_FALSE", #code, gConstevalGetFileNamePart(__FILE__), __LINE__); } while(0)

