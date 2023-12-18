#pragma once
#include "StringPool.h"

static constexpr int cFileRepoIndexBits = 6;
static constexpr int cFileIndexBits     = 26;

static constexpr size_t cMaxFileRepos   = 1ull << cFileRepoIndexBits;
static constexpr size_t cMaxFilePerRepo = 1ull << cFileIndexBits;

struct OwnedHandle : NoCopy
{
	static constexpr void* cInvalid = (void*)-1;

	OwnedHandle()									= default;
	OwnedHandle(void* inHandle)						{ mHandle = inHandle; }
	~OwnedHandle();									// Close the handle.
	OwnedHandle(OwnedHandle&& ioOther)				{ mHandle = ioOther.mHandle; ioOther.mHandle = cInvalid; }
	OwnedHandle& operator=(OwnedHandle&& ioOther)	{ mHandle = ioOther.mHandle; ioOther.mHandle = cInvalid; return *this; }

	operator void*() const							{ return mHandle; }

	void* mHandle = cInvalid;
};

// Identifier for a file. 4 bytes.
struct FileID
{
	uint32 mRepoIndex : cFileRepoIndexBits = 0;
	uint32 mFileIndex : cFileIndexBits     = 0;
};
static_assert(sizeof(FileID) == 4);


struct FileInfo
{
	FileID     mID;
	uint16     mNamePos      = 0;		// Position in the path of the start of the file name (after the last '/').
	uint16     mExtensionPos = 0;		// Position in the path of the first '.' in the file name.
	StringView mPath;					// Path relative to the root directory.

	StringView GetFileName()  const	{ return mPath.substr(mNamePos); }
	StringView GetExtension() const	{ return mPath.substr(mExtensionPos); }
};


// Top level container for files.
struct FileRepo
{
	StringView				   mRootPath; // Absolute path to the repo. Ends with a '/'.
	segmented_vector<FileInfo> mFiles;
	StringPool                 mStringPool;
	OwnedHandle                mDriveHandle;
	OwnedHandle                mRootDirHandle;

	void Init(StringView inRootPath);
	void ScanDirectory();
};