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

#include "win32/file.h"
#include "win32/io.h"

bool gReadFile(StringView inPath, VMemArray<uint8>& outFileData)
{
	TempPath long_path;
	if (inPath.Size() >= MAX_PATH && !gStartsWith(inPath, R"(\\?\)") && gIsAbsolute(inPath))
	{
		long_path.Append(R"(\\?\)");
		long_path.Append(inPath);
		inPath = long_path.AsStringView();
	}

	OwnedHandle handle = CreateFileA(inPath.AsCStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER file_size = {};
	if (GetFileSizeEx(handle, &file_size) == 0)
		return false;

	if (file_size.QuadPart > UINT32_MAX)
		gApp.FatalError("gReadFile: Trying to read a file that is > 4GiB ({})", inPath);

	auto lock = outFileData.Lock();
	Span<uint8> buffer = outFileData.EnsureCapacity(file_size.QuadPart + 1, lock);

	DWORD bytes_read = 0;
	BOOL success = ReadFile(handle, buffer.Data(), file_size.LowPart, &bytes_read, nullptr) != 0;

	// Null terminate in case it's text.
	buffer[file_size.QuadPart] = 0;

	outFileData.IncreaseSize(file_size.QuadPart + 1, lock);

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
static TempPath sCleanupPath(StringView line)
{ 
	TempPath cleaned_path;
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
	TEST_TRUE(sCleanupPath("./file.txt").AsStringView() == "./file.txt");

	// Handling proper Windows-style path escaping.
	TEST_TRUE(sCleanupPath(R"(C\:\\some\\escaped\\path)").AsStringView() == R"(C:\some\escaped\path)");
	TEST_TRUE(sCleanupPath(R"(C:\\path\ with\ spaces\\should\ work.txt)").AsStringView() == R"(C:\path with spaces\should work.txt)");
	// but handling those shouldn't break perfectly valid paths.
	TEST_TRUE(sCleanupPath(R"(C:\Windows\path32\command.com)").AsStringView() == R"(C:\Windows\path32\command.com)");
	TEST_TRUE(sCleanupPath(R"(C:\Windows\)").AsStringView() == R"(C:\Windows\)");



	// GNU Make escape characters induced from the documentation
	
	// Mentioned in https://www.gnu.org/software/make/manual/make.html#What-Makefiles-Contain
	TEST_TRUE(sCleanupPath(R"(\#sharp.glsl)").AsStringView() == R"(#sharp.glsl)");

	// See also https://www.gnu.org/software/make/manual/make.html#Rule-Syntax-1 for an explicit mention of having
	// to escape $ to avoid variable expansion.
	// Also, https://www.gnu.org/software/make/manual/make.html#Splitting-Long-Lines shows a corner case where $
	// can be used to remove the space resulting of scaffolding the newline.
	TEST_TRUE(sCleanupPath(R"($$currency.glsl)").AsStringView() == R"($currency.glsl)");

	// Given make's $() evaluation syntax, one might consider escaping parentheses but they don't have to be.
	TEST_TRUE(sCleanupPath(R"=((parens).glsl)=").AsStringView() == R"=((parens).glsl)=");

	// https://www.gnu.org/software/make/manual/make.html#Using-Wildcard-Characters-in-File-Names
	// mentions that wildcard characters can be escaped in order to use them verbatim.
	TEST_TRUE(sCleanupPath(R"(\[brackets\].glsl)").AsStringView() == R"([brackets].glsl)");

	// No explicit mention of having to escape spaces but given that prerequisites are split by spaces,
	// it sounds logical to escape them to inhibit that mechanism.
	TEST_TRUE(sCleanupPath(R"(space\ file.glsl)").AsStringView() == R"(space file.glsl)");

	// Given the role of % in Makefiles, one might consider escaping them but it doesn't happen.
	TEST_TRUE(sCleanupPath(R"(%percent%.glsl)").AsStringView() == R"(%percent%.glsl)");
};

// Barebones GNU Make-like dependency file parser. 
static bool sParseMakeDepFile(FileID inDepFileID, StringView inDepFileContent, Vector<FileID>& outInputs)
{
	StringView dep_file_content = inDepFileContent;

	// First there's the rule name, followed by a colon and a space. Skip that.
	constexpr StringView cDepStartMarker  = ": ";
	int deps_start = dep_file_content.Find(cDepStartMarker);
	if (deps_start == -1)
	{
		gApp.LogError(R"(Failed to parse Dep File {}, couldn't find the first dependency)", inDepFileID.GetFile());
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
			TempPath  path = sCleanupPath(dep_file_path);

			// Get a proper absolute path, in case some relative parts are involved (might happen when doing #include "../something.h").
			TempPath  abs_path = gGetAbsolutePath(path);

			// Find the repo.
			FileRepo* repo = gFileSystem.FindRepoByPath(abs_path);
			if (repo == nullptr)
			{
				gApp.LogError(R"(Failed to parse Dep File {}, path doesn't belong in any Repo ("{}"))", inDepFileID.GetFile(), abs_path);
				return false;
			}

			// Skip the repo path to get the file part.
			StringView file_path = abs_path.AsStringView().SubStr(repo->mRootPath.Size());

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


bool gReadDepFile(DepFileFormat inFormat, FileID inDepFileID, Vector<FileID>& outInputs, Vector<FileID>& outOutputs)
{
	TempPath full_path("{}{}", inDepFileID.GetRepo().mRootPath, inDepFileID.GetFile().mPath);

	VMemArray<uint8> buffer(4ull * 1024 * 1024, 4096);
	if (!gReadFile(full_path, buffer))
	{
		gApp.LogError(R"(Failed to read Dep File {} - {})", inDepFileID.GetFile(), GetLastErrorString());
		return false;
	}

	if (inFormat == DepFileFormat::AssetCooker)
	{
		gApp.FatalError("TODO");
	}
	else if (inFormat == DepFileFormat::Make)
	{
		return sParseMakeDepFile(inDepFileID, StringView((const char*)buffer.Begin(), buffer.Size()), outInputs);
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
		HashSet<FileID> old_dep_file_inputs = gToHashSet(Span(ioCommand.mDepFileInputs));
		HashSet<FileID> new_dep_file_inputs = gToHashSet(Span(inDepFileInputs));

		// If there are new inputs, let them know about this command.
		for (FileID input : inDepFileInputs)
			if (!old_dep_file_inputs.contains(input) && !gContains(ioCommand.mInputs, input))
				input.GetFile().mInputOf.PushBack(ioCommand.mID);

		// If some inputs disappeared, remove this command from them.
		for (FileID old_input : old_dep_file_inputs)
			if (!new_dep_file_inputs.contains(old_input) && !gContains(ioCommand.mInputs, old_input))
			{
				bool found = gSwapEraseFirstIf(old_input.GetFile().mInputOf, [&ioCommand](CookingCommandID inID) { return inID == ioCommand.mID; });
				gAssert(found);
			}
	}

	// Update the mOutputOf fields.
	{
		HashSet<FileID> old_dep_file_outputs = gToHashSet(Span(ioCommand.mDepFileOutputs));
		HashSet<FileID> new_dep_file_outputs = gToHashSet(Span(inDepFileOutputs));

		// If there are new outputs, let them know about this command.
		for (FileID output : inDepFileOutputs)
			if (!old_dep_file_outputs.contains(output) && !gContains(ioCommand.mOutputs, output))
				output.GetFile().mOutputOf.PushBack(ioCommand.mID);

		// If some outputs disappeared, remove this command from them.
		for (FileID old_output : old_dep_file_outputs)
			if (!new_dep_file_outputs.contains(old_output) && !gContains(ioCommand.mOutputs, old_output))
			{
				bool found = gSwapEraseFirstIf(old_output.GetFile().mOutputOf, [&ioCommand](CookingCommandID inID) { return inID == ioCommand.mID; });
				gAssert(found);
			}
	}

	// Update the DepFile input/output lists.
	ioCommand.mDepFileInputs = inDepFileInputs;
	ioCommand.mDepFileOutputs = inDepFileOutputs;
}
