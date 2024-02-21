#pragma once

#include "Core.h"

int64  gGetTickCount();
int64  gTicksToNanoseconds(int64 inTicks);
double gTicksToMilliseconds(int64 inTicks);
double gTicksToSeconds(int64 inTicks);
int64  gNanosecondsToTicks(int64 inNanoseconds);
int64  gMillisecondsToTicks(double inMilliseconds);
int64  gSecondsToTicks(double inSeconds);


const int64 gProcessStartTicks = gGetTickCount();

struct Timer : NoCopy
{
	Timer() { Reset(); }
	void  Reset() { mTicks = gGetTickCount(); }
	int64 GetTicks() const { return gGetTickCount() - mTicks; }
	int64 mTicks = 0;
};