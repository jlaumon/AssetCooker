/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "Core.h"
#include "Strings.h"


// Wrapper for a HANDLE that closes it on destruction.
struct OwnedHandle : NoCopy
{
	static inline void* const cInvalid = (void*)-1;

	OwnedHandle()									= default;
	OwnedHandle(void* inHandle)						{ mHandle = inHandle; }
	~OwnedHandle()									{ Close(); }								
	OwnedHandle(OwnedHandle&& ioOther)				{ mHandle = ioOther.mHandle; ioOther.mHandle = cInvalid; }
	OwnedHandle& operator=(OwnedHandle&& ioOther)	{ Close(); mHandle = ioOther.mHandle; ioOther.mHandle = cInvalid; return *this; }

	operator void*() const							{ return mHandle; }
	bool IsValid() const							{ return mHandle != cInvalid; }
	void Close();									// Close the handle.

	void* mHandle = cInvalid;
};


constexpr MutStringView  gNormalizePath(MutStringView ioPath); // Replace / by \.
constexpr bool           gIsNormalized(StringView inPath);     // Return true if path only contains backslashes.
TempString               gGetAbsolutePath(StringView inPath);  // Get the absolute and canonical version of this path.
constexpr bool           gIsAbsolute(StringView inPath);       // Return true if the path is absolute and canonical.
constexpr StringView     gGetFileNamePart(StringView inPath);  // Get the filename part of a path.
constexpr StringView     gNoTrailingSlash(StringView inPath);  // Remove the trailing slash if there is one.

bool                     gCreateDirectoryRecursive(StringView inAbsolutePath);

bool                     gDirectoryExists(StringView inPath);
bool                     gFileExists(StringView inPath);

[[nodiscard]] StringView gConvertToLargePath(StringView inPath, TempString& ioBuffer); // Prepend "\\?\" if necessary, to allow going over the MAX_PATH limit.

constexpr MutStringView  gNormalizePath(MutStringView ioPath)
{
	for (char& c : ioPath)
	{
		if (c == '/')
			c = '\\';
	}

	return ioPath;
}

template <class taString>
constexpr StringView gNormalizePath(taString& ioPath)
{
	for (char& c : ioPath)
	{
		if (c == '/')
			c = '\\';
	}

	return ioPath;
}

constexpr bool gIsNormalized(StringView inPath)
{
	for (char c : inPath)
	{
		if (c == '/')
			return false;
	}

	return true;
}

constexpr bool gIsAbsolute(StringView inPath)
{
	return inPath.Size() >= 3 && gIsAlpha(inPath[0]) && inPath[1] == ':' && (inPath[2] == '\\' || inPath[2] == '/')
		&& !inPath.Contains(".\\")	// Not canonical if it contains "./" or ".\"
		&& !inPath.Contains("./");  // Note: This also catches "../"
}


constexpr StringView gGetFileNamePart(StringView inPath)
{
	int file_start = inPath.FindLastOf("\\/");
	if (file_start == -1)
		return inPath;

	return inPath.SubStr(file_start + 1);
}


constexpr StringView gGetDirPart(StringView inPath)
{
	int file_start = inPath.FindLastOf("\\/");
	if (file_start == -1)
		return inPath;

	return inPath.SubStr(0, file_start);
}


constexpr StringView gNoTrailingSlash(StringView inPath)
{
	if (inPath.Back() == '\\' || inPath.Back() == '/')
		inPath.RemoveSuffix(1);

	return inPath;
}
