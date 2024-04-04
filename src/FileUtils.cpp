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


static bool sDirectoryExistsW(WStringView inPath)
{
	gAssert(!inPath.ends_with(L'\\'));
	gAssert(*(inPath.data() + inPath.size()) == 0);

	DWORD attributes = GetFileAttributesW(inPath.data());

	return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}


static bool sCreateDirectoryW(WStringView inPath)
{
	gAssert(!inPath.ends_with(L'\\'));
	gAssert(*(inPath.data() + inPath.size()) == 0);

	BOOL success = CreateDirectoryW(inPath.data(), nullptr);

	return success || GetLastError() == ERROR_ALREADY_EXISTS;
}


static bool sCreateDirectoryRecursiveW(Span<wchar_t> ioPath)
{
	gAssert(ioPath[ioPath.size() - 1] == 0);
	gAssert(ioPath[ioPath.size() - 2] != L'\\');
	gAssert(ioPath.size() > 2 && ioPath[1] == L':'); // We expect an absolute path, first two characters should be the drive (ioPath might be the drive itself without trailing slash).

	// Early out if the directory already exists.
	if (sDirectoryExistsW(WStringView(ioPath.data(), ioPath.size() - 1)))
		return true;

	// Otherwise try to create every parent directory.
	wchar_t* p_begin = ioPath.data();
	wchar_t* p_end   = ioPath.data() + ioPath.size() - 1; // Just before the null-terminator.
	wchar_t* p       = p_begin + 3;
	while(p != p_end)
	{
		if (*p == L'\\')
		{
			// Null terminate the dir path.
			*p = 0;

			// Create the parent directory.
			if (!sCreateDirectoryW({ p_begin, p }))
				return false; // Uh-oh failed.

			// Put back the slash we overwrote earlier.
			*p = L'\\';
		}

		p++;
	}

	// Create the final directory.
	return sCreateDirectoryW({ p_begin, p });
}


bool gCreateDirectoryRecursive(StringView inAbsolutePath)
{
	gAssert(gIsNormalized(inAbsolutePath) && gIsAbsolute(inAbsolutePath));

	// TODO replace this with the UTF8 version of the Win32 functions
	std::array<wchar_t, cMaxPathSizeUTF8> wpath_buffer;
	OptionalWStringView wpath_optional = gUtf8ToWideChar(inAbsolutePath, wpath_buffer);
	if (!wpath_optional)
		return false;

	WStringView wpath = *wpath_optional;

	// If the path ends with a slash, remove it, because that's what the other functions expect.
	if (wpath.back() == L'\\')
	{
		wpath.remove_suffix(1);
		wpath_buffer[wpath.size()] = 0; // Replace slash with null terminator.
	}

	return sCreateDirectoryRecursiveW({ wpath_buffer.data(), wpath.size() + 1 }); // +1 to include the null terminator.
}
