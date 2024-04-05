#pragma once
#include "Core.h"
#include "Strings.h"


// Wrapper for a HANDLE that closes it on destruction.
struct OwnedHandle : NoCopy
{
	static constexpr void* cInvalid = (void*)-1;

	OwnedHandle()									= default;
	OwnedHandle(void* inHandle)						{ mHandle = inHandle; }
	~OwnedHandle();									// Close the handle.
	OwnedHandle(OwnedHandle&& ioOther)				{ mHandle = ioOther.mHandle; ioOther.mHandle = cInvalid; }
	OwnedHandle& operator=(OwnedHandle&& ioOther)	{ mHandle = ioOther.mHandle; ioOther.mHandle = cInvalid; return *this; }

	operator void*() const							{ return mHandle; }
	bool IsValid() const							{ return mHandle != cInvalid; }

	void* mHandle = cInvalid;
};


// Arbitrary max path size that Asset Cooker decides to support for its files.
// Some code in the FileSystem monitoring actually supports longer paths, because it might get notifications about files outside repos.
constexpr size_t cMaxPathSizeUTF8  = 8192ull;

using TempPath = TempString<cMaxPathSizeUTF8>;


constexpr MutStringView gNormalizePath(MutStringView ioPath); // Replace / by \.
constexpr bool          gIsNormalized(StringView inPath);     // Return true if path only contains backslashes.
TempPath                gGetAbsolutePath(StringView inPath);  // Get the absolute and canonical version of this path.
constexpr bool          gIsAbsolute(StringView inPath);       // Return true if the path is absolute and canonical.

bool                    gCreateDirectoryRecursive(StringView inAbsolutePath);

bool                    gDirectoryExists(StringView inPath);
bool                    gFileExists(StringView inPath);



constexpr MutStringView gNormalizePath(MutStringView ioPath)
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
	return inPath.size() >= 3 && gIsAlpha(inPath[0]) && inPath[1] == ':' && (inPath[2] == '\\' || inPath[2] == '/')
		&& !inPath.Contains(".\\")	// Not canonical if it contains "./" or ".\"
		&& !inPath.Contains("./");  // Note: This also catches "../"
}
