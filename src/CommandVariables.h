/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include <Bedrock/String.h>

struct FileInfo;
struct FileRepo;
struct FileID;


enum class CommandVariables : uint8
{
	Ext,
	File,
	Dir,
	Dir_NoTrailingSlash,
	Path,
	Repo,
	_Count
};

constexpr StringView gToStringView(CommandVariables inVar)
{
	constexpr StringView cNames[]
	{
		"Ext",
		"File",
		"Dir",
		"Dir_NoTrailingSlash",
		"Path",
		"Repo",
	};
	static_assert(gElemCount(cNames) == (size_t)CommandVariables::_Count);

	return cNames[(int)inVar];
};


// Check for CommandVariables and replace them by the corresponding part of inFile.
// Eg. "copy.exe {Repo:Source}{Path} {Repo:Bin}" will turn into "copy.exe D:/src/file.txt D:/bin/"
bool gFormatCommandString(StringView inFormatStr, const FileInfo& inFile, TempString& outString);


// Check for CommandVariables and replace them by the corresponding part of inFile but expects the string to be a single file path.
// One Repo var is needed at the start and of the path and the corresponding FileRepo will be returned instead of be replaced by its path.
bool gFormatFilePath(StringView inFormatStr, const FileInfo& inFile, FileRepo*& outRepo, TempString& outPath);


// Format the file path and get (or add) the corresponding file. Return an invalid FileID if the format is invalid.
FileID gGetOrAddFileFromFormat(StringView inFormatStr, const FileInfo& inFile);
