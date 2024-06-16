/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "CookingSystemIDs.h"

void gUISetDPIScale(float inDPIScale);
void gUISetUserScale(float inScale);
float gUIGetUserScale();
void gUIUpdate();

void gDrawMainMenuBar();
void gDrawMain();
void gDrawCookingQueue();

void gSelectCookingLogEntry(CookingLogEntryID inLogEntryID, bool inScrollLog);

struct UIStats
{
	double mCPUMilliseconds = 0.0;
	double mGPUMilliseconds = 0.0;
};

inline UIStats gUILastFrameStats;
