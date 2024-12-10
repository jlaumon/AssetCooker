/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"

#include <string_view>
#include <optional>

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

template<int taSize> struct FixedString;


// WStringView.
using WStringView = std::wstring_view;

// Typedefs for Optional StringViews.
using OptionalWStringView = Optional<WStringView>;


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


// TODO: rename to gStringCopy? gAppend is misleading since it writes at the beginning, not the end
// Copy a string into a potentially larger one, and return a MutStringView for what remains.
// eg. next = gAppend(buffer, "hello") will write "hello" into buffer, and next will point after "hello".
constexpr MutStringView gAppend2(MutStringView ioDest, const StringView inStr)
{
	// Assert if destination isn't large enough, but still make sure we don't overflow.
	int dest_available_size = ioDest.Size() - 1; // Keep 1 for null terminator.
	gAssert(inStr.Size() <= dest_available_size);
	int copy_size = gMin(inStr.Size(), dest_available_size);

	for (int i = 0; i < copy_size; i++)
		ioDest[i] = inStr[i];

	// Add a null-terminator.
	ioDest[copy_size] = 0;

	return { ioDest.Data() + copy_size, ioDest.Size() - copy_size };
}
constexpr MutStringView gStringCopy(MutStringView ioDest, const StringView inStr) { return gAppend2(ioDest, inStr); }


template <class ...taArgs>
void gAppend(TempString& outString, taArgs&&... inArgs)
{
	(outString.Append(gForward<taArgs>(inArgs)), ...);
}

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
OptionalWStringView gUtf8ToWideChar(StringView inString, Span<wchar_t> ioBuffer);


// Hash for StringView.
template <> struct ankerl::unordered_dense::hash<StringView>
{
    using is_avalanching = void;
    uint64 operator()(const StringView& inString) const
	{
        return detail::wyhash::hash(inString.Data(), inString.Size());
    }
};


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