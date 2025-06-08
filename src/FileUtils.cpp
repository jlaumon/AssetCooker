/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "FileUtils.h"
#include "App.h"

#include "win32/file.h"
#include "win32/io.h"
#include "win32/misc.h"

void OwnedHandle::Close()
{
	if (mHandle != cInvalid)
	{
		CloseHandle(mHandle);
		mHandle = cInvalid;
	}
}


TempString gGetAbsolutePath(StringView inPath)
{
	TempString abs_path;
	abs_path.Reserve(4096); // Should be enough for everyone.

	// Note: if size is < capacity then it doesn't contain the null term
	//       if size is > capacity then it's the capacity needed (including null term)
	//       size == capacity cannot happen ¯\_(ツ)_/¯
	int size = GetFullPathNameA(inPath.AsCStr(), abs_path.Capacity(), abs_path.Data(), nullptr);

	if (size > abs_path.Capacity())
	{
		// Reserve as needed and try again.
		abs_path.Reserve(size);
		size = GetFullPathNameA(inPath.AsCStr(), abs_path.Capacity(), abs_path.Data(), nullptr);
	}

	if (size == 0 || size > abs_path.Capacity())
		gAppFatalError(R"(Failed get absolute path for %s")", inPath.AsCStr());

	gAssert(size < abs_path.Capacity()); // Don't think that can happen, but if it does it means we might be missing the last char?
	abs_path.Resize(size);
	abs_path.ShrinkToFit();	// Return some of the large buffer we reserved.

	return abs_path;
}


// Prepend "\\?\" if necessary, to allow going over the MAX_PATH limit.
StringView gConvertToLargePath(StringView inPath, TempString& ioBuffer)
{
	// Note: > (MAX_PATH - 13) because the doc says actual max for directories is MAX_PATH - 12, and that includes the null terminator (so + 1).
	// CreateDirectory worked up to MAX_PATH - 2 when I tried, but better stay on the cautious side.
	if (inPath.Size() > (MAX_PATH - 13) && !gStartsWith(inPath, R"(\\?\)"))
	{
		// Can't prepend a relative path, so convert to an absolute path first.
		TempString abs_path;
		if (!gIsAbsolute(inPath))
		{
			abs_path = gGetAbsolutePath(inPath);
			inPath   = abs_path;
		}

		ioBuffer = R"(\\?\)";
		ioBuffer.Append(inPath);
		return ioBuffer;
	}

	return inPath;
}



static bool sCreateDirectory(StringView inPath)
{
	gAssert(!gEndsWith(inPath, "\\"));

	TempString buffer;
	inPath = gConvertToLargePath(inPath, buffer);

	BOOL success = CreateDirectoryA(inPath.AsCStr(), nullptr);

	return success || GetLastError() == ERROR_ALREADY_EXISTS;
}


static bool sCreateDirectoryRecursive(TempString& ioPath)
{
	gAssert(*ioPath.End() == 0);
	gAssert(ioPath.Back() != L'\\');
	gAssert(ioPath.Size() > 2 && ioPath[1] == L':'); // We expect an absolute path, first two characters should be the drive (ioPath might be the drive itself without trailing slash).

	// Early out if the directory already exists.
	if (gDirectoryExists(ioPath))
		return true;

	// Otherwise try to create every parent directory.
	char* p_begin = ioPath.Begin();
	char* p_end   = ioPath.End();
	char* p       = p_begin + 3;
	while(p != p_end)
	{
		if (*p == L'\\')
		{
			// Null terminate the dir path.
			*p = 0;

			// Create the parent directory.
			if (!sCreateDirectory({ p_begin, p }))
				return false; // Uh-oh failed.

			// Put back the slash we overwrote earlier.
			*p = L'\\';
		}

		p++;
	}

	// Create the final directory.
	return sCreateDirectory({ p_begin, p });
}


bool gCreateDirectoryRecursive(StringView inAbsolutePath)
{
	gAssert(gIsNormalized(inAbsolutePath) && gIsAbsolute(inAbsolutePath));

	TempString path_copy = inAbsolutePath;

	// If the path ends with a slash, remove it, because that's what the other functions expect.
	if (path_copy.EndsWith("\\"))
		path_copy.RemoveSuffix(1);

	return sCreateDirectoryRecursive(path_copy);
}


bool gDirectoryExists(StringView inPath)
{
	StringView path = inPath;
	TempString path_copy;

	// If the path ends with a backslash, make a copy and remove it. The Win32 function doesn't like that.
	if (inPath.EndsWith("\\"))
	{
		path_copy = path.SubStr(0, path.Size() - 1);
		path      = path_copy;
	}

	DWORD attributes = GetFileAttributesA(path.AsCStr());

	return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}


bool gFileExists(StringView inPath)
{
	DWORD attributes = GetFileAttributesA(inPath.AsCStr());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
