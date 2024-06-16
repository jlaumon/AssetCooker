/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"

// Segmented double ended queue.
template <typename taType, size_t taSegmentSizeInElements = 1024>
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
		mSegments                = std::move(ioOtherQueue.mSegments);
		
		ioOtherQueue.mFirstSegmentBeginOffset = 0;
		ioOtherQueue.mLastSegmentEndOffset    = taSegmentSizeInElements;
		ioOtherQueue.mSegments.clear();
	}

	void PushBack(const taType& inElement)
	{
		// If last segment is full, add a new one.
		if (mLastSegmentEndOffset == taSegmentSizeInElements)
		{
			taType* new_segment = (taType*)malloc(taSegmentSizeInElements * sizeof(taType));
			mSegments.push_back(new_segment);
			mLastSegmentEndOffset = 0;
		}
		
		// Increment back offset.
		mLastSegmentEndOffset++;

		// Copy-construct the new element.
		new (&Back()) taType(inElement);
	}

	void PushFront(const taType& inElement)
	{
		// If first segment is full, add a new one. 
		if (mFirstSegmentBeginOffset == 0)
		{
			taType* new_segment = (taType*)malloc(taSegmentSizeInElements * sizeof(taType));
			mSegments.insert(mSegments.begin(), new_segment);
			mFirstSegmentBeginOffset = taSegmentSizeInElements;
		}
		
		// Decrement the front offset.
		mFirstSegmentBeginOffset--;

		// Copy-construct the new element.
		new (&Front()) taType(inElement);
	}

	void PopFront()
	{
		// Destroy the element.
		Front().~taType();

		// Increment the begin offset.
		mFirstSegmentBeginOffset++;

		// Special case when there is a single segment, we don't want to free it even it it's empty.
		if (mSegments.size() == 1)
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
				free(mSegments.front());
				mSegments.erase(mSegments.begin());
			}
		}
	}

	taType& Front()
	{
		gAssert(GetSize() > 0);

		return mSegments.front()[mFirstSegmentBeginOffset];
	}

	taType& Back()
	{
		gAssert(GetSize() > 0);

		return mSegments.back()[mLastSegmentEndOffset - 1];
	}

	void Clear()
	{
		for (size_t i = 0, n = GetSize(); i < n; ++i)
			PopFront();
	}

	size_t GetSize() const
	{
		return mSegments.size() * taSegmentSizeInElements           // Total size of the segments.
			   - mFirstSegmentBeginOffset                           // Number of elements not used in the first segment.
			   - (taSegmentSizeInElements - mLastSegmentEndOffset); // Number of elements not used in the last segment.
	}

	bool IsEmpty() const
	{
		return GetSize() == 0;
	}

private:

	size_t               mFirstSegmentBeginOffset = 0;                       // Offset of the first valid element in the first segment.
	size_t               mLastSegmentEndOffset    = taSegmentSizeInElements; // Offset after the last valid element in the last segment.
	std::vector<taType*> mSegments;
};