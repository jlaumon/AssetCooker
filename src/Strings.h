#pragma once

#include "Core.h"

#include <span>
#include <string_view>
#include <string>
#include <format>

// StringView class. Basically a std::span<const char>.
// Not just a std::string_view because an implicit constructor from MutStringView.
struct StringView : public std::string_view
{
	// Inherit string_view's constructors.
	using std::string_view::string_view;

	// Bunch of defaults.
	constexpr StringView(const StringView&)				= default;
	constexpr StringView(StringView&&)					= default;
	constexpr ~StringView()								= default;
	constexpr StringView& operator=(const StringView&)	= default;
	constexpr StringView& operator=(StringView&&)		= default;

	// Add constructor from std::span<char> (MutStringView) and std::string_view.
	constexpr StringView(std::span<char> inString)		: std::string_view(inString.data(), inString.size()) {}
	constexpr StringView(std::string_view inString)		: std::string_view(inString) {}
	constexpr StringView(const std::string& inString)	: std::string_view(inString) {}
};

// Mutable StringView.
using MutStringView = std::span<char>;

// MutableString. Allocates its own memory.
using String		= std::string;

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

// Return true if characters is an alphabetical letter.
constexpr bool gIsAlpha(char inChar) { return inChar >= 'A' && inChar < 'z'; }

// Copy a string into a potentially larger one, and return a MutStringView for what remains.
// eg. gAppend(gAppend(buffer, "hello"), "world") will write "helloworld" into buffer.
constexpr MutStringView gAppend(MutStringView ioDest, const StringView inStr)
{
	gAssert(inStr.size() <= ioDest.size());

	for (size_t i = 0; i < inStr.size(); i++)
		ioDest[i] = inStr[i];

	return { ioDest.data() + inStr.size(), ioDest.size() - inStr.size() };
}

// Return true if this string view is null-terminated.
// All strings allocated should ALWAYS be null-terminated, this is just checking if this is a sub-string view.
constexpr bool gIsNullTerminated(StringView inString)
{
	return *(inString.data() + inString.size()) == 0;
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