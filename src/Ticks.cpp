#include "Ticks.h"

#include "win32/misc.h"


int64 sGetTicksPerSecond()
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
	return init.mTicksPerSecond;
}


int64 gGetTickCount()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return counter.QuadPart;
}


int64 gTicksToNanoseconds(int64 inTicks)
{
	return inTicks * 1'000'000'000 / sGetTicksPerSecond();
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


int64 gNanosecondsToTicks(int64 inNanoseconds)
{
	return inNanoseconds * sGetTicksPerSecond() / 1'000'000'000;
}


int64 gMillisecondsToTicks(double inMilliseconds)
{
	return gNanosecondsToTicks(int64(inMilliseconds * 1'000'000.0));
}


int64 gSecondsToTicks(double inSeconds)
{
	return gNanosecondsToTicks(int64(inSeconds * 1'000'000'000.0));
}
