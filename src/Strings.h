#pragma once

#include "Core.h"

#include <span>
#include <string_view>
#include <string>
#include <optional>
#include "fmt/format.h"

struct StringView;
constexpr bool gIsNullTerminated(StringView inString);


// Mutable StringView. Size is fixed but content is mutable. Size includes the null-terminator.
using MutStringView = Span<char>;

// Mutable String. Allocates its own memory; size doesn't include the null-terminator.
using String		= std::string;

template<size_t taSize> struct TempString;


// StringView class. Size doesn't include the null-terminator.
// Not just a std::string_view because an implicit constructor from MutStringView.
struct StringView : public std::string_view
{
	// Inherit string_view's constructors.
	using std::string_view::string_view;
	constexpr StringView(const char* inString, size_t inSize) : std::string_view(inString, inSize)
	{
		// Don't include the null terminator if there's one.
		if (!empty() && back() == 0)
			remove_suffix(1);
	}

	// Bunch of defaults.
	constexpr StringView(const StringView&)				= default;
	constexpr StringView(StringView&&)					= default;
	constexpr ~StringView()								= default;
	constexpr StringView& operator=(const StringView&)	= default;
	constexpr StringView& operator=(StringView&&)		= default;

	// Add implicit constructors from std::string_view, String and MutStringView.
	constexpr StringView(fmt::string_view inString)		: std::string_view(inString.data(), inString.size()) {}
	constexpr StringView(std::string_view inString)		: std::string_view(inString) {}
	constexpr StringView(const String& inString)		: std::string_view(inString) {}
	constexpr StringView(MutStringView inString)		: std::string_view(inString.data(), inString.size())
	{
		// Don't include the null terminator if there's one.
		if (!empty() && back() == 0)
			remove_suffix(1);
	}

	// Implicit contructor from u8 literals. Is it a good idea? Who knows.
	template<size_t taSize>
	constexpr StringView(const char8_t (&inArray)[taSize])	: std::string_view((const char*)inArray, taSize)
	{
		// Don't include the null terminator if there's one.
		if (!empty() && back() == 0)
			remove_suffix(1);
	}

	// Implicit consturctors from TempString.
	template<size_t taSize> constexpr StringView(const TempString<taSize>& inString);

	// All our strings are null terminated so it's "safe", but assert in case it's a sub-string view.
	constexpr const char* AsCStr() const
	{
		gAssert(gIsNullTerminated(*this));
		return data();
	}

	constexpr bool Contains(StringView inOther) const { return find(inOther) != npos; }
};

// WStringView.
using WStringView = std::wstring_view;

// Typedefs for Optional StringViews.
using OptionalStringView = Optional<StringView>;
using OptionalWStringView = Optional<WStringView>;


// Fixed size string meant for temporaries. Size does not include null terminator.
template<size_t taSize>
struct TempString : NoCopy // No copy for now, should not be needed on temporary strings.
{
	static constexpr size_t cCapacity = taSize;

	TempString()
	{
		mBuffer[0] = 0;
		mSize      = 0;
	}

	// Constructor that also formats the string.
	template <typename... taArgs> TempString(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs) : TempString() { Format(inFmt, std::forward<taArgs>(inArgs)...); }
	TempString(StringView inFmt, fmt::format_args inArgs) : TempString() { Format(inFmt, inArgs); }

	// Constructor that sets a string.
	TempString(StringView inString) : TempString() { Set(inString); }

	template <typename... taArgs> void Format(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs);
	template <typename... taArgs> void AppendFormat(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs);
	void                               Format(StringView inFmt, fmt::format_args inArgs);
	void                               AppendFormat(StringView inFmt, fmt::format_args inArgs);
	void                               Set(StringView inString);
	void                               Append(StringView inString);
	TempString&                        operator=(StringView inString) { Set(inString); return *this; }

	StringView                         AsStringView() const { return { mBuffer, mSize }; }
	Span<char>                         AsSpan() { return { mBuffer, mSize + 1 }; }
	const char*                        AsCStr() const { return mBuffer; }
	size_t                             Size() const { return mSize; }
	char                               operator[](size_t inIndex) const { gAssert(inIndex <= mSize); return mBuffer[inIndex]; }

	size_t      mSize = 0; // Size of the string, not including the null terminator.
	char        mBuffer[taSize];
};

using TempString32  = TempString<32>;
using TempString64  = TempString<64>;
using TempString128 = TempString<128>;
using TempString256 = TempString<256>;
using TempString512 = TempString<512>;


template<size_t taSize> constexpr StringView::StringView(const TempString<taSize>& inString) : std::string_view(inString.mBuffer, inString.mSize) {}


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
constexpr MutStringView gStringCopy(MutStringView ioDest, const StringView inStr) { return gAppend(ioDest, inStr); }

constexpr MutStringView gConcat(MutStringView ioDest, const StringView inStr)
{
	StringView remaining_buffer = gAppend(ioDest, inStr);

	return { ioDest.data(), remaining_buffer.data() };
}

// Copy multiple string into a potentially larger one, and return a MutStringView of what was written.
// eg. gConcat(buffer, "hello", "world") will write "helloworld" into buffer.
template <class ...taArgs>
constexpr MutStringView gConcat(MutStringView ioDest, const StringView inStr, taArgs... inArgs)
{
	MutStringView concatenated_str = gConcat(gAppend(ioDest, inStr), inArgs...);

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
template <> struct fmt::formatter<StringView> : fmt::formatter<fmt::string_view>
{
	auto format(StringView inStringView, format_context& ioCtx) const
	{
		return fmt::formatter<fmt::string_view>::format(fmt::string_view(inStringView.data(), inStringView.size()), ioCtx);
	}
};

// Formatter for TempString.
template<size_t taSize>
struct fmt::formatter<TempString<taSize>> : fmt::formatter<fmt::string_view>
{
	auto format(const TempString<taSize>& inTempString, format_context& ioCtx) const
	{
		return fmt::formatter<fmt::string_view>::format(fmt::string_view(inTempString.mBuffer, inTempString.mSize), ioCtx);
	}
};

// Helper type to format sizes in bytes.
enum class SizeInBytes : size_t;

// Formatter for SizeInBytes.
template <> struct fmt::formatter<SizeInBytes> : fmt::formatter<fmt::string_view>
{
	auto format(SizeInBytes inBytes, format_context& ioCtx) const
	{
		if ((size_t)inBytes < 10_KiB)
			return fmt::format_to(ioCtx.out(), "{} B", (size_t)inBytes);
		else if ((size_t)inBytes < 10_MiB)
			return fmt::format_to(ioCtx.out(), "{} KiB", (size_t)inBytes / 1_KiB);
		else if ((size_t)inBytes < 10_GiB)
			return fmt::format_to(ioCtx.out(), "{} MiB", (size_t)inBytes / 1_MiB);
		else
			return fmt::format_to(ioCtx.out(), "{} GiB", (size_t)inBytes / 1_GiB);
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
	size_t dest_available_size = ioBuffer.size() - 1; // Keep 1 for null terminator.

	auto result = fmt::vformat_to_n(ioBuffer.data(), dest_available_size, inFmt, inArgs);
	*result.out = 0; // Add the null terminator.
	gAssert(result.size <= dest_available_size);

	return { ioBuffer.data(), result.out };
}

template<typename... taArgs>
StringView gFormat(MutStringView ioBuffer, fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs)
{
	return gFormatV(ioBuffer, inFmt.get(), fmt::make_format_args(inArgs...));
}


template <size_t taSize>
template <typename... taArgs> void TempString<taSize>::Format(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs)
{
	StringView str_view = gFormat(mBuffer, inFmt, std::forward<taArgs>(inArgs)...);
	mSize               = str_view.size();
}


template <size_t taSize>
template <typename... taArgs> void TempString<taSize>::AppendFormat(fmt::format_string<taArgs...> inFmt, taArgs&&... inArgs)
{
	StringView str_view = gFormat(Span(mBuffer).subspan(mSize), inFmt, std::forward<taArgs>(inArgs)...);
	mSize               += str_view.size();
}


template <size_t taSize>
void TempString<taSize>::Format(StringView inFmt, fmt::format_args inArgs)
{
	StringView str_view = gFormatV(mBuffer, inFmt, inArgs);
	mSize               = str_view.size();
}


template <size_t taSize>
void TempString<taSize>::AppendFormat(StringView inFmt, fmt::format_args inArgs)
{
	StringView str_view = gFormatV(Span(mBuffer).subspan(mSize), inFmt, inArgs);
	mSize               += str_view.size();
}


template <size_t taSize>
void TempString<taSize>::Set(StringView inString)
{
	StringView str_view = gConcat(mBuffer, inString);
	mSize               = str_view.size();
}

template <size_t taSize>
void TempString<taSize>::Append(StringView inString)
{
	gAssert(mSize + inString.size() < cCapacity); // String will be cropped!

	StringView appended_str = gConcat(MutStringView(mBuffer).subspan(mSize), inString);
	mSize += appended_str.size();
}



// Hash for StringView.
template <> struct ankerl::unordered_dense::hash<StringView>
{
    using is_avalanching = void;
    uint64 operator()(const StringView& inString) const
	{
        return detail::wyhash::hash(inString.data(), inString.size());
    }
};

inline uint64 gHash(StringView inString) { return ankerl::unordered_dense::detail::wyhash::hash(inString.data(), inString.size()); }


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