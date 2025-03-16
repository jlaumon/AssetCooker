/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "DepFile.h"

#include "App.h"
#include "Debug.h"
#include "FileSystem.h"
#include <Bedrock/Test.h>
#include <Bedrock/Algorithm.h>
#include <Bedrock/StringFormat.h>

#include "win32/file.h"
#include "win32/io.h"

bool gReadFile(StringView inPath, TempVector<uint8>& outFileData)
{
	TempString long_path;
	inPath = gConvertToLargePath(inPath, long_path);

	OwnedHandle handle = CreateFileA(inPath.AsCStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER file_size = {};
	if (GetFileSizeEx(handle, &file_size) == 0)
		return false;

	if (file_size.QuadPart > UINT32_MAX)
		gAppFatalError("gReadFile: Trying to read a file that is > 4GiB (%s)", inPath.AsCStr());

	outFileData.Resize(file_size.QuadPart + 1, EResizeInit::NoZeroInit);

	DWORD bytes_read = 0;
	BOOL success = ReadFile(handle, outFileData.Data(), file_size.LowPart, &bytes_read, nullptr) != 0;

	// Null terminate in case it's text.
	outFileData[file_size.QuadPart] = 0;

	return success != 0;
}


static bool sIsSpace(char inChar)
{
	return inChar == ' ' || inChar == '\t';
}

static bool sMakeEscapedWithBackslash(char inChar)
{
	return inChar == ' ' || inChar == '\\' || inChar == ':' || inChar == '[' || inChar == ']' || inChar == '#';
}

static bool sMakeEscapedWithDollar(char inChar)
{
	return inChar == '$';
}

// glslang and GNU make expects a series of paths on a single line.
// Spaces in paths are escaped with backslashes.
static StringView sExtractFirstPath(StringView inLine)
{
	bool escaping_next_character = false;
	// Trim spaces before processing.
	while (!inLine.Empty() && sIsSpace(inLine[0]))
	{
		inLine.RemovePrefix(1);
	}
	while (!inLine.Empty() && sIsSpace(inLine[inLine.Size() - 1]))
	{
		inLine.RemoveSuffix(1);
	}

	StringView remaining = inLine;
	while (!remaining.Empty())
	{
		char c = remaining[0];
		if (escaping_next_character)
		{
			escaping_next_character = false;
		}
		else if (c == '\\')
		{
			escaping_next_character = true;
		}
		else if(sIsSpace(c))
			return inLine.SubStr(0, remaining.Data() - inLine.Data());

		remaining = remaining.SubStr(1);
	}
	return inLine;
}

REGISTER_TEST("ExtractFirstPath")
{
	TEST_TRUE(sExtractFirstPath("file.txt") == "file.txt");
	TEST_TRUE(sExtractFirstPath("file.txt other.bat") == "file.txt");
	TEST_TRUE(sExtractFirstPath("file with spaces.txt") == "file");
	TEST_TRUE(sExtractFirstPath("file\\ with\\ spaces.txt") == "file\\ with\\ spaces.txt");
	TEST_TRUE(sExtractFirstPath(" \ttrim_me.png \t ") == "trim_me.png");
};

// Some make rule generators tend to produce absolute paths with all special characters escaped.
// Some example of such path: C\:\\Bogus\\Path\ \\with\\too_many\\Backsla.sh
static TempString sCleanupPath(StringView line)
{ 
	TempString cleaned_path;
	for (auto it = line.begin(); it != line.end(); ++it)
	{
		auto next_character = it + 1;
		if (*it == '\\' && next_character != line.end() && sMakeEscapedWithBackslash(*next_character))
		{
			continue;
		}
		if (*it == '$' && next_character != line.end() && sMakeEscapedWithDollar(*next_character))
		{
			continue;
		}
		char c = *it;
		cleaned_path.Append({ &c, 1 });
	}
	return cleaned_path;
}

REGISTER_TEST("CleanupPath")
{
	TEST_TRUE(sCleanupPath("./file.txt") == "./file.txt");

	// Handling proper Windows-style path escaping.
	TEST_TRUE(sCleanupPath(R"(C\:\\some\\escaped\\path)") == R"(C:\some\escaped\path)");
	TEST_TRUE(sCleanupPath(R"(C:\\path\ with\ spaces\\should\ work.txt)") == R"(C:\path with spaces\should work.txt)");
	// but handling those shouldn't break perfectly valid paths.
	TEST_TRUE(sCleanupPath(R"(C:\Windows\path32\command.com)") == R"(C:\Windows\path32\command.com)");
	TEST_TRUE(sCleanupPath(R"(C:\Windows\)") == R"(C:\Windows\)");



	// GNU Make escape characters induced from the documentation
	
	// Mentioned in https://www.gnu.org/software/make/manual/make.html#What-Makefiles-Contain
	TEST_TRUE(sCleanupPath(R"(\#sharp.glsl)") == R"(#sharp.glsl)");

	// See also https://www.gnu.org/software/make/manual/make.html#Rule-Syntax-1 for an explicit mention of having
	// to escape $ to avoid variable expansion.
	// Also, https://www.gnu.org/software/make/manual/make.html#Splitting-Long-Lines shows a corner case where $
	// can be used to remove the space resulting of scaffolding the newline.
	TEST_TRUE(sCleanupPath(R"($$currency.glsl)") == R"($currency.glsl)");

	// Given make's $() evaluation syntax, one might consider escaping parentheses but they don't have to be.
	TEST_TRUE(sCleanupPath(R"=((parens).glsl)=") == R"=((parens).glsl)=");

	// https://www.gnu.org/software/make/manual/make.html#Using-Wildcard-Characters-in-File-Names
	// mentions that wildcard characters can be escaped in order to use them verbatim.
	TEST_TRUE(sCleanupPath(R"(\[brackets\].glsl)") == R"([brackets].glsl)");

	// No explicit mention of having to escape spaces but given that prerequisites are split by spaces,
	// it sounds logical to escape them to inhibit that mechanism.
	TEST_TRUE(sCleanupPath(R"(space\ file.glsl)") == R"(space file.glsl)");

	// Given the role of % in Makefiles, one might consider escaping them but it doesn't happen.
	TEST_TRUE(sCleanupPath(R"(%percent%.glsl)") == R"(%percent%.glsl)");
};


// Barebones GNU Make-like dependency file parser. 
static bool sParseDepFileMake(FileID inDepFileID, StringView inDepFileContent, Vector<FileID>& outInputs)
{
	StringView dep_file_content = inDepFileContent;

	// First there's the rule name, followed by a colon and a space. Skip that.
	constexpr StringView cDepStartMarker  = ": ";
	int deps_start = dep_file_content.Find(cDepStartMarker);
	if (deps_start == -1)
	{
		gAppLogError(R"(Failed to parse Dep File %s, couldn't find the first dependency)", inDepFileID.GetFile().ToString().AsCStr());
		return false;
	}

	dep_file_content = dep_file_content.SubStr(deps_start + cDepStartMarker.Size());
	
	while (!dep_file_content.Empty())
	{
		// Skip white space before the path.
		while (!dep_file_content.Empty() && sIsSpace(dep_file_content.Front()))
			dep_file_content = dep_file_content.SubStr(1);

		// Lines generally end with space + backslash + linefeed.
		constexpr StringView cWindowsLineEnd  = " \\\r\n";
		constexpr StringView cLineEnd  = " \\\n";
		int path_end  = dep_file_content.Find(cWindowsLineEnd);
		int next_path = path_end + cWindowsLineEnd.Size();

		// Match first on Windows' CRLF before matching on LF.
		if (path_end == -1)
		{
			path_end  = dep_file_content.Find(cLineEnd);
			next_path = path_end + cLineEnd.Size();
		}
		if (path_end == -1)
		{
			// Last line might also end with a linefeed (or nothing).
			constexpr StringView cLastLineEnd = "\n\r";
			path_end = dep_file_content.FindFirstOf(cLastLineEnd);
			next_path = path_end + 1;

			if (path_end == -1)
				next_path = dep_file_content.Size();
		}

		if (path_end == -1)
			next_path = dep_file_content.Size();

		StringView current_line = StringView(dep_file_content.SubStr(0, path_end));

		while (!current_line.Empty())
		{
			StringView dep_file_path = sExtractFirstPath(current_line);

			current_line = current_line.SubStr(dep_file_path.Data() + dep_file_path.Size() - current_line.Data());

			// Make a copy because we need a null terminated string.
			TempString path = sCleanupPath(dep_file_path);

			// Get a proper absolute path, in case some relative parts are involved (might happen when doing #include "../something.h").
			TempString abs_path = gGetAbsolutePath(path);

			// Find the repo.
			FileRepo* repo = gFileSystem.FindRepoByPath(abs_path);
			if (repo == nullptr)
			{
				gAppLogError(R"(Failed to parse Dep File %s, path doesn't belong in any Repo ("%s"))", 
					inDepFileID.GetFile().ToString().AsCStr(), abs_path.AsCStr());
				return false;
			}

			// Skip the repo path to get the file part.
			StringView file_path = abs_path.SubStr(repo->mRootPath.Size());

			// Find or add the file.
			// The file probably exists, but we can't be sure of that (maybe we're reading the dep file after it was deleted).
			FileID     file_id = repo->GetOrAddFile(file_path, FileType::File, {}).mID;

			// Add it to the input list, while making sure there are no duplicates.
			gEmplaceSorted(outInputs, file_id);
		}

		// Continue to the next path.
		dep_file_content.RemovePrefix(next_path);
	} 

	return true;
}


namespace 
{
	struct Dependency
	{
		enum DepType
		{
			Input,
			Output
		};

		DepType    mType;
		StringView mPath;
	};
}

// Parse custom AssetCooker dep file format
// Ex:
//   INPUT: D:/inputs/file.png
//   INPUT: D:/other_inputs/file.txt
//   OUTPUT: X:/outputs/file.dds
static void sParseDepFileAssetCooker(StringView inDepFileContent, TempVector<Dependency>& outDependencies, Vector<String>& outErrors)
{
	StringView dep_file_content = inDepFileContent;

	while (!dep_file_content.Empty())
	{
		StringView line      = dep_file_content.SubStr(0, dep_file_content.FindFirstOf("\r\n"));
		const int  line_size = line.Size();

		defer 
		{
			// Go to next line.
			dep_file_content.RemovePrefix(line_size);

			gRemoveLeading(dep_file_content, "\r\n");
		};

		// Remove leading white space.
		gRemoveLeading(line, " \t");

		// Empty lines are allowed, just skip them.
		if (line.Empty())
			continue;

		constexpr StringView input_str = "INPUT:";
		constexpr StringView output_str = "OUTPUT:";

		Dependency dep;

		if (line.StartsWith(input_str))
		{
			dep.mPath = line.SubStr(input_str.Size());
			dep.mType = Dependency::Input;
		}
		else if (line.StartsWith(output_str))
		{
			dep.mPath = line.SubStr(output_str.Size());
			dep.mType = Dependency::Output;
		}
		else
		{
			// Bad format.
			outErrors.PushBack(gFormat(R"(Invalid line. Lines should start with INPUT: or OUTPUT: ("%s"))", TempString(line).AsCStr()));
			continue;
		}

		// Remove leading and trailing spaces.
		gRemoveLeading(dep.mPath, " \t");
		gRemoveTrailing(dep.mPath, " \t");

		if (dep.mPath.Empty())
		{
			// Bad format.
			outErrors.PushBack(gFormat(R"(Invalid line. There should be a path after INPUT: or OUTPUT: ("%s"))", TempString(line).AsCStr()));
			continue;
		}

		outDependencies.PushBack(dep);
	}
}


REGISTER_TEST("DepFile_AssetCooker")
{
	TEST_INIT_TEMP_MEMORY(10_KiB);

	constexpr StringView dep_file = 
		" \t  "
		"INPUT:C:/simple/input.txt\n"
		"OUTPUT:C:/simple/output.txt\n"
		"Hello error\n"
		"INPUT:\n" // also error, no path 
		"INPUT:C:/with spaces/t e s t.txt\n\r"
		"\t\t\t \n\n\n\n\n"
		"#INPUT:error but technically this could be a comment?\n"
		"  INPUT:  C:/with spaces\\test.txt\t  \r\n"
		"\n"
		"  \t\t\t\tOUTPUT: \t D:/an/output.txt\t  \r\n"
		"                                       \n"
	;

	TempVector<Dependency> dependencies;
	Vector<String>         errors;
	sParseDepFileAssetCooker(dep_file, dependencies, errors);

	TEST_TRUE(errors.Size() == 3);
	TEST_TRUE(dependencies.Size() == 5);

	TEST_TRUE(dependencies[0].mPath == "C:/simple/input.txt");
	TEST_TRUE(dependencies[0].mType == Dependency::Input);

	TEST_TRUE(dependencies[1].mPath == "C:/simple/output.txt");
	TEST_TRUE(dependencies[1].mType == Dependency::Output);

	TEST_TRUE(dependencies[2].mPath == "C:/with spaces/t e s t.txt");
	TEST_TRUE(dependencies[2].mType == Dependency::Input);

	TEST_TRUE(dependencies[3].mPath == "C:/with spaces\\test.txt");
	TEST_TRUE(dependencies[3].mType == Dependency::Input);

	TEST_TRUE(dependencies[4].mPath == "D:/an/output.txt");
	TEST_TRUE(dependencies[4].mType == Dependency::Output);
};


static bool sParseDepFileAssetCooker(FileID inDepFileID, StringView inDepFileContent, Vector<FileID>& outInputs, Vector<FileID>& outOutputs)
{
	TempVector<Dependency> dependencies;
	Vector<String>         errors;

	// Parse the content.
	sParseDepFileAssetCooker(inDepFileContent, dependencies, errors);

	// Process the file paths.
	for (const Dependency& dep : dependencies)
	{
		// Make a copy because we need a null terminated string.
		TempString path = dep.mPath;

		// Get a proper absolute path, in case some relative parts are involved (might happen when doing #include "../something.h").
		TempString abs_path = gGetAbsolutePath(path);

		// Find the repo.
		FileRepo* repo = gFileSystem.FindRepoByPath(abs_path);
		if (repo == nullptr)
		{
			errors.PushBack(gFormat(R"(Path doesn't belong in any Repo ("%s"))", abs_path.AsCStr()));
		}

		// Skip the repo path to get the file part.
		StringView file_path = abs_path.SubStr(repo->mRootPath.Size());

		// Find or add the file.
		// The file probably exists, but we can't be sure of that (maybe we're reading the dep file after it was deleted).
		FileID     file_id = repo->GetOrAddFile(file_path, FileType::File, {}).mID;

		// Add it to the input/output lists, while making sure there are no duplicates.
		switch (dep.mType)
		{
		case Dependency::Input:
			gEmplaceSorted(outInputs, file_id);
			break;
		case Dependency::Output:
			gEmplaceSorted(outOutputs, file_id);
			break;
		default:
			gAssert(false);
			break;
		}
	}

	if (!errors.Empty())
	{
		gAppLogError(R"(Failed to parse Dep File %s)", inDepFileID.GetFile().ToString().AsCStr());
		for (String& error : errors)
			gAppLogError("  %s", error.AsCStr());

		return false;
	}

	return true;
}



bool gReadDepFile(DepFileFormat inFormat, FileID inDepFileID, Vector<FileID>& outInputs, Vector<FileID>& outOutputs)
{
	TempString full_path = gConcat(inDepFileID.GetRepo().mRootPath, inDepFileID.GetFile().mPath);

	TempVector<uint8> buffer;
	if (!gReadFile(full_path, buffer))
	{
		gAppLogError(R"(Failed to read Dep File %s - %s)", 
			inDepFileID.GetFile().ToString().AsCStr(), GetLastErrorString().AsCStr());
		return false;
	}

	gAssert(buffer[buffer.Size() - 1] == 0); // Should be null terminated.

	StringView dep_file_content((const char*)buffer.Begin(), buffer.Size() - 1); // -1 to exclude the null terminator

	if (inFormat == DepFileFormat::AssetCooker)
	{
		return sParseDepFileAssetCooker(inDepFileID, dep_file_content, outInputs, outOutputs);
	}
	else if (inFormat == DepFileFormat::Make)
	{
		return sParseDepFileMake(inDepFileID, dep_file_content, outInputs);
	}
	else
	{
		// Unsupported.
		return false;
	}
}


void gApplyDepFileContent(CookingCommand& ioCommand, Span<FileID> inDepFileInputs, Span<FileID> inDepFileOutputs)
{
	// Update the mInputOf fields.
	{
		TempHashSet<FileID> old_dep_file_inputs = gToHashSet(Span(ioCommand.mDepFileInputs));
		TempHashSet<FileID> new_dep_file_inputs = gToHashSet(Span(inDepFileInputs));

		// If there are new inputs, let them know about this command.
		for (FileID input : inDepFileInputs)
			if (!old_dep_file_inputs.Contains(input) && !gContains(ioCommand.mInputs, input))
				input.GetFile().mInputOf.PushBack(ioCommand.mID);

		// If some inputs disappeared, remove this command from them.
		for (FileID old_input : old_dep_file_inputs)
			if (!new_dep_file_inputs.Contains(old_input) && !gContains(ioCommand.mInputs, old_input))
			{
				bool found = gSwapEraseFirstIf(old_input.GetFile().mInputOf, [&ioCommand](CookingCommandID inID) { return inID == ioCommand.mID; });
				gAssert(found);
			}
	}

	// Update the mOutputOf fields.
	{
		TempHashSet<FileID> old_dep_file_outputs = gToHashSet(Span(ioCommand.mDepFileOutputs));
		TempHashSet<FileID> new_dep_file_outputs = gToHashSet(Span(inDepFileOutputs));

		// If there are new outputs, let them know about this command.
		for (FileID output : inDepFileOutputs)
			if (!old_dep_file_outputs.Contains(output) && !gContains(ioCommand.mOutputs, output))
				output.GetFile().mOutputOf.PushBack(ioCommand.mID);

		// If some outputs disappeared, remove this command from them.
		for (FileID old_output : old_dep_file_outputs)
			if (!new_dep_file_outputs.Contains(old_output) && !gContains(ioCommand.mOutputs, old_output))
			{
				bool found = gSwapEraseFirstIf(old_output.GetFile().mOutputOf, [&ioCommand](CookingCommandID inID) { return inID == ioCommand.mID; });
				gAssert(found);
			}
	}

	// Update the DepFile input/output lists.
	ioCommand.mDepFileInputs = inDepFileInputs;
	ioCommand.mDepFileOutputs = inDepFileOutputs;
}
