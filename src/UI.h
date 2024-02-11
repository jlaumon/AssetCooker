#pragma once

#include "CookingSystemIDs.h"

void gUISetDPIScale(float inDPIScale);
void gUIUpdate();

void gDrawMainMenuBar();
void gDrawMain();
void gDrawCookingQueue();

void gSelectCookingLogEntry(CookingLogEntryID inLogEntryID, bool inScrollLog);

struct UIStats
{
	double mCPUMilliseconds = 0.0;
	double mGPUMilliseconds = 0.0;
	int    mFPS             = 0;	// Capped by VSYNC.
};

inline UIStats gUILastFrameStats;
