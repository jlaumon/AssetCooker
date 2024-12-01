/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include <Bedrock/Ticks.h>

#include <Bedrock/Mutex.h>
#include <Bedrock/ConditionVariable.h>


struct SyncSignal : NoCopy
{
	enum class WaitResult : uint8
	{
		Success,
		Timeout
	};

	inline void             Set();
	inline void             Clear();
	inline void             Wait();
	inline WaitResult       WaitFor(int64 inTicks);
	inline void             SetAutoClear(bool inAutoClear) { mAutoClear = inAutoClear; }

private:
	ConditionVariable       mCond;
	Mutex                   mMutex;
	bool                    mIsSet     = false;
	bool                    mAutoClear = true;
};


void SyncSignal::Set()
{
	{
		LockGuard lock(mMutex);
		if (mIsSet == true)
			return; // Already set, nothing to do.

		mIsSet = true;
	}

	if (mAutoClear)
		mCond.NotifyOne();
	else
		mCond.NotifyAll();
}


void SyncSignal::Clear()
{
	LockGuard lock(mMutex);
	mIsSet = false;
}


void SyncSignal::Wait()
{
	LockGuard lock(mMutex);

	while (!mIsSet)
	{
		mCond.Wait(lock);
	}

	if (mAutoClear)
		mIsSet = false;
}


SyncSignal::WaitResult SyncSignal::WaitFor(int64 inTicks)
{
	LockGuard lock(mMutex);

	while (!mIsSet)
	{
		int64 start_ticks = gGetTickCount();

		if (mCond.Wait(lock, (NanoSeconds)gTicksToNanoseconds(inTicks)) == ConditionVariable::WaitResult::Timeout)
			return WaitResult::Timeout;

		// Decrease the time to wait in case it was a spurious wake up.
		int64 elapsed_ticks = gGetTickCount() - start_ticks;
		inTicks -= gMax(inTicks - elapsed_ticks, (int64)0);
	}

	if (mAutoClear)
		mIsSet = false;

	return WaitResult::Success;
}