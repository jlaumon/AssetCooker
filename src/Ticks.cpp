#include "Ticks.h"

#include "win32/misc.h"


int64 gGetTickCount()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return counter.QuadPart;
}


int64 gTicksToNanoseconds(int64 inTicks)
{
	struct Initializer
	{
		Initializer()
		{
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);

			mTicksPerSecond = frequency.QuadPart;
		}

		int64 mTicksPerSecond;
	};

	static Initializer init;
	
	return inTicks * 1'000'000'000 / init.mTicksPerSecond;
}


double gTicksToMilliseconds(int64 inTicks)
{
	int64 ns = gTicksToNanoseconds(inTicks);
	return (double)ns / 1'000'000.0;
}


double gTicksToSeconds(int64 inTicks)
{
	int64 ns = gTicksToNanoseconds(inTicks);
	return (double)ns / 1'000'000'000.0;
}