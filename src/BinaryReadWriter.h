/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include "Strings.h"
#include "VMemArray.h"
#include "App.h"


// Helper class to write a binary file.
struct BinaryWriter : NoCopy
{
	BinaryWriter()
	{
		// Lock once to avoid the overhead of locking many times.
		// TODO make the mutex a template param to have a dummy one when not needed?
		mBufferLock = mBuffer.Lock();
	}

	// Write the internal buffer to this file.
	bool WriteFile(FILE* ioFile);

	template <typename taType>
	void Write(Span<const taType> inSpan)
	{
		static_assert(cHasUniqueObjectRepresentations<taType>); // Don't write padding into the file.

		int size_bytes = inSpan.SizeInBytes();

		Span dest = mBuffer.EnsureCapacity(size_bytes, mBufferLock);
		memcpy(dest.Data(), inSpan.Data(), size_bytes);
		mBuffer.IncreaseSize(size_bytes, mBufferLock);
	}

	template <typename taType>
	void Write(const taType& inValue)
	{
		Write(Span(&inValue, 1));
	}

	void Write(StringView inStr)
	{
		Write((uint32)inStr.Size());
		Write(Span(inStr.Data(), inStr.Size()));
	}

	void WriteLabel(Span<const char> inLabel)
	{
		// Don't write the null terminator, we don't need it/don't read it back.
		Write(inLabel.SubSpan(0, inLabel.Size() - 1));
	}

	VMemArray<uint8> mBuffer = { 1'000'000'000, 256ull * 1024 };
	VMemArrayLock    mBufferLock;
};


// Helper class to read a binary file.
struct BinaryReader : NoCopy
{
	BinaryReader()
	{
		// Lock once to avoid the overhead of locking many times.
		// TODO make the mutex a template param to have a dummy one when not needed?
		mBufferLock = mBuffer.Lock();
	}

	// Read the entire file into the internal buffer.
	bool ReadFile(FILE* inFile);

	template <typename taType>
	void Read(Span<taType> outSpan)
	{
		if (mCurrentOffset + outSpan.SizeInBytes() > mBuffer.SizeRelaxed())
		{
			mError = true;
			return;
		}

		static_assert(cHasUniqueObjectRepresentations<taType>); // Make sure there's no padding inside that type.

		memcpy(outSpan.Data(), mBuffer.Begin() + mCurrentOffset, outSpan.SizeInBytes());
		mCurrentOffset += outSpan.SizeInBytes();
	}

	template <typename taType>
	void Read(taType& outValue)
	{
		Read(Span(&outValue, 1));
	}

	void Read(TempString& outStr)
	{
		uint32 size = 0;
		Read(size);

		// Reserve enough space (and null terminate).
		outStr.Resize(size);

		// Read the string.
		Read(Span(outStr.Data(), size));

		gAssert(*outStr.End() == 0);
	}

	[[nodiscard]] StringView Read(StringPool& ioStringPool)
	{
		uint32 size = 0;
		Read(size);

		MutStringView buffer = ioStringPool.Allocate(size);
		Read(Span(buffer.Data(), (int)size));

		return buffer;
	}

	void Skip(size_t inSizeInBytes)
	{
		if (mCurrentOffset + inSizeInBytes > mBuffer.SizeRelaxed())
		{
			mError = true;
			return;
		}

		mCurrentOffset += inSizeInBytes;
	}

	void SkipString()
	{
		uint32 size = 0;
		Read(size);
		Skip(size);
	}

	template <size_t taSize>
	bool ExpectLabel(const char (& inLabel)[taSize])
	{
		gAssert(inLabel[taSize - 1] == 0);
		constexpr int cLabelSize = taSize - 1; // Ignore null terminator

		char read_label[16];
		static_assert(cLabelSize <= gElemCount(read_label));
		Read(Span(read_label, cLabelSize));

		if (memcmp(inLabel, read_label, cLabelSize) != 0)
		{
			gAppLogError(R"(Expected label "%s" not found, file corrupted.)", inLabel);
			mError = true;
		}

		return !mError; // This will return false if there was an error before, even if the label is correct. That's on purpose, we use this to early out.
	}

	VMemArray<uint8> mBuffer = { 1'000'000'000, 256ull * 1024 };
	VMemArrayLock    mBufferLock;
	size_t           mCurrentOffset = 0;
	bool             mError = false;
};