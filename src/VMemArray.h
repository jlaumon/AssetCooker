/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"

#include <optional>

#include <Bedrock/Mutex.h>
#include <Bedrock/Atomic.h>
#include <Bedrock/PlacementNew.h>
#include <Bedrock/Vector.h>


using VMemArrayLock = LockGuard<Mutex>;

// Minimalistic ever-growing dynamic array backed by virtual memory. Can grow without relocating to a different address.
// Adding needs to be protected by a mutex, but reading from multiple threads is allowed without lock since they're always valid once added.
template <typename taType>
struct VMemArray : NoCopy
{
	using ValueType = taType;

	VMemArray(int inMaxCapacityInBytes = 0, int inMinGrowSizeInBytes = 0)
	{
		mVector = VMemVector<taType>(VMemAllocator<taType>(inMaxCapacityInBytes, inMinGrowSizeInBytes));
	}

	~VMemArray()
	{
		// Destroy all elements.
		Clear();
	}

	[[nodiscard]] VMemArrayLock Lock() { return VMemArrayLock(mMutex); }

	void Add(const taType& inElement, const OptionalRef<const VMemArrayLock>& inLock = {})
	{
		// If no lock was provided, make a new one.
		const VMemArrayLock& lock = inLock ? (const VMemArrayLock&)*inLock : (const VMemArrayLock&)Lock();

		// Reserve instead of letting PushBack do a geometric grow since VMem will commit a large block anyway.
		mVector.Reserve(mVector.Size() + 1);

		mVector.PushBack(inElement);

		// Update mEnd last to let readers see the new element only when it's ready.
		IncreaseSize(1, lock);
	}

	template <typename... taArgs>
	taType& Emplace(const OptionalRef<const VMemArrayLock>& inLock = {}, taArgs&&... inArgs)
	{
		// If no lock was provided, make a new one.
		const VMemArrayLock& lock = inLock ? (const VMemArrayLock&)*inLock : (const VMemArrayLock&)Lock();

		// Reserve instead of letting PushBack do a geometric grow since VMem will commit a large block anyway.
		mVector.Reserve(mVector.Size() + 1);

		taType& new_element = mVector.EmplaceBack(gForward<taArgs>(inArgs)...);

		// Update mEnd last to let readers see the new element only when it's ready.
		IncreaseSize(1, lock);

		return new_element;
	}

	// Increase the current size. This is not like resize, elements are not constructed.
	// Meant to be used with a manual lock: first lock, then reserve, then construct elements manually, then set size.
	void IncreaseSize(int inSizeIncreaseInElements, const VMemArrayLock& inLock)
	{
		mAtomicSize.Add(inSizeIncreaseInElements, MemoryOrder::Relaxed);
	}

	// Make sure enough memory is committed for that many more elements.
	// Return the span for the new elements (constructed if they have a constructor, but not zero initialzied if they're trivial types).
	[[nodiscard]] Span<taType> EnsureCapacity(int inExtraCapacityInElements, const VMemArrayLock& inLock)
	{
		ValidateLock(inLock);

		mVector.Resize(mVector.Size() + inExtraCapacityInElements, EResizeInit::NoZeroInit);

		taType* cur_end = mVector.Begin() + mAtomicSize.Load(MemoryOrder::Relaxed);
		return { cur_end, inExtraCapacityInElements };
	}

	// Destroy all elements.
	// Note: not thread safe if readers are allowed without lock.
	void Clear(const OptionalRef<const VMemArrayLock>& inLock = {})
	{
		// If no lock was provided, make a new one.
		const VMemArrayLock& lock = inLock.value_or((const VMemArrayLock&)Lock());
		(void)lock;

		mAtomicSize.Store(mVector.Size());

		mVector.Clear();
	}

	taType&              operator[](size_t inIndex)			{ gAssert(inIndex < Size()); return mVector[inIndex]; }
	const taType&        operator[](size_t inIndex) const	{ gAssert(inIndex < Size()); return mVector[inIndex]; }
					     
	size_t               Size() const						{ return mAtomicSize.Load(); }
	size_t               SizeRelaxed() const				{ return mAtomicSize.Load(MemoryOrder::Relaxed); }	// To use inside lock scope.
	size_t               CapacityInBytes() const			{ return mVector.Capacity(); }
	size_t               MaxCapacityInBytes() const			{ return mVector.GetAllocator().GetArena()->GetReservedSize(); }

	taType*              Begin()							{ return mVector.Begin(); }
	taType*              End()								{ return mVector.Begin() + mAtomicSize.Load(); }
	taType*              begin()							{ return mVector.Begin(); }
	taType*              end()								{ return mVector.Begin() + mAtomicSize.Load(); }
	const taType*        Begin()	const					{ return mVector.Begin(); }
	const taType*        End()		const					{ return mVector.Begin() + mAtomicSize.Load(); }
	const taType*        begin()	const					{ return mVector.Begin(); }
	const taType*        end()		const					{ return mVector.Begin() + mAtomicSize.Load(); }

private:
	void                 ValidateLock(const VMemArrayLock& inLock) const { gAssert(inLock.GetMutex() == &mMutex); }

	VMemVector<taType>   mVector;
	Atomic<int>          mAtomicSize = 0;
	Mutex                mMutex;
};


// VMemArray is a contiguous container.
template<class T> inline constexpr bool cIsContiguous<VMemArray<T>> = true;