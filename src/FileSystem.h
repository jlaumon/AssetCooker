#pragma once
#include "Core.h"
#include "StringPool.h"

#include <thread>
#include <semaphore>

static constexpr int cFileRepoIndexBits = 6;
static constexpr int cFileIndexBits     = 26;

static constexpr size_t cMaxFileRepos   = 1ull << cFileRepoIndexBits;
static constexpr size_t cMaxFilePerRepo = 1ull << cFileIndexBits;


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

struct _FILE_ID_128;

// Alias for FILE_ID_128.
struct FileRefNumber
{
	uint64 mData[2] = {};

	FileRefNumber() = default;
	FileRefNumber(const FileRefNumber&) = default;
	FileRefNumber(const _FILE_ID_128&);

	auto operator<=>(const FileRefNumber& inOther) const = default;
	using Hasher = MemoryHasher<FileRefNumber>;

	_FILE_ID_128   ToWin32() const;
	FileRefNumber& operator=(const _FILE_ID_128&);
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


struct FileInfo : NoCopy
{
	const FileID        mID;              // Our ID for this file.
	const uint16        mIsDirectory : 1; // Is this a directory or a file.
	const uint16        mNamePos : 15;    // Position in the path of the start of the file name (after the last '/').
	const uint16        mExtensionPos;    // Position in the path of the first '.' in the file name.
	const FileRefNumber mRefNumber;       // ID used by Windows.
	const StringView    mPath;            // Path relative to the root directory.

	FileInfo(FileID inID, StringView inPath, FileRefNumber inRefNumber, bool inIsDirectory);

	bool		IsDirectory() const		{ return mIsDirectory != 0; }
	StringView	GetName() const			{ return mPath.substr(mNamePos); }
	StringView	GetExtension() const	{ return mPath.substr(mExtensionPos); }

	auto operator <=>(const FileInfo& inOther) const { return mID <=> inOther.mID; }	// Comparing the ID is enough.
};


// Top level container for files.
struct FileRepo : NoCopy
{
	static constexpr size_t cFilePerSegment = 4096;
	using FilesSegmentedVector = SegmentedVector<FileInfo, std::allocator<FileInfo>, sizeof(FileInfo) * cFilePerSegment>;
	using FilesByRefNumberMap = SegmentedHashMap<FileRefNumber, FileInfo*, FileRefNumber::Hasher>;

	FileRepo(uint32 inIndex, StringView inShortName, StringView inRootPath);
	~FileRepo() = default;

	FileInfo&		AddFile(FileRefNumber inRefNumber, StringView inPath, bool inIsDirectory);
	const FileInfo& GetFile(FileID inFileID) const	{ gAssert(inFileID.mRepoIndex == mIndex); return mFiles[inFileID.mFileIndex]; }
	FileInfo&		GetFile(FileID inFileID)		{ gAssert(inFileID.mRepoIndex == mIndex); return mFiles[inFileID.mFileIndex]; }

	// TODO: move these to a global filesystem containing all repos
	OwnedHandle OpenFileByRefNumber(FileRefNumber inRefNumber) const;
	void        ScanFile(FileID inFileID);
	void        ScanDirectory(FileID inFileID);

	void        QueueScanDirectory(FileID inFileID);
	bool		ProcessScanDirectoryQueue();			// Process some items from the queue. Return false if the queue is empty.

	static constexpr size_t		cScanDirBufferSize = 64 * 1024ull;
	std::unique_ptr<uint8[]>	mScanDirBuffer = nullptr;

	uint32                    mIndex = 0;        // The index of this repo.
	StringView                mShortName;		 // A named used for display.
	StringView                mRootPath;         // Absolute path to the repo. No trailing slash.
	OwnedHandle               mDriveHandle;      // Handle to the drive, needed to open files with ref numbers.

	// TODO: use bigger buckets for files
	SegmentedVector<FileInfo> mFiles;            // All the files.
	FilesByRefNumberMap       mFilesByRefNumber; // Map to find files by ref number.
	std::mutex				  mFilesMutex;

	StringPool                mStringPool;       // Pool for storing all the paths.

	std::vector<FileID> mScanDirQueue;
	std::mutex          mScanDirQueueMutex;
};

struct FileSystem : NoCopy
{
	void AddRepo(StringView inShortName, StringView inRootPath);

	void StartMonitoring();		// Only call after adding all repos.
	void StopMonitoring();


	const FileInfo& GetFile(FileID inFileID) const	{ return mRepos[inFileID.mRepoIndex].GetFile(inFileID); }
	FileInfo&		GetFile(FileID inFileID)		{ return mRepos[inFileID.mRepoIndex].GetFile(inFileID); }

	void			KickScanDirectoryThread();
	void			ScanDirectoryThread(std::stop_token inStopToken);

	SegmentedVector<FileRepo> mRepos;
	std::jthread              mScanDirThread;
	std::binary_semaphore     mScanDirThreadSignal = std::binary_semaphore(1);
};

inline FileSystem gFileSystem;