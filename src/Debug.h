/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "Strings.h"

bool gIsDebuggerAttached();

// Get last error as a string.
TempString512 GetLastErrorString();

// Set the name of the current thread.
void   gSetCurrentThreadName(const wchar_t* inName);