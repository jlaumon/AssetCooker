#include "FileUtils.h"
#include "App.h"

#include "win32/file.h"
#include "win32/io.h"
#include "win32/misc.h"

OwnedHandle::~OwnedHandle()
{
	if (mHandle != cInvalid)
		CloseHandle(mHandle);
}


TempPath gGetAbsolutePath(StringView inPath)
{
	TempPath abs_path;
	abs_path.mSize = GetFullPathNameA(inPath.AsCStr(), abs_path.cCapacity, abs_path.mBuffer, nullptr);

	if (abs_path.mSize == 0 || abs_path.mSize > abs_path.cCapacity)
		gApp.FatalError(R"(Failed get absolute path for "{}")", inPath);

	return abs_path;
}


static bool sDirectoryExists(StringView inPath)
{
	gAssert(!inPath.ends_with(L'\\'));

	DWORD attributes = GetFileAttributesA(inPath.AsCStr());

	return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}


static bool sCreateDirectory(StringView inPath)
{
	gAssert(!inPath.ends_with(L'\\'));

	BOOL success = CreateDirectoryA(inPath.AsCStr(), nullptr);

	return success || GetLastError() == ERROR_ALREADY_EXISTS;
}


static bool sCreateDirectoryRecursive(MutStringView ioPath)
{
	gAssert(ioPath[ioPath.size() - 1] == 0);
	gAssert(ioPath[ioPath.size() - 2] != L'\\');
	gAssert(ioPath.size() > 2 && ioPath[1] == L':'); // We expect an absolute path, first two characters should be the drive (ioPath might be the drive itself without trailing slash).

	// Early out if the directory already exists.
	if (sDirectoryExists(ioPath))
		return true;

	// Otherwise try to create every parent directory.
	char* p_begin = ioPath.data();
	char* p_end   = ioPath.data() + ioPath.size() - 1; // Just before the null-terminator.
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

	TempPath path_copy = inAbsolutePath;

	// If the path ends with a slash, remove it, because that's what the other functions expect.
	if (gEndsWith(path_copy, "\\"))
	{
		path_copy.mSize--;
		path_copy.mBuffer[path_copy.mSize] = 0;
	}

	return sCreateDirectoryRecursive(path_copy.AsSpan());
}
