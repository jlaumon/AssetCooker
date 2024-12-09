/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Strings.h"
#include <Bedrock/Test.h>

#include <mbstring.h>

#include "win32/misc.h"

// Same as gIsEqual but case-insensitive.
bool gIsEqualNoCase(StringView inString1, StringView inString2)
{
	if (inString1.Size() != inString2.Size())
		return false;

	return _mbsnicmp((const unsigned char*)inString1.Data(), (const unsigned char*)inString2.Data(), inString1.Size()) == 0;
}

// Same as gStartsWith but case-insensitive.
bool gStartsWithNoCase(StringView inString, StringView inStart)
{
	if (inString.Size() < inStart.Size())
		return false;

	return _mbsnicmp((const unsigned char*)inString.Data(), (const unsigned char*)inStart.Data(), inStart.Size()) == 0;
}

// Same as gEndsWith but case-insensitive.
bool gEndsWithNoCase(StringView inString, StringView inEnd)
{
	if (inString.Size() < inEnd.Size())
		return false;

	return _mbsnicmp((const unsigned char*)inString.Data() + inString.Size() - inEnd.Size(), (const unsigned char*)inEnd.Data(), inEnd.Size()) == 0;
}


// Transform the string to lower case in place.
void Details::ToLowercase(Span<char> ioString)
{
	_mbslwr_s((unsigned char*)ioString.Data(), ioString.Size());
}


// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
TempString gWideCharToUtf8(WStringView inWString)
{
	TempString out_str;

	// Reserve enough buffer. 4 char per wchar should be enough in all cases. But make it at least 4K just in case.
	out_str.Reserve(gMin((int)inWString.size() * 4, 4096));

	int available_bytes = out_str.Capacity() - 1;

	int written_bytes = WideCharToMultiByte(CP_UTF8, 0, inWString.data(), (int)inWString.size(), out_str.Data(), available_bytes, nullptr, nullptr);

	if (written_bytes == 0 && !inWString.empty())
		return {}; // Failed to convert.

	if (written_bytes == available_bytes)
		return {}; // Might be cropped, consider failed.

	// If there's already a null terminator, don't count it in the size.
	if (out_str.Begin()[written_bytes - 1] == 0)
		written_bytes--;

	// Set the correct size and null terminate.
	out_str.Resize(written_bytes);
	out_str.ShrinkToFit();
	
	return out_str;
}


// Convert utf8 string to wide char. Always returns a null terminated string. Return an empty string on failure.
OptionalWStringView gUtf8ToWideChar(StringView inString, Span<wchar_t> ioBuffer)
{
	// Reserve 1 byte for the null terminator.
	int available_wchars = ioBuffer.Size() - 1;

	int written_wchars = MultiByteToWideChar(CP_UTF8, 0, inString.Data(), inString.Size(), ioBuffer.Data(), available_wchars);

	if (written_wchars == 0 && !inString.Empty())
		return {}; // Failed to convert.

	if (written_wchars == available_wchars)
		return {}; // Might be cropped, consider failed.

	// Add the null terminator.
	ioBuffer[written_wchars] = 0;

	return WStringView{ ioBuffer.Data(), (size_t)written_wchars };
}

