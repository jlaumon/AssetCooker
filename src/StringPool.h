#pragma once
#include "Core.h"
#include "memory"
#include <span>
#include <vector>

// Simple linear allocator for strings.
struct StringPool
{
	static constexpr size_t              cChunkSize = 1024 * 1024ull;
	std::vector<std::unique_ptr<char[]>> mChunks;
	size_t                               mLastChunkSize     = 0;
	size_t                               mLastChunkCapacity = 0;

	std::span<char> Allocate(size_t inSize)
	{
		// Add one for the null terminator.
		size_t alloc_size = inSize + 1;

		// Do we need a new chunk?
		if (mLastChunkSize + alloc_size > mLastChunkCapacity)
		{
			// Make sure the chunk is always large enough for the requested size.
			size_t chunk_size = gMax(cChunkSize, alloc_size);

			// Allocate the chunk.
			mChunks.push_back(std::make_unique<char[]>(chunk_size));
			mLastChunkSize     = 0;
			mLastChunkCapacity = chunk_size;
		}

		// Allocate the string.
		char* str = mChunks.back().get() + mLastChunkSize;
		mLastChunkSize += alloc_size;

		// Put the null terminator.
		str[inSize] = 0;

		return { str, inSize };
	}

	std::string_view AllocateCopy(std::string_view inString)
	{
		auto span = Allocate(inString.size());

		inString.copy(span.data(), inString.size());

		return { span.data(), span.size() };
	}
};