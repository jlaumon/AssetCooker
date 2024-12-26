/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once


#include <Bedrock/Core.h>

// Forward declarations of std types we don't want to include here.
namespace std
{
template <class T> class optional;
template <class T> class reference_wrapper;
}

// TODO: remove and add only where needed
#include <Bedrock/Span.h> 
#include <Bedrock/HashMap.h> 

// Typedef for Optional, until we have a custom version.
template <typename taType>
using Optional = std::optional<taType>;
template <typename taType>
using OptionalRef = std::optional<std::reference_wrapper<taType>>;

// Helper struct to iterate multiple Spans as a single one.
template <typename taType, size_t taSpanCount>
struct MultiSpanRange
{
	struct Iterator
	{
		Span<Span<taType>> mSpans;
		size_t             mSpanIndex = 0;
		size_t             mElemIndex = 0;

		bool               operator!=(const Iterator& inOther) const { return mSpanIndex != inOther.mSpanIndex || mElemIndex != inOther.mElemIndex; }
		taType&            operator*() { return mSpans[mSpanIndex][mElemIndex]; }
		void               operator++()
		{
			gAssert(mSpanIndex < mSpans.Size() && mElemIndex < mSpans[mSpanIndex].Size());

			mElemIndex++;

			// Loop in case there are empty spans.
			while (mSpanIndex < mSpans.Size() && mElemIndex == mSpans[mSpanIndex].Size())
			{
				mElemIndex = 0;
				mSpanIndex++;
			}
		}
	};

	Iterator begin() { return { mSpans, 0, 0 }; }
	Iterator end() { return { mSpans, taSpanCount, 0 }; }
	bool     Empty() const { return Size() == 0; }
	size_t   Size() const
	{
		size_t total_size = 0;
		for (auto& span : mSpans)
			total_size += span.Size();
		return total_size;
	}

	Span<taType> mSpans[taSpanCount] = {};
};


// Helper to turn a Span into a HashSet.
template <typename taType>
static TempHashSet<taType> gToHashSet(Span<taType> inSpan)
{
	TempHashSet<taType> hash_set;
	hash_set.Reserve(inSpan.Size());

	for (taType& element : inSpan)
		hash_set.Insert(element);

	return hash_set;
}