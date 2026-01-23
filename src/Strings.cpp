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
	out_str.Reserve(gMax((int)inWString.size() * 4, 4096));

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
WStringView gUtf8ToWideChar(StringView inString, Span<wchar_t> ioBuffer)
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


constexpr FormatColor cBlack   = {   0,   0,   0, true };
constexpr FormatColor cRed	   = { 255,   0,   0, true };
constexpr FormatColor cGreen   = {   0, 255,   0, true };
constexpr FormatColor cYellow  = { 255, 255,   0, true };
constexpr FormatColor cBlue	   = {   0,   0, 255, true };
constexpr FormatColor cMagenta = { 255,   0, 255, true };
constexpr FormatColor cCyan	   = {   0, 255,   0, true };
constexpr FormatColor cWhite   = { 255, 255, 255, true };


void gParseANSIColors(StringView inStr, Vector<FormatSpan>& outSpans)
{
	// If there are no ANSI colors there's no need to generate any spans and inStr can be used as is
	bool		generate_spans = false;

	int			cursor		   = 0;
	FormatColor current_color  = {};

	do
	{
		int sequence_start = inStr.Find("\x1b[", cursor);
		if (sequence_start == -1)
		{
			// If we have format spans, output the final one
			if (generate_spans && cursor < inStr.Size())
			{
				FormatSpan span;
				span.mSpan	= inStr.SubStr(cursor);
				span.mColor = current_color;
				outSpans.PushBack(span);
			}

			return;
		}
		else
		{
			int sequence_end = inStr.Find('m', sequence_start);
			if (sequence_end == -1)
			{
				// Error, missing end of ANSI escape sequence, no obvious way to parse
				outSpans.ClearAndFreeMemory();
				return;
			}
			else
			{
				// At least one ANSI escape sequence, do generate
				generate_spans = true;

				if (cursor < sequence_start)
				{
					// Emit current span
					FormatSpan span;
					span.mSpan	= inStr.SubStr(cursor, sequence_start - cursor);
					span.mColor = current_color;
					outSpans.PushBack(span);
				}

				cursor = sequence_start + 2; // skip over "\x1b["

				// Parse ANSI escape sequence

				TempVector<long> numbers;
				char*			 parse_end;
				do
				{
					long parsed_number = strtol(inStr.AsCStr() + cursor, &parse_end, 10);
					if (parse_end != inStr.AsCStr() + cursor)
					{
						numbers.PushBack(parsed_number);
						cursor = parse_end - inStr.AsCStr();

						if (*parse_end == ';')
						{
							cursor++;
						}
					}
					else if (*parse_end == 'm')
					{
						cursor++;
						break;
					}
					else
					{
						// Error, broken ANSI escape sequence, no obvious way to parse
						outSpans.ClearAndFreeMemory();
						return;
					}
				} while (parse_end != inStr.End());

				// Handle supported sequences

				if (numbers.Size() == 5 && numbers[0] == 38 && numbers[1] == 2)
				{
					// rgb color
					current_color = { (uint8)numbers[2], (uint8)numbers[3], (uint8)numbers[4], true };
				}
				else if (numbers.Size() > 0)
				{
					// regular / old style style specifiers

					for (int num_idx = 0; num_idx < numbers.Size(); ++num_idx)
					{
						switch (numbers[num_idx])
						{
						case 0:		current_color = {};			break;	// Reset all styles
						case 30:	current_color = cBlack;		break;
						case 31:	current_color = cRed;		break;
						case 32:	current_color = cGreen;		break;
						case 33:	current_color = cYellow;	break;
						case 34:	current_color = cBlue;		break;
						case 35:	current_color = cMagenta;	break;
						case 36:	current_color = cCyan;		break;
						case 37:	current_color = cWhite;		break;
						case 39:	current_color = {};			break;	// Default color
						default:								break;	// Unhandled command
						}
					}
				}

				cursor = sequence_end + 1;
			}
		}
	} while (true);
}


REGISTER_TEST("gRemoveTrailing")
{
	StringView test = "test !!";
	gRemoveTrailing(test, " !");
	TEST_TRUE(test == "test");

	gRemoveTrailing(test, "o");
	TEST_TRUE(test == "test");

	gRemoveTrailing(test, "tes");
	TEST_TRUE(test == "");
};


REGISTER_TEST("gRemoveLeading")
{
	StringView test = "!! test";
	gRemoveLeading(test, " !");
	TEST_TRUE(test == "test");

	gRemoveLeading(test, "o");
	TEST_TRUE(test == "test");

	gRemoveLeading(test, "tes");
	TEST_TRUE(test == "");
};

REGISTER_TEST("gParseANSIColors")
{
	StringView		   test;
	Vector<FormatSpan> spans;

	test = "";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 0);
	spans.Clear();

	test = "No escape sequences";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 0);
	spans.Clear();

	test = "\x1b[38;2;255;0;0mRed text specified as RGB\x1b[0m";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 1);
	TEST_TRUE(spans[0].mSpan.Begin() == test.Begin() + 15); // should skip the parsed sequence itself
	TEST_TRUE(spans[0].mSpan.End() == test.Begin() + 40);	// ensure it ends before the next sequence
	TEST_TRUE(spans[0].mColor == cRed);
	spans.Clear();

	test = "\x1b[32mGreen text\x1b[34mBlue text\x1b[0m";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 2);
	TEST_TRUE(spans[0].mSpan.Begin() == test.Begin() + 5);
	TEST_TRUE(spans[0].mColor == cGreen);
	TEST_TRUE(spans[1].mSpan.Begin() == test.Begin() + 20);
	TEST_TRUE(spans[1].mColor == cBlue);
	spans.Clear();

	test = "\x1b[1;35mBold magenta text, except we don't support bold so ignored\x1b[0m";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 1);
	TEST_TRUE(spans[0].mSpan.Begin() == test.Begin() + 7);
	TEST_TRUE(spans[0].mColor == cMagenta);
	spans.Clear();

	test = "\x1b[0mEscape sequence but no colors";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 1);
	TEST_TRUE(spans[0].mSpan.Begin() == test.Begin() + 4);
	TEST_FALSE(spans[0].mColor.mValid);
	spans.Clear();

	test = "\x1b[38;2;255;0;0mBroken ANSI escape sequence 1 (missing m at end)\x1b[0";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 0);
	spans.Clear();

	test = "\x1b[38;2;255;0;0!Broken ANSI escape sequence 2 (missing m in first sequence)\x1b[0m";
	gParseANSIColors(test, spans);
	TEST_TRUE(spans.Size() == 0);
};
