#pragma once
#include "StringPool.h"

static constexpr int cFileRepoIndexBits = 6;
static constexpr int cFileIndexBits     = 26;

static constexpr size_t cMaxFileRepos   = 1ull << cFileRepoIndexBits;
static constexpr size_t cMaxFilePerRepo = 1ull << cFileIndexBits;

// Hash helper to hash entire structs.
// Only use on structs/classes that don't contain any padding.
template <typename taType>
struct MemoryHasher
{
	using is_avalanching = void; // mark class as high quality avalanching hash

    uint64 operator()(const taType& inValue) const noexcept
	{
		static_assert(std::has_unique_object_representations_v<taType>);
        return ankerl::unordered_dense::detail::wyhash::hash(&inValue, sizeof(inValue));
    }
};

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

	void* mHandle = cInvalid;
};

// Alias for FILE_ID_128.
struct FileRefNumber
{
	uint64 mData[2] = {};

	auto operator<=>(const FileRefNumber& inOther) const = default;
	using Hasher = MemoryHasher<FileRefNumber>;
};
static_assert(sizeof(FileRefNumber) == 16);

// Identifier for a file. 4 bytes.
struct FileID
{
	uint32 mRepoIndex : cFileRepoIndexBits = 0;
	uint32 mFileIndex : cFileIndexBits     = 0;

	auto operator<=>(const FileID& inOther) const = default;
	using Hasher = MemoryHasher<FileID>;
};
static_assert(sizeof(FileID) == 4);


struct FileInfo
{
	FileID        mID;                 // Our ID for this file.
	uint16        mNamePos       = 0;  // Position in the path of the start of the file name (after the last '/').
	uint16        mExtensionPos  = 0;  // Position in the path of the first '.' in the file name.
	FileRefNumber mRefNumber	 = {}; // ID used by Windows.
	StringView    mPath;               // Path relative to the root directory.

	StringView GetFileName()  const	{ return mPath.substr(mNamePos); }
	StringView GetExtension() const	{ return mPath.substr(mExtensionPos); }

	auto operator <=>(const FileInfo& inOther) const { return mID <=> inOther.mID; }	// Comparing the ID is enough.
};


// Top level container for files.
struct FileRepo
{
	using FilesByIDMap        = SegmentedHashMap<FileID, FileInfo*, FileID::Hasher>;
	using FilesByRefNumberMap = SegmentedHashMap<FileRefNumber, FileInfo*, FileRefNumber::Hasher>;

	StringView                mRootPath; // Absolute path to the repo. Ends with a '/'.
	SegmentedVector<FileInfo> mFiles;
	StringPool                mStringPool;
	OwnedHandle               mDriveHandle;
	OwnedHandle               mRootDirHandle;
	FilesByIDMap              mFilesByID;
	FilesByRefNumberMap       mFilesByRefNumber;

	void Init(StringView inRootPath);
	void ScanDirectory();
};