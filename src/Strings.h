/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"

#include <optional>
#include <string_view>

#include <Strings.h>
#include <Bedrock/StringView.h>
#include <Bedrock/String.h>
#include <Bedrock/StringFormat.h>

constexpr bool gIsNullTerminated(StringView inString);


// Mutable StringView. Size is fixed but content is mutable. Size includes the null-terminator.
struct MutStringView : Span<char>
{
	MutStringView() = default;
	MutStringView(const MutStringView&) = default;
	MutStringView(Span inSpan) : Span(inSpan) {}
	using Span::Span;

	operator StringView() const
	{
		// Don't include the null terminator if there's one.
		if (!Empty() && Back() == 0)
			return StringView(Data(), Size() - 1);
		else
			return StringView(Data(), Size());
	}
};


// WStringView.
using WStringView = std::wstring_view;


// Remove any inCharacters at the end of ioString
constexpr void gRemoveTrailing(StringView& ioString, StringView inCharacters)
{
	int pos = ioString.FindLastNotOf(inCharacters);
	ioString = ioString.SubStr(0, pos + 1);
}

// Remove any inCharacters at the start of ioString
constexpr void gRemoveLeading(StringView& ioString, StringView inCharacters)
{
	int pos = ioString.FindFirstNotOf(inCharacters);

	if (pos == -1)
		ioString = {};
	else
		ioString = ioString.SubStr(pos);
}


// Return true if inString1 and inString2 are identical.
constexpr bool gIsEqual(StringView inString1, StringView inString2)
{
	return inString1 == inString2;
}

// Return true if inString starts with inStart.
// TODO: replace by call to method 
constexpr bool gStartsWith(StringView inString, StringView inStart)
{
	return inString.StartsWith(inStart);
}

// Return true if inString ends with inEnd.
// TODO: replace by call to method 
constexpr bool gEndsWith(StringView inString, StringView inEnd)
{
	return inString.EndsWith(inEnd);
}

// Return true if inString1 and inString2 are identical (case-insensitive).
bool gIsEqualNoCase(StringView inString1, StringView inString2);

// Return true if inString starts with inStart (case-insensitive).
bool gStartsWithNoCase(StringView inString, StringView inStart);

// Return true if inString ends with inEnd (case-insensitive).
bool gEndsWithNoCase(StringView inString, StringView inEnd);


// Return true if inChar is an alphabetical letter.
constexpr bool gIsAlpha(char inChar) { return inChar >= 'A' && inChar < 'z'; }


namespace Details
{
	void ToLowercase(Span<char> ioString);	// Needs to include the null terminator.
}

// Transform the string to lower case in place.
template <class taString> void gToLowercase(taString& ioString) { Details::ToLowercase(Span(ioString.Data(), ioString.Size() + 1)); }


// Copy a StringView into a fixed sized buffer.
inline StringView gStringCopy(MutStringView ioDest, StringView inStr)
{
	// Assert if destination isn't large enough, but still make sure we don't overflow.
	int dest_available_size = ioDest.Size() - 1; // Keep 1 for null terminator.
	gAssert(inStr.Size() <= dest_available_size);
	int copy_size = gMin(inStr.Size(), dest_available_size);

	gMemCopy(ioDest.Data(), inStr.Data(), copy_size);

	// Add a null-terminator.
	ioDest[copy_size] = 0;

	return { ioDest.Data(), copy_size };
}


// Append multiple strings to a TempString. Accepts any number of parameters.
template <class ...taArgs>
void gAppend(TempString& outString, taArgs&&... inArgs)
{
	(outString.Append(gForward<taArgs>(inArgs)), ...);
}

// Concatenate multiple strings into a TempString. Accepts any number of parameters.
// Eg. TempString str = gConcat("hello", "world", "!");
template <class ...taArgs>
TempString gConcat(taArgs&&... inArgs)
{
	TempString str;
	gAppend(str, gForward<taArgs>(inArgs)...);
	return str;
}


// Return true if this string view is null-terminated.
// All strings allocated should ALWAYS be null-terminated, this is just checking if this is a sub-string view.
constexpr bool gIsNullTerminated(StringView inString)
{
	return *inString.End() == 0;
}


// Format a size in bytes into an easy to read string.
inline TempString gFormatSizeInBytes(int64 inBytes)
{
	if (inBytes < 10_KiB)
		return gTempFormat("%lld B", inBytes);
	else if (inBytes < 10_MiB)
		return gTempFormat("%lld KiB", inBytes / 1_KiB);
	else if (inBytes < 10_GiB)
		return gTempFormat("%lld MiB", inBytes / 1_MiB);
	else
		return gTempFormat("%lld GiB", inBytes / 1_GiB);
}


// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
TempString gWideCharToUtf8(WStringView inWString);

// Convert utf8 string to wide char. Always returns a null terminated string. Return an empty string on failure.
WStringView gUtf8ToWideChar(StringView inString, Span<wchar_t> ioBuffer);



// Helper to turn back an StringView into an enum, assuming the right gToStringView exists for the enum and that its values go from 0 to _Count.
template <typename taEnumType>
bool gStringViewToEnum(StringView inStrValue, taEnumType& outValue)
{
	for (int i = 0; i < (int)taEnumType::_Count; ++i)
	{
		taEnumType value = (taEnumType)i;
		if (gIsEqualNoCase(inStrValue, gToStringView(value)))
		{
			outValue = value;
			return true;
		}
	}

	return false;
}

struct FormatColor
{
	uint8 mR	 = 0;
	uint8 mG	 = 0;
	uint8 mB	 = 0;
	bool  mValid = false;

	constexpr bool operator==(const FormatColor&) const = default;
};

struct FormatSpan
{
	StringView	mSpan;
	FormatColor mColor;
};

// Parse the string for ANSI escape sequences with color codes
void gParseANSIColors(StringView inStr, Vector<FormatSpan>& outSpans);
