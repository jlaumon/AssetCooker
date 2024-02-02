#pragma once
#include "Strings.h"

bool gIsDebuggerAttached();

// Get last error as a string.
String GetLastErrorString();

// Set the name of the current thread.
void   gSetCurrentThreadName(const wchar_t* inName);