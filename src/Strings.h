#pragma once

#include "Core.h"

#include <span>
#include <string_view>
#include <string>
#include <format>
#include <optional>


// Mutable StringView. Size includes the null-terminator.
using MutStringView = std::span<char>;

// Mutable String. Allocates its own memory; size doesn't include the null-terminator.
using String		= std::string;

// StringView class. Size doesn't include the null-terminator.
// Not just a std::string_view because an implicit constructor from MutStringView.
struct StringView : public std::string_view
{
	// Inherit string_view's constructors.
	using std::string_view::string_view;
	constexpr StringView(const char* inString, size_t inSize) : std::string_view(inString, inSize) {}

	// Bunch of defaults.
	constexpr StringView(const StringView&)				= default;
	constexpr StringView(StringView&&)					= default;
	constexpr ~StringView()								= default;
	constexpr StringView& operator=(const StringView&)	= default;
	constexpr StringView& operator=(StringView&&)		= default;

	// Add implicit constructors from std::string_view, String and MutStringView.
	constexpr StringView(std::string_view inString)		: std::string_view(inString) {}
	constexpr StringView(const String& inString)		: std::string_view(inString) {}
	constexpr StringView(MutStringView inString)		: std::string_view(inString.data(), inString.size())
	{
		// Don't include the null terminator if there's one.
		if (!empty() && back() == 0)
			remove_suffix(1);
	}
};

// Return true if inString1 and inString2 are identical.
constexpr bool gIsEqual(StringView inString1, StringView inString2)
{
	return inString1 == inString2;
}

// Return true if inString starts with inStart.
constexpr bool gStartsWith(StringView inString, StringView inStart)
{
	return std::string_view(inString.data(), inString.size()).starts_with(std::string_view(inStart.data(), inStart.size()));
}

// Return true if inString ends with inEnd.
constexpr bool gEndsWith(StringView inString, StringView inEnd)
{
	return std::string_view(inString.data(), inString.size()).ends_with(std::string_view(inEnd.data(), inEnd.size()));
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
constexpr const char* gEndPtr(StringView inString)
{
	return inString.data() + inString.size();
}


// TODO: rename to gStringCopy? gAppend is misleading since it writes at the beginning, not the end
// Copy a string into a potentially larger one, and return a MutStringView for what remains.
// eg. next = gAppend(buffer, "hello") will write "hello" into buffer, and next will point after "hello".
constexpr MutStringView gAppend(MutStringView ioDest, const StringView inStr)
{
	// Assert if destination isn't large enough, but still make sure we don't overflow.
	size_t dest_available_size = ioDest.size() - 1; // Keep 1 for null terminator.
	gAssert(inStr.size() <= dest_available_size);
	size_t copy_size = gMin(inStr.size(), dest_available_size);

	for (size_t i = 0; i < copy_size; i++)
		ioDest[i] = inStr[i];

	// Add a null-terminator.
	ioDest[copy_size] = 0;

	return { ioDest.data() + copy_size, ioDest.size() - copy_size };
}


constexpr StringView gConcat(MutStringView ioDest, const StringView inStr)
{
	StringView remaining_buffer = gAppend(ioDest, inStr);

	return { ioDest.data(), remaining_buffer.data() };
}

// Copy multiple string into a potentially larger one, and return a MutStringView for what remains.
// eg. gAppend(buffer, "hello", "world") will write "helloworld" into buffer.
template <class ...taArgs>
constexpr StringView gConcat(MutStringView ioDest, const StringView inStr, taArgs... inArgs)
{
	StringView concatenated_str = gConcat(gAppend(ioDest, inStr), inArgs...);

	return { ioDest.data(), gEndPtr(concatenated_str) };
}

// Return true if this string view is null-terminated.
// All strings allocated should ALWAYS be null-terminated, this is just checking if this is a sub-string view.
constexpr bool gIsNullTerminated(StringView inString)
{
	return *gEndPtr(inString) == 0;
}

// Formatter for StringView.
// This also makes formatting work for MutStringView and span<char> since StringView is implicity constructible from that.
template <> struct std::formatter<StringView> : std::formatter<std::string_view>
{
	auto format(StringView inStringView, format_context& ioCtx) const
	{
		return std::formatter<std::string_view>::format(std::string_view(inStringView.data(), inStringView.size()), ioCtx);
	}
};

// Helper type to format sizes in bytes.
enum class SizeInBytes : size_t;

// Formatter for SizeInBytes.
template <> struct std::formatter<SizeInBytes> : std::formatter<std::string_view>
{
	auto format(SizeInBytes inBytes, format_context& ioCtx) const
	{
		if ((size_t)inBytes < 10_KiB)
			return std::format_to(ioCtx.out(), "{} B", (size_t)inBytes);
		else if ((size_t)inBytes < 10_MiB)
			return std::format_to(ioCtx.out(), "{} KiB", (size_t)inBytes / 1_KiB);
		else if ((size_t)inBytes < 10_GiB)
			return std::format_to(ioCtx.out(), "{} MiB", (size_t)inBytes / 1_MiB);
		else
			return std::format_to(ioCtx.out(), "{} GiB", (size_t)inBytes / 1_GiB);
	}
};


// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
std::optional<StringView> gWideCharToUtf8(std::wstring_view inWString, MutStringView ioBuffer);

// Convert utf8 string to wide char. Always returns a null terminated string. Return an empty string on failure.
std::optional<std::wstring_view> gUtf8ToWideChar(StringView inString, std::span<wchar_t> ioBuffer);


