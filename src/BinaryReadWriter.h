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
	// Write the internal buffer to this file.
	bool WriteFile(FILE* ioFile);

	template <typename taType>
	void Write(Span<const taType> inSpan)
	{
		static_assert(cHasUniqueObjectRepresentations<taType>); // Don't write padding into the file.

		int size_bytes = inSpan.SizeInBytes();

		// Resize the buffer.
		mBuffer.Resize(mBuffer.Size() + size_bytes, EResizeInit::NoZeroInit);

		// Write the data.
		gMemCopy(mBuffer.End() - size_bytes, inSpan.Data(), size_bytes);
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

	VMemVector<uint8> mBuffer;
};


// Helper class to read a binary file.
struct BinaryReader : NoCopy
{
	// Read the entire file into the internal buffer.
	bool ReadFile(FILE* inFile);

	template <typename taType>
	void Read(Span<taType> outSpan)
	{
		if (mCurrentOffset + outSpan.SizeInBytes() > mBuffer.Size())
		{
			mError = true;
			return;
		}

		static_assert(cHasUniqueObjectRepresentations<taType>); // Make sure there's no padding inside that type.

		gMemCopy(outSpan.Data(), mBuffer.Begin() + mCurrentOffset, outSpan.SizeInBytes());
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

	void Skip(int inSizeInBytes)
	{
		if (mCurrentOffset + inSizeInBytes > mBuffer.Size())
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

	template <int taSize>
	bool ExpectLabel(const char (& inLabel)[taSize])
	{
		gAssert(inLabel[taSize - 1] == 0);
		constexpr int cLabelSize = taSize - 1; // Ignore null terminator

		char read_label[16];
		static_assert(cLabelSize <= gElemCount(read_label));
		Read(Span(read_label, cLabelSize));

		if (gMemCmp(inLabel, read_label, cLabelSize) != 0)
		{
			gAppLogError(R"(Expected label "%s" not found, file corrupted.)", inLabel);
			mError = true;
		}

		return !mError; // This will return false if there was an error before, even if the label is correct. That's on purpose, we use this to early out.
	}

	VMemVector<uint8> mBuffer;
	int               mCurrentOffset = 0;
	bool              mError         = false;
};