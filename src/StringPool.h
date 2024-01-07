#pragma once
#include "Strings.h"
#include <vector>
#include <memory>


// Simple linear allocator for strings.
struct StringPool
{
	struct Chunk
	{
		std::unique_ptr<char[]> mData;
		size_t                  mSize;
	};
	size_t                               mDefaultChunkSize  = 1024 * 1024ull;
	std::vector<Chunk>                   mChunks;
	char*                                mLastChunk         = nullptr;
	size_t                               mLastChunkCapacity = 0;

	struct ResizableStringView;

	void Clear()
	{
		if (!mChunks.empty())
		{
			// Keep just one chunk.
			mChunks.resize(1);

			// Reset to its start.
			mLastChunk         = mChunks.back().mData.get();
			mLastChunkCapacity = mChunks.back().mSize;
		}
	}

	MutStringView Allocate(size_t inSize)
	{
		// Add one for the null terminator.
		size_t alloc_size = inSize + 1;

		// Do we need a new chunk?
		if (alloc_size > mLastChunkCapacity)
			AddChunk(alloc_size);

		// Allocate the string.
		MutStringView str = { mLastChunk, alloc_size };
		mLastChunk += alloc_size;
		mLastChunkCapacity -= alloc_size;

		// Put the null terminator preemptively.
		str[inSize] = 0;

		return str;
	}


	MutStringView AllocateCopy(StringView inString)
	{
		auto storage = Allocate(inString.size());

		gAppend(storage, inString);

		return storage;
	}


	void AddChunk(size_t inMinSize)
	{
		// Make sure the chunk is always large enough for the requested size.
		size_t chunk_size = gMax(mDefaultChunkSize, inMinSize);

		// Allocate the chunk.
		mChunks.push_back({ std::make_unique<char[]>(chunk_size), chunk_size });
		mLastChunk         = mChunks.back().mData.get();
		mLastChunkCapacity = chunk_size;
	}

	size_t GetTotalAllocatedSize() const
	{
		size_t total = 0;
		for (const Chunk& chunk : mChunks)
			total += chunk.mSize;
		return total;
	}

	struct ResizableStringView
	{
		using value_type = char;

		char*           mData     = nullptr;
		size_t          mSize     = 0;
		StringPool&     mPool;

		MutStringView	AsMutStringView()		{ return { mData, mSize }; }
		StringView		AsStringView() const	{ return { mData, mSize - 1 }; }


		void Append(StringView inStr)
		{
			// String needs to be the last alloc in the pool to allow resizing.
			gAssert(mPool.mLastChunk == mData + mSize);

			size_t additional_size = inStr.size();

			// Check if it would it fit in the current chunk.
			if (additional_size > mPool.mLastChunkCapacity)
			{
				size_t min_chunk_size = mSize + additional_size;

				// If the string size is already larger than a chunk, grow more aggressively to avoid allocating a new chunk at each append.
				// This is a lot more aggressive than eg. std::vector because we're wasting the memory of the chunks where the string doesn't fit,
				// so we really want to make sure the next size fits everything! 
				if (min_chunk_size > mPool.mDefaultChunkSize)
					min_chunk_size *= 4;

				// Add a new chunk.
				mPool.AddChunk(min_chunk_size);

				// Copy the string into it.
				auto new_str = mPool.AllocateCopy(AsStringView());

				// Update data with re-allocated string.
				mData = new_str.data();
				mSize = new_str.size();
			}

			// Now append the new part.
			// Note: -1 because we write over the null terminator of the current string.
			char* current_end = mData + mSize - 1;
			for (size_t i = 0; i < additional_size; i++)
				current_end[i] = inStr[i];

			// Add a new null-terminator.
			current_end[additional_size] = 0;

			mSize += additional_size;
			mPool.mLastChunk += additional_size;
			mPool.mLastChunkCapacity -= additional_size;
		}

		template<typename... taArgs> void AppendFormat(std::format_string<taArgs...> inFmt, const taArgs&... inArgs)
		{
			std::vformat_to(std::back_inserter(*this), inFmt.get(), std::make_format_args(inArgs...));
		}

		void push_back(char inChar)
		{
			Append({ &inChar, 1 });
		}
	};

	
	ResizableStringView CreateResizableString()
	{
		// Allocate an empty string (actually allocates a null terminator).
		auto str = Allocate(0);
		return { str.data(), str.size(), *this };
	}

};