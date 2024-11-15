/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "Strings.h"
#include "VMemArray.h"


// Simple linear allocator for strings.
struct StringPool
{
	VMemArray<char> mBuffer;

	struct ResizableStringView;

	StringPool(size_t inMinGrowSizeInBytes = 0) : mBuffer(0, inMinGrowSizeInBytes) {}

	void Clear()
	{
		mBuffer.Clear();
	}

	MutStringView Allocate(size_t inSize, const OptionalRef<const VMemArrayLock>& inLock = {})
	{
		// If no lock was provided, make a new one.
		const VMemArrayLock& lock = inLock ? (const VMemArrayLock&)*inLock : (const VMemArrayLock&)mBuffer.Lock();

		// Add one for the null terminator.
		size_t alloc_size = inSize + 1;

		MutStringView str = mBuffer.EnsureCapacity(alloc_size, lock);

		// Put the null terminator preemptively.
		str[inSize] = 0;

		mBuffer.IncreaseSize(alloc_size, lock);

		return str;
	}


	MutStringView AllocateCopy(StringView inString, const OptionalRef<const VMemArrayLock>& inLock = {})
	{
		auto storage = Allocate(inString.Size(), inLock);

		gAppend(storage, inString);

		return storage;
	}


	size_t GetTotalAllocatedSize() const
	{
		return mBuffer.CapacityInBytes();
	}

	struct ResizableStringView
	{
		using value_type          = char;
		using ValueType           = char;

		char*           mData     = nullptr;
		int				mSize     = 0;
		StringPool&     mPool;
		VMemArrayLock   mPoolLock;	// Pool is locked while we have one resizable string being used.

		MutStringView	AsMutStringView()		{ return { mData, mSize }; }
		StringView		AsStringView() const	{ return { mData, mSize - 1 }; }


		void Append(StringView inStr)
		{
			// String needs to be the last alloc in the pool to allow resizing.
			// Though that should always be the case since we have locked the pool.
			gAssert(mPool.mBuffer.End() == mData + mSize);

			int additional_size = inStr.Size();
			(void)mPool.mBuffer.EnsureCapacity(additional_size, mPoolLock);

			// Append the new part.
			// Note: -1 because we write over the null terminator of the current string.
			char* current_end = mData + mSize - 1;
			for (size_t i = 0; i < additional_size; i++)
				current_end[i] = inStr[i];

			// Add a new null-terminator.
			current_end[additional_size] = 0;

			mSize += additional_size;
			mPool.mBuffer.IncreaseSize(additional_size, mPoolLock);
		}

		template<typename... taArgs> void AppendFormat(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs)
		{
			AppendFormatV(inFmt.get(), fmt::make_format_args(inArgs...));
		}

		void AppendFormatV(StringView inFmt, fmt::format_args inArgs)
		{
			fmt::vformat_to(std::back_inserter(*this), fmt::string_view(inFmt.Data(), inFmt.Size()), inArgs);
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
		return { str.data(), str.Size(), *this, this->mBuffer.Lock() };
	}

};