#include "DepFile.h"

#include "App.h"
#include "Debug.h"
#include "FileSystem.h"

#include "win32/file.h"
#include "win32/io.h"

bool gReadFile(StringView inPath, VMemArray<uint8>& outFileData)
{
	TempPath long_path;
	if (inPath.size() >= MAX_PATH && !gStartsWith(inPath, R"(\\?\)") && gIsAbsolute(inPath))
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
	BOOL success = ReadFile(handle, buffer.data(), file_size.LowPart, &bytes_read, nullptr) != 0;

	// Null terminate in case it's text.
	buffer[file_size.QuadPart] = 0;

	outFileData.IncreaseSize(file_size.QuadPart + 1, lock);

	return success != 0;
}


static bool sIsSpace(char inChar)
{
	return inChar == ' ' || inChar == '\t';
}


// Probably a very shitty parser. Just good enough for parsing dep files from DXC.
static bool sParseMakeDepFile(FileID inDepFileID, StringView inDepFileContent, std::vector<FileID>& outInputs)
{
	StringView dep_file_content = inDepFileContent;

	// First there's the rule name, followed by a colon and a space. Skip that.
	constexpr StringView cDepStartMarker  = ": ";
	size_t deps_start = dep_file_content.find(cDepStartMarker);
	if (deps_start == StringView::npos)
	{
		gApp.LogError(R"(Failed to parse Dep File {}, couldn't find the first dependency)", inDepFileID.GetFile());
		return false;
	}

	dep_file_content = dep_file_content.substr(deps_start + cDepStartMarker.size());

	while (!dep_file_content.empty())
	{
		// Skip white space before the path.
		while (!dep_file_content.empty() && sIsSpace(dep_file_content.front()))
			dep_file_content = dep_file_content.substr(1);

		// Lines generally end with space + backslash + linefeed.
		constexpr StringView cLineEnd  = " \\\n";
		size_t path_end  = dep_file_content.find(cLineEnd);
		size_t next_path = path_end + cLineEnd.size();
		if (path_end == StringView::npos)
		{
			// Last line might also end with a linefeed (or nothing).
			constexpr StringView cLastLineEnd = "\n";
			path_end  = dep_file_content.find(cLastLineEnd);
			next_path = path_end + cLastLineEnd.size();

			if (path_end == StringView::npos)
				next_path = dep_file_content.size();
		}

		// Make a copy because we need a null terminated string.
		TempPath path = StringView(dep_file_content.substr(0, path_end));

		// Get a proper absolute path, in case some relative parts are involved (might happen when doing #include "../something.h").
		TempPath abs_path = gGetAbsolutePath(path);

		// Find the repo.
		FileRepo* repo = gFileSystem.FindRepoByPath(abs_path);
		if (repo == nullptr)
		{
			gApp.LogError(R"(Failed to parse Dep File {}, path doesn't belong in any Repo ("{}"))", inDepFileID.GetFile(), abs_path);
			return false;
		}

		// Skip the repo path to get the file part.
		StringView file_path = abs_path.AsStringView().substr(repo->mRootPath.size());

		// Find or add the file.
		// The file probably exists, but we can't be sure of that (maybe we're reading the dep file after it was deleted).
		FileID file_id = repo->GetOrAddFile(file_path, FileType::File, {}).mID;

		// Add it to the input list, while making sure there are no duplicates.
		gEmplaceSorted(outInputs, file_id);

		// Continue to the next path.
		dep_file_content.remove_prefix(next_path);
	} 

	return true;
}


bool gReadDepFile(DepFileFormat inFormat, FileID inDepFileID, std::vector<FileID>& outInputs, std::vector<FileID>& outOutputs)
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
				input.GetFile().mInputOf.push_back(ioCommand.mID);

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
				output.GetFile().mOutputOf.push_back(ioCommand.mID);

		// If some outputs disappeared, remove this command from them.
		for (FileID old_output : old_dep_file_outputs)
			if (!new_dep_file_outputs.contains(old_output) && !gContains(ioCommand.mOutputs, old_output))
			{
				bool found = gSwapEraseFirstIf(old_output.GetFile().mOutputOf, [&ioCommand](CookingCommandID inID) { return inID == ioCommand.mID; });
				gAssert(found);
			}
	}

	// Update the DepFile input/output lists.
	ioCommand.mDepFileInputs.assign(inDepFileInputs.begin(), inDepFileInputs.end());
	ioCommand.mDepFileOutputs.assign(inDepFileOutputs.begin(), inDepFileOutputs.end());
}
