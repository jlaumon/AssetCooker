/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Strings.h"
#include "Tests.h"

#include <mbstring.h>

#include "win32/misc.h"

// Same as gIsEqual but case-insensitive.
bool gIsEqualNoCase(StringView inString1, StringView inString2)
{
	if (inString1.size() != inString2.size())
		return false;

	return _mbsnicmp((const unsigned char*)inString1.data(), (const unsigned char*)inString2.data(), inString1.size()) == 0;
}

// Same as gStartsWith but case-insensitive.
bool gStartsWithNoCase(StringView inString, StringView inStart)
{
	if (inString.size() < inStart.size())
		return false;

	return _mbsnicmp((const unsigned char*)inString.data(), (const unsigned char*)inStart.data(), inStart.size()) == 0;
}

// Same as gEndsWith but case-insensitive.
bool gEndsWithNoCase(StringView inString, StringView inEnd)
{
	if (inString.size() < inEnd.size())
		return false;

	return _mbsnicmp((const unsigned char*)inString.data() + inString.size() - inEnd.size(), (const unsigned char*)inEnd.data(), inEnd.size()) == 0;
}


// Transform the string to lower case in place.
void gToLowercase(MutStringView ioString)
{
	_mbslwr_s((unsigned char*)ioString.data(), ioString.size());
}


// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
OptionalStringView gWideCharToUtf8(WStringView inWString, MutStringView ioBuffer)
{
	// If a null terminator is included in the source, WideCharToMultiByte will also add it in the destination.
	// Otherwise we'll need to add it manually.
	bool source_is_null_terminated = (!inWString.empty() && inWString.back() == 0);

	int available_bytes = (int)ioBuffer.size();

	// If we need to add a null terminator, reserve 1 byte for it.
	if (source_is_null_terminated)
		available_bytes--;

	int written_bytes = WideCharToMultiByte(CP_UTF8, 0, inWString.data(), (int)inWString.size(), ioBuffer.data(), available_bytes, nullptr, nullptr);

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

	return ioBuffer.subspan(0, written_bytes);
}

// Convert utf8 string to wide char. Always returns a null terminated string. Return an empty string on failure.
OptionalWStringView gUtf8ToWideChar(StringView inString, Span<wchar_t> ioBuffer)
{
	// If a null terminator is included in the source, WideCharToMultiByte will also add it in the destination.
	// Otherwise we'll need to add it manually.
	bool source_is_null_terminated = (!inString.empty() && inString.back() == 0);

	int available_wchars = (int)ioBuffer.size();

	// If we need to add a null terminator, reserve 1 byte for it.
	if (source_is_null_terminated)
		available_wchars--;

	int written_wchars = MultiByteToWideChar(CP_UTF8, 0, inString.data(), (int)inString.size(), ioBuffer.data(), available_wchars);

	if (written_wchars == 0 && !inString.empty())
		return {}; // Failed to convert.

	if (written_wchars == available_wchars)
		return {}; // Might be cropped, consider failed.

	// If there isn't a null terminator, add it.
	if (!source_is_null_terminated)
		ioBuffer[written_wchars] = 0;
	else
	{
		gAssert(ioBuffer[written_wchars - 1] == 0); // Should already have a null terminator.
		written_wchars--; // Don't count the null terminator in the returned string view.
	}

	return WStringView{ ioBuffer.data(), (size_t)written_wchars };
}


REGISTER_TEST("Strings")
{
	TempString32 str("test");
	TEST_TRUE(str.AsStringView() == "test");

	str.Append("test");
	TEST_TRUE(str.AsStringView() == "testtest");

	str.Set("oooo");
	TEST_TRUE(str.AsStringView() == "oooo");

	str.AppendFormat("{}", "zest");
	TEST_TRUE(str.AsStringView() == "oooozest");

	str.Format("{}", "best");
	TEST_TRUE(str.AsStringView() == "best");

	TEST_TRUE(gIsEqual("tata", "tata"));
	TEST_TRUE(gStartsWith("tatapoom", "tata"));
	TEST_TRUE(gEndsWith("tatapoom", "poom"));

	TEST_TRUE(gIsEqualNoCase("taTa", "TatA"));
	TEST_TRUE(gStartsWithNoCase("taTaPOOM", "TatA"));
	TEST_TRUE(gEndsWithNoCase("taTaPOOM", "pOom"));

	TEST_FALSE(gIsEqual("taTa", "TatA"));
	TEST_FALSE(gStartsWith("taTaPOOM", "TatA"));
	TEST_FALSE(gEndsWith("taTaPOOM", "pOom"));
};

