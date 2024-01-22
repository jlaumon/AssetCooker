#pragma once

#include "Core.h"

#include <atomic>
#include <optional>
#include <functional>

struct VMemBlock
{
	uint8* mBegin = nullptr;
	uint8* mEnd   = nullptr;

	size_t Size() const { return mEnd - mBegin; }
};

size_t    gVMemReserveGranularity();		// Return the granularity at which memory can be reserved.
size_t    gVMemCommitGranularity();			// Return the granularity at which memory can be committed.
VMemBlock gVMemReserve(size_t inSize);		// Reserve some memory. inSize will be rounded up to reserve granularity.
void      gVMemFree(VMemBlock inBlock);		// Free previously reserved memory.
VMemBlock gVMemCommit(VMemBlock inBlock);	// Commit some reserved memory.

using VMemArrayLock = std::unique_lock<std::mutex>;

// Minimalistic ever-growing dynamic array backed by virtual memory. Can grow without relocating to a different address.
// Adding needs to be protected by a mutex, but reading from multiple threads is allowed without lock since they're always valid once added.
template <typename taType>
struct VMemArray : NoCopy
{
	VMemArray(size_t inMaxCapacityInBytes = 0, size_t inMinGrowSizeInBytes = 0)
	{
		if (inMaxCapacityInBytes)
			mSizeToReserve = inMaxCapacityInBytes;

		if (inMinGrowSizeInBytes)
			mMinCommitSize = inMinGrowSizeInBytes;

		// Reserve backing memory on immediately as we want safe read access without lock.
		mBegin        = (taType*)gVMemReserve(mSizeToReserve).mBegin;
		mEnd          = mBegin;
		mEndCommitted = (uint8*)mBegin;
	}

	~VMemArray()
	{
		// Destroy all elements.
		Clear();

		// Free the VMem.
		if (mBegin)
			gVMemFree({ (uint8*)mBegin, (uint8*)mBegin + mSizeToReserve });
	}

	[[nodiscard]] VMemArrayLock Lock() { return VMemArrayLock(mMutex); }

	void Add(const taType& inElement, const OptionalRef<const VMemArrayLock>& inLock = {})
	{
		// If no lock was provided, make a new one.
		const VMemArrayLock& lock = inLock.value_or((const VMemArrayLock&)Lock());

		taType* new_element = EnsureCapacity(1, lock)[0];

		// Call copy constructor.
		new (new_element) taType(inElement);

		// Update mEnd last to let readers see the new element only when it's ready.
		IncreaseSize(1, lock);
	}

	// Increase the current size. This is not like resize, elements are not constructed.
	// Meant to be used with a manual lock: first lock, then reserve, then construct elements manually, then set size.
	void IncreaseSize(size_t inSizeIncreaseInElements, const VMemArrayLock& inLock)
	{
		mEnd += inSizeIncreaseInElements;
	}

	// Make sure enough memory is committed for that many more elements.
	// Return the span for the new elements (not constructed).
	Span<taType> EnsureCapacity(size_t inExtraCapacityInElements, const VMemArrayLock& inLock)
	{
		ValidateLock(inLock);

		taType* cur_end = mEnd.load(std::memory_order::relaxed);
		taType* new_end = cur_end + inExtraCapacityInElements;

		// Do we need to commit more memory?
		if ((uint8*)new_end <= mEndCommitted)
			return { cur_end, new_end };

		// It's fine if this is not aligned to pages, gVMemCommit will round up.
		uint8* new_end_committed = gMax((uint8*)new_end, mEndCommitted + mMinCommitSize);

		mEndCommitted = gVMemCommit({ mEndCommitted, new_end_committed }).mEnd;

		return { cur_end, new_end };
	}

	// Destroy all elements.
	// Note: not thread safe if readers are allowed without lock.
	void Clear(const OptionalRef<const VMemArrayLock>& inLock = {})
	{
		// If no lock was provided, make a new one.
		const VMemArrayLock& lock = inLock.value_or((const VMemArrayLock&)Lock());

		Span elements = *this;
		mEnd          = mBegin;

		for (taType& element :elements)
			element.~taType();
	}

	taType&              operator[](size_t inIndex)			{ gAssert(inIndex < Size()); return mBegin[inIndex]; }
	const taType&        operator[](size_t inIndex) const	{ gAssert(inIndex < Size()); return mBegin[inIndex]; }
					     
	size_t               Size() const						{ return mEnd - mBegin; }
	size_t               SizeRelaxed() const				{ return mEnd.load(std::memory_order::relaxed) - mBegin; }	// To use inside lock scope.
	size_t               CapacityInBytes() const			{ return (uint8*)mBegin - mEndCommitted; }
	size_t               MaxCapacityInBytes() const			{ return mSizeToReserve; }

	taType*              Begin()							{ return mBegin; }
	taType*              End()								{ return mEnd; }
	taType*              begin()							{ return mBegin; }
	taType*              end()								{ return mEnd; }

private:
	void                 ValidateLock(const VMemArrayLock& inLock) const { gAssert(inLock && inLock.mutex() == &mMutex); }

	taType*              mBegin         = nullptr;
	std::atomic<taType*> mEnd           = nullptr;
	uint8*               mEndCommitted  = nullptr;
	std::mutex           mMutex;
	size_t               mSizeToReserve = 1024ull * 1024 * 1024;
	size_t               mMinCommitSize = 1024ull * 1024;			// Minimum size to commit at once, to avoid calling gVMemCommit too often.
};