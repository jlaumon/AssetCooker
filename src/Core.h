/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once


#include <Bedrock/Core.h>


// Basic containers.
#include "ankerl/unordered_dense.h"

// These namespaces are too long, add shortcuts.
template <  class Key,
			class T,
			class Hash =  ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
			class Bucket =  ankerl::unordered_dense::bucket_type::standard>
using HashMap = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class T,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using SegmentedHashMap = ankerl::unordered_dense::segmented_map<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<Key>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using HashSet = ankerl::unordered_dense::set<Key, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<Key>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using SegmentedHashSet = ankerl::unordered_dense::segmented_set<Key, Hash, KeyEqual, AllocatorOrContainer, Bucket>;


// Hash helper to hash entire structs.
// Only use on structs/classes that don't contain any padding.
template <typename taType>
struct MemoryHasher
{
	using is_avalanching = void; // mark class as high quality avalanching hash

    uint64 operator()(const taType& inValue) const noexcept
	{
		static_assert(std::has_unique_object_representations_v<taType>);
        return ankerl::unordered_dense::detail::wyhash::hash(&inValue, sizeof(inValue));
    }
};

inline uint64 gHash(uint64 inValue) { return ankerl::unordered_dense::detail::wyhash::hash(inValue); }
inline uint64 gHashCombine(uint64 inA, uint64 inB) { return ankerl::unordered_dense::detail::wyhash::mix(inA, inB); }


// Forward declarations of std types we don't want to include here.
namespace std
{
template <class T, size_t Extent> class span;
template <class T> class optional;
template <class T> class reference_wrapper;
}

// TODO: remove and add only where needed
#include <Bedrock/Span.h> 

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
static HashSet<taType> gToHashSet(Span<taType> inSpan)
{
	HashSet<taType> hash_set;
	hash_set.reserve(inSpan.Size());

	for (taType& element : inSpan)
		hash_set.insert(element);

	return hash_set;
}