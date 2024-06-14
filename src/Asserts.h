#pragma once

// Break to the debugger (or crash if no debugger is attached).
#define breakpoint __debugbreak()

#ifdef ASSERTS_ENABLED

// Assert macro.
#define gAssert(condition)                                                 \
	do                                                                     \
	{                                                                      \
		if (!(condition) && gReportAssert(#condition, __FILE__, __LINE__)) \
			breakpoint;                                                    \
	} while (0)

// Internal assert report function. Return true if it should break.
bool gReportAssert(const char* inCondition, const char* inFile, int inLine);

#else

#define gAssert(condition) do { (void)sizeof(!(condition)); } while(0)

#endif

