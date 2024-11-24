/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"

#include <Bedrock/Vector.h>

// Segmented double ended queue.
template <typename taType, int taSegmentSizeInElements = 1024>
struct Queue : NoCopy // Copy not implemented yet
{
	static_assert(gIsPow2(taSegmentSizeInElements));

	Queue() = default;
	~Queue()
	{
		Clear();
	}

	Queue(Queue&& ioOtherQueue)
	{
		mFirstSegmentBeginOffset = ioOtherQueue.mFirstSegmentBeginOffset;
		mLastSegmentEndOffset    = ioOtherQueue.mLastSegmentEndOffset;
		mSegments                = gMove(ioOtherQueue.mSegments);
		
		ioOtherQueue.mFirstSegmentBeginOffset = 0;
		ioOtherQueue.mLastSegmentEndOffset    = taSegmentSizeInElements;
		ioOtherQueue.mSegments.Clear();
	}

	void PushBack(const taType& inElement)
	{
		// If last segment is full, add a new one.
		if (mLastSegmentEndOffset == taSegmentSizeInElements)
		{
			taType* new_segment = (taType*)gMemAlloc(taSegmentSizeInElements * sizeof(taType)).mPtr;
			mSegments.PushBack(new_segment);
			mLastSegmentEndOffset = 0;
		}
		
		// Increment back offset.
		mLastSegmentEndOffset++;

		// Copy-construct the new element.
		gPlacementNew(Back(), inElement);
	}

	void PushFront(const taType& inElement)
	{
		// If first segment is full, add a new one. 
		if (mFirstSegmentBeginOffset == 0)
		{
			taType* new_segment = (taType*)gMemAlloc(taSegmentSizeInElements * sizeof(taType)).mPtr;
			mSegments.Insert(mSegments.begin(), new_segment);
			mFirstSegmentBeginOffset = taSegmentSizeInElements;
		}
		
		// Decrement the front offset.
		mFirstSegmentBeginOffset--;

		// Copy-construct the new element.
		gPlacementNew(Front(), inElement);
	}

	void PopFront()
	{
		// Destroy the element.
		Front().~taType();

		// Increment the begin offset.
		mFirstSegmentBeginOffset++;

		// Special case when there is a single segment, we don't want to free it even it it's empty.
		if (mSegments.Size() == 1)
		{
			// If that was the last element, just reset the offsets.
			if (mFirstSegmentBeginOffset == mLastSegmentEndOffset)
			{
				mLastSegmentEndOffset    = 0;
				mFirstSegmentBeginOffset = 0;
			}
		}
		else
		{
			// If that was the last element, but not the last segment, remove the segment.
			if (mFirstSegmentBeginOffset == taSegmentSizeInElements)
			{
				gMemFree({ (uint8*)mSegments.Front(), taSegmentSizeInElements * sizeof(taType) });
				mSegments.Erase(0);
			}
		}
	}

	taType& Front()
	{
		gAssert(GetSize() > 0);

		return mSegments.Front()[mFirstSegmentBeginOffset];
	}

	taType& Back()
	{
		gAssert(GetSize() > 0);

		return mSegments.Back()[mLastSegmentEndOffset - 1];
	}

	void Clear()
	{
		for (int i = 0, n = GetSize(); i < n; ++i)
			PopFront();
	}

	int GetSize() const
	{
		return mSegments.Size() * taSegmentSizeInElements           // Total size of the segments.
			   - mFirstSegmentBeginOffset                           // Number of elements not used in the first segment.
			   - (taSegmentSizeInElements - mLastSegmentEndOffset); // Number of elements not used in the last segment.
	}

	bool IsEmpty() const
	{
		return GetSize() == 0;
	}

private:

	int             mFirstSegmentBeginOffset = 0;                       // Offset of the first valid element in the first segment.
	int             mLastSegmentEndOffset    = taSegmentSizeInElements; // Offset after the last valid element in the last segment.
	Vector<taType*> mSegments;
};