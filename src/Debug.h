/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include <Bedrock/Debug.h>
#include <Bedrock/String.h>

// Get last error as a string.
TempString GetLastErrorString();

// Write a dump file.
struct _EXCEPTION_POINTERS;
bool gWriteDump(StringView inPath, uint32 inCrashingThreadID, _EXCEPTION_POINTERS* inExceptionInfo);
