/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "CommandVariables.h"
#include "CookingSystem.h"

#include <Bedrock/Test.h>



static bool sToDigit(char inChar, int& outNumber)
{
	outNumber = inChar - '0';
	return outNumber >= 0 && outNumber <= 9;
}


static bool sParseInt(StringView inStr, int& outInt)
{
	outInt = 0;
	bool negative = false;

	if (!inStr.Empty() && inStr.Front() == '-')
	{
		negative = true;
		inStr.RemovePrefix(1);
	}

	if (inStr.Empty()) [[unlikely]]
	{
		outInt = 0;
		return false;
	}

	do
	{
		int digit;
		if (!sToDigit(inStr.Front(), digit)) [[unlikely]]
		{
			outInt = 0;
			return false;
		}

		outInt = outInt * 10 + digit;

		inStr.RemovePrefix(1);

	} while (!inStr.Empty());

	if (negative)
		outInt = -outInt;

	return true;
}


REGISTER_TEST("ParseInt")
{
	int i;
	TEST_TRUE(sParseInt("123", i));
	TEST_TRUE(i == 123);

	TEST_TRUE(sParseInt("-123", i));
	TEST_TRUE(i == -123);

	TEST_TRUE(sParseInt("0", i));
	TEST_TRUE(i == 0);

	TEST_TRUE(sParseInt("-0", i));
	TEST_TRUE(i == 0);

	TEST_TRUE(sParseInt("0000", i));
	TEST_TRUE(i == 0);

	TEST_TRUE(sParseInt("00001", i));
	TEST_TRUE(i == 1);

	TEST_TRUE(sParseInt("123456789", i));
	TEST_TRUE(i == 123456789);

	TEST_TRUE(sParseInt("2147483647", i));
	TEST_TRUE(i == cMaxInt);
	
	TEST_TRUE(sParseInt("-2147483648", i));
	TEST_TRUE(i == -2147483648);

	TEST_FALSE(sParseInt("", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("-", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("a", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("-a", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("123a", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("-123a", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("123 ", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("- 123", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("--123", i));
	TEST_TRUE(i == 0);

	TEST_FALSE(sParseInt("+123", i)); // We could support that, but at the same time, why would we
	TEST_TRUE(i == 0);
};

namespace
{

struct Slice
{
	int mStart = 0;
	int mEnd   = cMaxInt;

	bool operator==(const Slice&) const = default;
};

}

// Parse a python-like slice, ie. "[start:end]".
// Both start and end are optional. The column is optional too if only start is provided.
static bool sParseSlice(StringView inSliceStr, Slice& outSlice)
{
	gAssert(inSliceStr.Front() == '[' && inSliceStr.Back() == ']');
	outSlice = {};

	// Remove the brackets.
	inSliceStr.RemovePrefix(1);
	inSliceStr.RemoveSuffix(1);

	int column_pos = inSliceStr.Find(':');

	// Parse the start if it's there.
	StringView start_str = inSliceStr.SubStr(0, column_pos);
	gRemoveLeading(start_str, " \t");
	gRemoveTrailing(start_str, " \t");
	if (!start_str.Empty())
	{
		if (!sParseInt(start_str, outSlice.mStart)) [[unlikely]]
		{
			outSlice = {};
			return false;
		}
	}

	// Parse the end if it's there.
	if (column_pos != -1)
	{
		StringView end_str = inSliceStr.SubStr(column_pos + 1);
		gRemoveLeading(end_str, " \t");
		gRemoveTrailing(end_str, " \t");
		if (!end_str.Empty())
		{
			if (!sParseInt(end_str, outSlice.mEnd)) [[unlikely]]
			{
				outSlice = {};
				return false;
			}
		}
	}

	return true;
}


REGISTER_TEST("ParseSlice")
{
	Slice slice;
	TEST_TRUE(sParseSlice("[123:321]", slice));
	TEST_TRUE(slice.mStart == 123);
	TEST_TRUE(slice.mEnd == 321);

	TEST_TRUE(sParseSlice("[ 123  :   321    ]", slice));
	TEST_TRUE(slice.mStart == 123);
	TEST_TRUE(slice.mEnd == 321);

	TEST_TRUE(sParseSlice("[:321]", slice));
	TEST_TRUE(slice.mStart == 0);
	TEST_TRUE(slice.mEnd == 321);

	TEST_TRUE(sParseSlice("[123:]", slice));
	TEST_TRUE(slice.mStart == 123);
	TEST_TRUE(slice.mEnd == cMaxInt);

	TEST_TRUE(sParseSlice("[:]", slice)); // Arguable if we should allow this
	TEST_TRUE(slice == Slice());

	TEST_TRUE(sParseSlice("[]", slice)); // Arguable if we should allow this
	TEST_TRUE(slice == Slice());

	TEST_TRUE(sParseSlice("[-123:-321]", slice));
	TEST_TRUE(slice.mStart == -123);
	TEST_TRUE(slice.mEnd == -321);

	TEST_FALSE(sParseSlice("[123x:-321]", slice));
	TEST_TRUE(slice == Slice());

	TEST_FALSE(sParseSlice("[123:-321x]", slice));
	TEST_TRUE(slice == Slice());
};


static bool sParseArgument(StringView& ioFormatStr, StringView& outArgument, Slice& outSlice)
{
	outArgument = "";
	outSlice    = {};

	StringView format_str = ioFormatStr;
	gAssert(format_str.Front() == '{');
	format_str.RemovePrefix(1);

	int curly_close_pos = format_str.Find('}');
	if (curly_close_pos == -1) [[unlikely]]
		return false;

	StringView arg = format_str.SubStr(0, curly_close_pos);

	// Remove leading and trailing space
	gRemoveLeading(arg, " \t");
	gRemoveTrailing(arg, " \t");

	if (arg.Empty()) [[unlikely]]
		return false;

	// Check if there's a slice (eg. [0:3]) after the argument.
	int square_open_pos = arg.Find('[');
	if (square_open_pos != -1)
	{
		// There should be nothing else after the slice.
		if (arg.Back() != ']') [[unlikely]]
			return false;

		StringView slice_str = arg.SubStr(square_open_pos);
		arg.RemoveSuffix(slice_str.Size());

		if (!sParseSlice(slice_str, outSlice)) [[unlikely]]
			return false;
	}

	// Update buffer to point after the closing curly bracket.
	format_str.RemovePrefix(curly_close_pos + 1);

	ioFormatStr = format_str;
	outArgument = arg;
	return true;
}


REGISTER_TEST("ParseCommandVariableArgument")
{
	StringView test = "{   arg    }!   ";
	StringView arg;
	Slice slice;

	// Successes ---

	TEST_TRUE(sParseArgument(test, arg, slice));
	TEST_TRUE(arg == "arg");
	TEST_TRUE(test.Front() == '!');
	TEST_TRUE(slice == Slice());

	test = "{   arg[1:3] }!  ";
	TEST_TRUE(sParseArgument(test, arg, slice));
	TEST_TRUE(arg == "arg");
	TEST_TRUE(test.Front() == '!');
	TEST_TRUE(slice.mStart == 1);
	TEST_TRUE(slice.mEnd == 3);

	test = "{   arg[1] }!  ";
	TEST_TRUE(sParseArgument(test, arg, slice));
	TEST_TRUE(arg == "arg");
	TEST_TRUE(test.Front() == '!');
	TEST_TRUE(slice.mStart == 1);
	TEST_TRUE(slice.mEnd == cMaxInt);

	test = "{   arg1:3] }!  "; // Valid only for the purpose of parsing, error will be detected when trying to identify the arg
	TEST_TRUE(sParseArgument(test, arg, slice));
	TEST_TRUE(arg == "arg1:3]");
	TEST_TRUE(test.Front() == '!');
	TEST_TRUE(slice == Slice());

	// Failures ---

	test = "{   arg    o";
	TEST_FALSE(sParseArgument(test, arg, slice));
	TEST_TRUE(arg.Empty());
	TEST_TRUE(test.Front() == '{');
	TEST_TRUE(slice == Slice());

	test = "{   arg[1:3 }";
	TEST_FALSE(sParseArgument(test, arg, slice));
	TEST_TRUE(arg.Empty());
	TEST_TRUE(test.Front() == '{');
	TEST_TRUE(slice == Slice());

};


// Format inFormatStr into outString by calling inFormatter any time a CommandVariable is encountered.
// Return true on success. Output is always an empty string on failure.
// FIXME should use a FunctionRef instead of template
template<class taFormatter>
static bool sParseCommandVariables(StringView inFormatStr, taFormatter&& inFormatter, TempString& outString)
{
	// Make sure the output is cleared, the function should return empty string on failure.
	outString.Clear();

	TempString out_string;
	while (true)
	{
		int open_brace_pos = inFormatStr.FindFirstOf("{");
		if (open_brace_pos == -1)
		{
			// No more arguments.
			out_string.Append(inFormatStr);

			// Move the result to the actual output.
			outString = gMove(out_string);
			return true;
		}

		out_string.Append(inFormatStr.SubStr(0, open_brace_pos));
		inFormatStr.RemovePrefix(open_brace_pos);

		StringView arg;
		Slice      slice;
		if (!sParseArgument(inFormatStr, arg, slice)) [[unlikely]]
		{
			// Failed to parse argument.
			return false;
		}

		if (arg.StartsWith(gToStringView(CommandVariables::Repo)))
		{
			arg.RemovePrefix(gToStringView(CommandVariables::Repo).Size());

			if (arg.Size() < 2 || arg[0] != ':') [[unlikely]]
				return false; // Failed to get the repo name part.
		
			StringView repo_name = arg.SubStr(1);

			if (!inFormatter(CommandVariables::Repo, repo_name, slice, inFormatStr, out_string)) [[unlikely]]
				return false; // Formatter says error.
		}
		else
		{
			bool matched = false;
			for (int i = 0; i < (int)CommandVariables::_Count; ++i)
			{
				CommandVariables var = (CommandVariables)i;

				if (var == CommandVariables::Repo)
					continue; // Treated separately.

				if (arg == gToStringView(var))
				{
					matched = true;
					if (!inFormatter(var, "", slice, inFormatStr, out_string)) [[unlikely]]
						return false; // Formatter says error.

					break;
				}
			}

			if (!matched) [[unlikely]]
				return false; // Invalid variable name.
		}
	}
}


REGISTER_TEST("ParseCommandVariables")
{
	// Make sure temp memory is initialized or the tests will fail.
	TEST_INIT_TEMP_MEMORY(1_KiB);

	auto test_formatter = [](CommandVariables inVar, StringView inRepoName, Slice inSlice, StringView inRemainingFormatStr, TempString& outStr)
	{
		outStr.Append(gToStringView(inVar));

		if (inVar == CommandVariables::Repo)
			outStr.Append(inRepoName);

		if (inSlice != Slice())
		{
			outStr.Append("[");

			if (inSlice.mStart != 0)
				gAppendFormat(outStr, "%d", inSlice.mStart);

			if (inSlice.mEnd != cMaxInt)
				gAppendFormat(outStr, ":%d", inSlice.mEnd);

			outStr.Append("]");
		}

		return true;
	};

	{
		TempString result;
		bool success = sParseCommandVariables("OH! { Repo:Test} AH!", test_formatter, result);
		TEST_TRUE(success);
		TEST_TRUE(result == "OH! RepoTest AH!");
	}

	{
		TempString result;
		bool success = sParseCommandVariables("{   File    }{Ext}{\tExt\t}{Dir } ", test_formatter, result);
		TEST_TRUE(success);
		TEST_TRUE(result == "FileExtExtDir ");
	}

	{
		TempString result;
		bool success = sParseCommandVariables("{ Repo:! }\n\n{Dir_NoTrailingSlash}\t{Path}", test_formatter, result);
		TEST_TRUE(success);
		TEST_TRUE(result == "Repo!\n\nDir_NoTrailingSlash\tPath");
	}

	{
		TempString result;
		bool success = sParseCommandVariables("{   File[4:5] }{Ext[ : -1 ]}{\tExt[-2]\t}{Dir[100000:100001 ]}{Repo:A[10:70] } ", test_formatter, result);
		TEST_TRUE(success);
		TEST_TRUE(result == "File[4:5]Ext[:-1]Ext[-2]Dir[100000:100001]RepoA[10:70] ");
	}

	{
		TempString result;
		bool success = sParseCommandVariables("JustText", test_formatter, result);
		TEST_TRUE(success);
		TEST_TRUE(result == "JustText");
	}

	{
		TempString result = "previous text";
		bool success = sParseCommandVariables("", test_formatter, result);
		TEST_TRUE(success);
		TEST_TRUE(result == "");
	}

	TempString result;
	TEST_FALSE(sParseCommandVariables("{ Repo: }", test_formatter, result));
	TEST_FALSE(sParseCommandVariables("{ Repo }", test_formatter, result));
	TEST_FALSE(sParseCommandVariables("{ Repo Test }", test_formatter, result));
	TEST_FALSE(sParseCommandVariables("{ File and more things", test_formatter, result));
	TEST_FALSE(sParseCommandVariables("{}", test_formatter, result));
	TEST_FALSE(sParseCommandVariables("{        }", test_formatter, result));
	TEST_FALSE(sParseCommandVariables("{ file }", test_formatter, result));
};


static StringView sApplySlice(StringView inStr, Slice inSlice)
{
	int start;
	if (inSlice.mStart >= 0)
		start = gMin(inSlice.mStart, inStr.Size());
	else
		start = gMax(inStr.Size() + inSlice.mStart, 0);

	int end;
	if (inSlice.mEnd >= 0)
		end = gMin(inSlice.mEnd, inStr.Size());
	else
		end = gMax(inStr.Size() + inSlice.mEnd, 0);

	int count = gMax(0, end - start);
	return inStr.SubStr(start, count);
}


REGISTER_TEST("ApplySlice")
{
	TEST_TRUE(sApplySlice("test!", {}) == "test!");
	TEST_TRUE(sApplySlice("test!", { 0, 3 }) == "tes");
	TEST_TRUE(sApplySlice("test!", { 0, 0 }) == "");
	TEST_TRUE(sApplySlice("test!", { 1, 0 }) == "");
	TEST_TRUE(sApplySlice("test!", { 2, 2 }) == "");
	TEST_TRUE(sApplySlice("test!", { 3, 10 }) == "t!");
	TEST_TRUE(sApplySlice("test!", { -1 }) == "!");
	TEST_TRUE(sApplySlice("test!", { -4 }) == "est!");
	TEST_TRUE(sApplySlice("test!", { 0,-1 }) == "test");
	TEST_TRUE(sApplySlice("test!", { 1,-1 }) == "est");
	TEST_TRUE(sApplySlice("test!", { -1,1 }) == "");
	TEST_TRUE(sApplySlice("test!", { -1,-2 }) == "");
	TEST_TRUE(sApplySlice("test!", { -2,-1 }) == "t");
	TEST_TRUE(sApplySlice("test!", { -10 }) == "test!");
	TEST_TRUE(sApplySlice("test!", { 0, -10 }) == "");
};


static StringView sGetCommandVarString(CommandVariables inVar, const FileInfo& inFile)
{
	switch (inVar)
	{
	case CommandVariables::Ext:
		return inFile.GetExtension();

	case CommandVariables::File:
		return inFile.GetNameNoExt();

	case CommandVariables::Dir:
		return inFile.GetDirectory();

	case CommandVariables::Dir_NoTrailingSlash: 
		{
			StringView dir = inFile.GetDirectory();

			// If the dir is not empty, there is a trailing slash. Remove it.
			if (!dir.Empty()) [[likely]]
				dir.RemoveSuffix(1);

			return dir;
		}
	case CommandVariables::Path:
		return inFile.mPath;

	case CommandVariables::Repo:
		return {}; // Repo needs to be handled separately.

	default:
		gAssert(false);
		return {};
	}
}


// TODO add an output error string to help understand why it fails
bool gFormatCommandString(StringView inFormatStr, const FileInfo& inFile, TempString& outString)
{
	if (inFormatStr.Empty())
		return false; // Consider empty format string is an error.

	return sParseCommandVariables(inFormatStr, [&inFile](CommandVariables inVar, StringView inRepoName, Slice inSlice, StringView inRemainingFormatStr, TempString& outStr) 
	{
		StringView var_str;

		// Repo need to be treated separately, string is based on inRepoName rather than inFile.
		if (inVar == CommandVariables::Repo)
		{
			if (FileRepo* repo = gFileSystem.FindRepo(inRepoName))
				var_str = repo->mRootPath;
			else
				// Invalid repo name.
				return false;
		}
		else
		{
			// Get the string corresponding to this CommandVariable.
			var_str = sGetCommandVarString(inVar, inFile);
		}

		// Apply the slice to it.
		sApplySlice(var_str, inSlice);

		// Append to the output.
		outStr.Append(var_str);

		// If the string ends with a backslash and the following character is a quote, the backslash will escape it and the command line won't work.
		// In this case, add another backslash to escape the first one.
		if (outStr.EndsWith(R"(\)") &&
			inRemainingFormatStr.StartsWith(R"(")") &&
			!outStr.EndsWith(R"(\\)")) // Already added the double backslash
		{
			outStr.Append(R"(\)");
		}

		return true;
	}, outString);
}


bool gFormatFilePath(StringView inFormatStr, const FileInfo& inFile, FileRepo*& outRepo, TempString& outPath)
{
	FileRepo*  repo = nullptr;
	TempString out_path;

	bool       success = sParseCommandVariables(
        inFormatStr,
        [&inFile, &repo](CommandVariables inVar, StringView inRepoName, Slice inSlice, StringView inRemainingFormatStr, TempString& outStr) {
            // Repo need to be treated separately, string is based on inRepoName rather than inFile.
            if (inVar == CommandVariables::Repo)
            {
                // There can only be 1 Repo arg and it should be at the very beginning of the path.
                if (repo != nullptr || !outStr.Empty())
                    return false;

                // Repo cannot be sliced.
                if (inSlice != Slice())
                    return false;

                repo = gFileSystem.FindRepo(inRepoName);

                // Invalid repo name.
                if (repo == nullptr)
                    return false;
            }
            else
            {
                // Get the string corresponding to this CommandVariable.
                StringView var_str = sGetCommandVarString(inVar, inFile);

                // Apply the slice to it.
                sApplySlice(var_str, inSlice);

                // Append to the output.
                outStr.Append(var_str);
            }

            return true;
        },
        out_path);

	if (!success)
		return false;

	outRepo = repo;
	outPath = gMove(out_path);
	return true;
}


FileID gGetOrAddFileFromFormat(StringView inFormatStr, const FileInfo& inFile)
{
	FileRepo*  repo = nullptr;
	TempString path;

	if (!gFormatFilePath(inFormatStr, inFile, repo, path))
		return FileID::cInvalid();

	return repo->GetOrAddFile(path, FileType::File, {}).mID;
}
