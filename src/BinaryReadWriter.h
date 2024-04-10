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
		static_assert(std::has_unique_object_representations_v<taType>); // Don't write padding into the file.

		size_t size_bytes = inSpan.size_bytes();

		Span dest = mBuffer.EnsureCapacity(size_bytes, mBufferLock);
		memcpy(dest.data(), inSpan.data(), size_bytes);
		mBuffer.IncreaseSize(size_bytes, mBufferLock);
	}

	template <typename taType>
	void Write(const taType& inValue)
	{
		Write(Span(&inValue, 1));
	}

	void Write(StringView inStr)
	{
		Write((uint16)inStr.size());
		Write(Span(inStr));
	}

	void WriteLabel(Span<const char> inLabel)
	{
		// Don't write the null terminator, we don't need it/don't read it back.
		Write(inLabel.subspan(0, inLabel.size() - 1));
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
		if (mCurrentOffset + outSpan.size_bytes() > mBuffer.SizeRelaxed())
		{
			mError = true;
			return;
		}

		static_assert(std::has_unique_object_representations_v<taType>); // Make sure there's no padding inside that type.

		memcpy(outSpan.data(), mBuffer.Begin() + mCurrentOffset, outSpan.size_bytes());
		mCurrentOffset += outSpan.size_bytes();
	}

	template <typename taType>
	void Read(taType& outValue)
	{
		Read(Span(&outValue, 1));
	}

	template <size_t taSize>
	void Read(TempString<taSize>& outStr)
	{
		uint16 size = 0;
		Read(size);

		if (size > outStr.cCapacity - 1)
		{
			gAssert(false);
			gApp.LogError("BinaryReader tried to read a string of size {} in a TempString{}, it does not fit!", size, outStr.cCapacity);
			mError = true;

			// Skip the string instead of reading it.
			Skip(size);
		}
		else
		{
			outStr.mSize = size;
			outStr.mBuffer[size] = 0;
			Read(Span(outStr.mBuffer, size));
		}
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
			// TODO log an error here
			mError = true;
		}

		return !mError; // This will return false if there was an error before, even if the label is correct. That's on purpose, we use this to early out.
	}

	VMemArray<uint8> mBuffer = { 1'000'000'000, 256ull * 1024 };
	VMemArrayLock    mBufferLock;
	size_t           mCurrentOffset = 0;
	bool             mError = false;
};