#pragma once

#include "Core.h"

int64  gGetTickCount();
int64  gTicksToNanoSeconds(int64 inTicks);
double gTicksToSeconds(int64 inTicks);

const int64 gProcessStartTicks = gGetTickCount();