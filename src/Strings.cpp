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
void gToLowercase(MutStringView ioString)
{
	_mbslwr_s((unsigned char*)ioString.Data(), ioString.Size());
}


// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
OptionalStringView gWideCharToUtf8(WStringView inWString, MutStringView ioBuffer)
{
	// If a null terminator is included in the source, WideCharToMultiByte will also add it in the destination.
	// Otherwise we'll need to add it manually.
	bool source_is_null_terminated = (!inWString.empty() && inWString.back() == 0);

	int available_bytes = (int)ioBuffer.Size();

	// If we need to add a null terminator, reserve 1 byte for it.
	if (source_is_null_terminated)
		available_bytes--;

	int written_bytes = WideCharToMultiByte(CP_UTF8, 0, inWString.data(), (int)inWString.size(), ioBuffer.Data(), available_bytes, nullptr, nullptr);

	if (written_bytes == 0 && !inWString.empty())
		return {}; // Failed to convert.

	if (written_bytes == available_bytes)
		return {}; // Might be cropped, consider failed.

	// If there isn't a null terminator, add it.
	if (!source_is_null_terminated)
		ioBuffer[written_bytes] = 0;
	else
	{
		gAssert(ioBuffer[written_bytes - 1] == 0); // Should already have a null terminator.
		written_bytes--; // Don't count the null terminator in the returned string view.
	}

	return StringView(ioBuffer.Data(), written_bytes);
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

