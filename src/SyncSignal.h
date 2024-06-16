/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include "Ticks.h"

#include <condition_variable>
#include <mutex>


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
	std::condition_variable mCond;
	std::mutex              mMutex;
	bool                    mIsSet     = false;
	bool                    mAutoClear = true;
};


void SyncSignal::Set()
{
	{
		std::unique_lock lock(mMutex);
		if (mIsSet == true)
			return; // Already set, nothing to do.

		mIsSet = true;
	}

	if (mAutoClear)
		mCond.notify_one();
	else
		mCond.notify_all();
}


void SyncSignal::Clear()
{
	std::unique_lock lock(mMutex);
	mIsSet = false;
}


void SyncSignal::Wait()
{
	std::unique_lock lock(mMutex);

	while (!mIsSet)
	{
		mCond.wait(lock);
	}

	if (mAutoClear)
		mIsSet = false;
}


SyncSignal::WaitResult SyncSignal::WaitFor(int64 inTicks)
{
	std::unique_lock lock(mMutex);

	while (!mIsSet)
	{
		int64 start_ticks = gGetTickCount();

		if (mCond.wait_for(lock, std::chrono::nanoseconds(gTicksToNanoseconds(inTicks))) == std::cv_status::timeout)
			return WaitResult::Timeout;

		// Decrease the time to wait in case it was a spurious wake up.
		int64 elapsed_ticks = gGetTickCount() - start_ticks;
		inTicks = gMax(inTicks - elapsed_ticks, (int64)0);
	}

	if (mAutoClear)
		mIsSet = false;

	return WaitResult::Success;
}