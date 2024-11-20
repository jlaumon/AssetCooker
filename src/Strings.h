/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"

#include <span>
#include <string_view>
#include <string>
#include <optional>
#include "fmt/format.h"

#include <Bedrock/StringView.h>
#include <Bedrock/String.h>

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
		StringView string_view(Data(), Size());

		// Don't include the null terminator if there's one.
		if (!string_view.Empty() && string_view.Back() == 0)
			string_view.RemoveSuffix(1);

		return string_view;
	}
};

template<int taSize> struct FixedString;


// WStringView.
using WStringView = std::wstring_view;

// Typedefs for Optional StringViews.
using OptionalStringView = Optional<StringView>;
using OptionalWStringView = Optional<WStringView>;


// Fixed size string. Size does not include null terminator.
template<int taSize>
struct FixedString : NoCopy // No copy for now, should not be needed on temporary strings.
{
	static constexpr int cCapacity = taSize;

	FixedString()
	{
		mBuffer[0] = 0;
		mSize      = 0;
	}

	// Constructor that also formats the string.
	template <typename... taArgs> FixedString(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs) : FixedString() { Format(inFmt, std::forward<taArgs>(inArgs)...); }
	FixedString(StringView inFmt, fmt::format_args inArgs) : FixedString() { Format(inFmt, inArgs); }

	// Constructor that sets a string.
	FixedString(StringView inString) : FixedString() { Set(inString); }

	template <typename... taArgs> void Format(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs);
	template <typename... taArgs> void AppendFormat(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs);
	void                               Format(StringView inFmt, fmt::format_args inArgs);
	void                               AppendFormat(StringView inFmt, fmt::format_args inArgs);
	void                               Set(StringView inString);
	void                               Append(StringView inString);
	FixedString&                       operator=(StringView inString) { Set(inString); return *this; }

	operator StringView() const { return { mBuffer, mSize }; }
	StringView                         AsStringView() const { return { mBuffer, mSize }; }
	Span<char>                         AsSpan() { return { mBuffer, mSize + 1 }; }
	const char*                        AsCStr() const { return mBuffer; }
	int                                Size() const { return mSize; }
	char                               operator[](size_t inIndex) const { gAssert(inIndex <= mSize); return mBuffer[inIndex]; }
	char*                              Data() { return mBuffer; }

	int			mSize = 0; // Size of the string, not including the null terminator.
	char        mBuffer[taSize];
};

using FixedString32  = FixedString<32>;
using FixedString64  = FixedString<64>;
using FixedString128 = FixedString<128>;
using FixedString256 = FixedString<256>;
using FixedString512 = FixedString<512>;



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


// Return a pointer to the end of a string view. StringView::end returns an iterator, and that's often annoying.
// TODO: replace by call to End(), it's not an iterator anymore
constexpr const char* gEndPtr(StringView inString)
{
	return inString.End();
}


// Transform the string to lower case in place.
void gToLowercase(MutStringView ioString);


// TODO: rename to gStringCopy? gAppend is misleading since it writes at the beginning, not the end
// Copy a string into a potentially larger one, and return a MutStringView for what remains.
// eg. next = gAppend(buffer, "hello") will write "hello" into buffer, and next will point after "hello".
constexpr MutStringView gAppend(MutStringView ioDest, const StringView inStr)
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
constexpr MutStringView gStringCopy(MutStringView ioDest, const StringView inStr) { return gAppend(ioDest, inStr); }

constexpr MutStringView gConcat(MutStringView ioDest, const StringView inStr)
{
	MutStringView remaining_buffer = gAppend(ioDest, inStr);

	return { ioDest.Data(), remaining_buffer.Data() };
}

// Copy multiple string into a potentially larger one, and return a MutStringView of what was written.
// eg. gConcat(buffer, "hello", "world") will write "helloworld" into buffer.
template <class ...taArgs>
constexpr MutStringView gConcat(MutStringView ioDest, const StringView inStr, taArgs... inArgs)
{
	MutStringView concatenated_str = gConcat(gAppend(ioDest, inStr), inArgs...);

	return { ioDest.Data(), concatenated_str.End() };
}

// Return true if this string view is null-terminated.
// All strings allocated should ALWAYS be null-terminated, this is just checking if this is a sub-string view.
constexpr bool gIsNullTerminated(StringView inString)
{
	return *gEndPtr(inString) == 0;
}

// Formatter for StringView.
// This also makes formatting work for MutStringView and span<char> since StringView is implicity constructible from that.
template <> struct fmt::formatter<StringView> : fmt::formatter<fmt::string_view>
{
	auto format(StringView inStringView, format_context& ioCtx) const
	{
		return fmt::formatter<fmt::string_view>::format(fmt::string_view(inStringView.Data(), inStringView.Size()), ioCtx);
	}
};

// Formatter for FixedString.
template<int taSize>
struct fmt::formatter<FixedString<taSize>> : fmt::formatter<fmt::string_view>
{
	auto format(const FixedString<taSize>& inTempString, format_context& ioCtx) const
	{
		return fmt::formatter<fmt::string_view>::format(fmt::string_view(inTempString.mBuffer, inTempString.mSize), ioCtx);
	}
};

// Formatter for String.
template <> struct fmt::formatter<String> : fmt::formatter<fmt::string_view>
{
	auto format(const String& inString, format_context& ioCtx) const
	{
		return fmt::formatter<fmt::string_view>::format(fmt::string_view(inString.Data(), inString.Size()), ioCtx);
	}
};

// Helper type to format sizes in bytes.
enum class SizeInBytes : int64;

// Formatter for SizeInBytes.
template <> struct fmt::formatter<SizeInBytes> : fmt::formatter<fmt::string_view>
{
	auto format(SizeInBytes inBytes, format_context& ioCtx) const
	{
		if ((int64)inBytes < 10_KiB)
			return fmt::format_to(ioCtx.out(), "{} B", (int64)inBytes);
		else if ((int64)inBytes < 10_MiB)
			return fmt::format_to(ioCtx.out(), "{} KiB", (int64)inBytes / 1_KiB);
		else if ((int64)inBytes < 10_GiB)
			return fmt::format_to(ioCtx.out(), "{} MiB", (int64)inBytes / 1_MiB);
		else
			return fmt::format_to(ioCtx.out(), "{} GiB", (int64)inBytes / 1_GiB);
	}
};


// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
OptionalStringView gWideCharToUtf8(std::wstring_view inWString, MutStringView ioBuffer);

// Convert utf8 string to wide char. Always returns a null terminated string. Return an empty string on failure.
OptionalWStringView gUtf8ToWideChar(StringView inString, Span<wchar_t> ioBuffer);


// Helper to format a string into a fixed size buffer.
inline StringView gFormatV(MutStringView ioBuffer, StringView inFmt, fmt::format_args inArgs)
{
	// Assert if destination isn't large enough, but still make sure we don't overflow.
	size_t dest_available_size = ioBuffer.Size() - 1; // Keep 1 for null terminator.

	auto result = fmt::vformat_to_n(ioBuffer.Data(), dest_available_size, fmt::string_view(inFmt.Data(), inFmt.Size()), inArgs);
	*result.out = 0; // Add the null terminator.
	gAssert(result.size <= dest_available_size);

	return { ioBuffer.Data(), result.out };
}

template<typename... taArgs>
StringView gFormat(MutStringView ioBuffer, fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs)
{
	return gFormatV(ioBuffer, StringView(inFmt.get().data(), (int)inFmt.get().size()), fmt::make_format_args(inArgs...));
}


template <int taSize>
template <typename... taArgs> void FixedString<taSize>::Format(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs)
{
	StringView str_view = gFormat(mBuffer, inFmt, std::forward<taArgs>(inArgs)...);
	mSize               = str_view.Size();
}


template <int taSize>
template <typename... taArgs> void FixedString<taSize>::AppendFormat(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs)
{
	StringView str_view = gFormat(Span(mBuffer).SubSpan(mSize), inFmt, std::forward<taArgs>(inArgs)...);
	mSize               += str_view.Size();
}


template <int taSize>
void FixedString<taSize>::Format(StringView inFmt, fmt::format_args inArgs)
{
	StringView str_view = gFormatV(mBuffer, inFmt, inArgs);
	mSize               = str_view.Size();
}


template <int taSize>
void FixedString<taSize>::AppendFormat(StringView inFmt, fmt::format_args inArgs)
{
	StringView str_view = gFormatV(Span(mBuffer).SubSpan(mSize), inFmt, inArgs);
	mSize               += str_view.Size();
}


template <int taSize>
void FixedString<taSize>::Set(StringView inString)
{
	MutStringView str_view = gConcat(mBuffer, inString);
	mSize                  = str_view.Size();
}

template <int taSize>
void FixedString<taSize>::Append(StringView inString)
{
	gAssert(mSize + inString.Size() < cCapacity); // String will be cropped!

	MutStringView appended_str = gConcat(MutStringView(mBuffer).SubSpan(mSize), inString);

	gAssert(appended_str.Back() == 0);
	mSize += appended_str.Size() - 1;
}



// Hash for StringView.
template <> struct ankerl::unordered_dense::hash<StringView>
{
    using is_avalanching = void;
    uint64 operator()(const StringView& inString) const
	{
        return detail::wyhash::hash(inString.Data(), inString.Size());
    }
};

inline uint64 gHash(StringView inString) { return ankerl::unordered_dense::detail::wyhash::hash(inString.Data(), inString.Size()); }


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