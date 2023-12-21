#pragma once
#include "Core.h"
#include "StringPool.h"

#include <thread>
#include <semaphore>
#include <optional>

static constexpr int cFileRepoIndexBits = 6;
static constexpr int cFileIndexBits     = 26;

static constexpr uint32 cMaxFileRepos   = (1u << cFileRepoIndexBits) - 1;
static constexpr uint32 cMaxFilePerRepo = (1u << cFileIndexBits) - 1;


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


// Forward declarations of Win32 types.
struct _FILE_ID_128;
using USN = int64;


// Alias for FILE_ID_128.
struct FileRefNumber
{
	uint64 mData[2] = {};

	FileRefNumber() = default;
	FileRefNumber(const FileRefNumber&) = default;
	FileRefNumber& operator=(const FileRefNumber&) = default;

	auto operator<=>(const FileRefNumber& inOther) const = default;
	using Hasher = MemoryHasher<FileRefNumber>;

	// Conversion to/from FILE_ID_128.
	FileRefNumber(const _FILE_ID_128&);
	_FILE_ID_128   ToWin32() const;
	FileRefNumber& operator=(const _FILE_ID_128&);
};
static_assert(sizeof(FileRefNumber) == 16);

// Formatter for FileRefNumber.
template <> struct std::formatter<FileRefNumber> : std::formatter<std::string_view>
{
	auto format(FileRefNumber inRefNumber, format_context& ioCtx) const
	{
		return std::format_to(ioCtx.out(), "0x{:X}{:016X}", inRefNumber.mData[1], inRefNumber.mData[0]);
	}
};


// Identifier for a file. 4 bytes.
struct FileID
{
	uint32 mRepoIndex : cFileRepoIndexBits = cMaxFileRepos;
	uint32 mFileIndex : cFileIndexBits     = cMaxFilePerRepo;

	bool IsValid() const { return *this != FileID{}; }

	auto operator<=>(const FileID& inOther) const = default;
	using Hasher = MemoryHasher<FileID>;
};
static_assert(sizeof(FileID) == 4);


struct FileInfo : NoCopy
{
	const FileID        mID;				// Our ID for this file.
	const uint16        mIsDirectory : 1;	// Is this a directory or a file.
	const uint16        mNamePos : 15;		// Position in the path of the start of the file name (after the last '/').
	const uint16        mExtensionPos;		// Position in the path of the first '.' in the file name.
	const StringView    mPath;				// Path relative to the root directory.

	FileRefNumber		mRefNumber;			// File ID used by Windows. Can change when the file is deleted and re-created.
	USN					mUSN		= 0;	// Identifier of the last change to this file.
	bool                mExists		= true;	// True if the file exists (ie. not deleted).

	FileInfo(FileID inID, StringView inPath, FileRefNumber inRefNumber, bool inIsDirectory);

	bool		IsDirectory() const		{ return mIsDirectory != 0; }
	StringView	GetName() const			{ return mPath.substr(mNamePos); }
	StringView	GetExtension() const	{ return mPath.substr(mExtensionPos); }

	auto operator <=>(const FileInfo& inOther) const { return mID <=> inOther.mID; }	// Comparing the ID is enough.
};


// Top level container for files.
// TODO: rename to root something?
struct FileRepo : NoCopy
{
	static constexpr size_t cFilePerSegment = 4096;
	using FilesSegmentedVector = SegmentedVector<FileInfo, std::allocator<FileInfo>, sizeof(FileInfo) * cFilePerSegment>;
	using FilesByRefNumberMap = SegmentedHashMap<FileRefNumber, FileID, FileRefNumber::Hasher>;
	using FileRefNumberSet = HashSet<FileRefNumber, FileRefNumber::Hasher>;

	FileRepo(uint32 inIndex, StringView inShortName, StringView inRootPath);
	~FileRepo() = default;

	FileInfo&		AddFile(FileRefNumber inRefNumber, StringView inPath, bool inIsDirectory);
	const FileInfo& GetFile(FileID inFileID) const	{ gAssert(inFileID.mRepoIndex == mIndex); return mFiles[inFileID.mFileIndex]; }
	FileInfo&		GetFile(FileID inFileID)		{ gAssert(inFileID.mRepoIndex == mIndex); return mFiles[inFileID.mFileIndex]; }

	FileID						FindFile(FileRefNumber inRefNumber) const;				// Return an invalid FileID if not found.
	std::optional<StringView>	FindPath(FileRefNumber inRefNumber) const;	// Return the path if it's an already known file.

	OwnedHandle OpenFileByRefNumber(FileRefNumber inRefNumber) const;
	void        ScanDirectory(FileRefNumber inRefNumber, std::span<uint8> ioBuffer);

	void        QueueScanDirectory(FileRefNumber inRefNumber);
	bool        IsInScanDirectoryQueue(FileRefNumber inRefNumber) const;
	bool		ProcessScanDirectoryQueue(std::span<uint8> ioBuffer);	// Process some items from the queue. Return false if the queue is empty.

	bool		ProcessMonitorDirectory(std::span<uint8> ioBuffer);		// Check if files changed and queue scans for them. Return false if there were no changes.



	uint32                    mIndex = 0;        // The index of this repo.
	StringView                mShortName;		 // A named used for display.
	StringView                mRootPath;         // Absolute path to the repo. No trailing slash.
	OwnedHandle               mDriveHandle;      // Handle to the drive, needed to open files with ref numbers.
	uint64                    mUSNJournalID = 0; // Journal ID, needed to query the USN journal.

	USN						  mNextUSN = 0;

	FilesSegmentedVector	  mFiles;            // All the files.
	FilesByRefNumberMap       mFilesByRefNumber; // Map to find files by ref number.
	mutable std::mutex		  mFilesMutex;

	StringPool                mStringPool;       // Pool for storing all the paths.

	FileRefNumberSet	mScanDirQueue;
	mutable std::mutex	mScanDirQueueMutex;
};

struct FileSystem : NoCopy
{
	void AddRepo(StringView inShortName, StringView inRootPath);

	void StartMonitoring();		// Only call after adding all repos.
	void StopMonitoring();


	const FileInfo& GetFile(FileID inFileID) const	{ return mRepos[inFileID.mRepoIndex].GetFile(inFileID); }
	FileInfo&		GetFile(FileID inFileID)		{ return mRepos[inFileID.mRepoIndex].GetFile(inFileID); }

	void			KickScanDirectoryThread();
	void			KickMonitorDirectoryThread();
	void			ScanDirectoryThread(std::stop_token inStopToken);
	void			MonitorDirectoryThread(std::stop_token inStopToken);

	SegmentedVector<FileRepo> mRepos;
	bool					  mInitialScan = true;

	std::jthread             mScanDirThread;
	std::binary_semaphore    mScanDirThreadSignal = std::binary_semaphore(1);

	std::jthread             mMonitorDirThread;
	std::binary_semaphore    mMonitorDirThreadSignal = std::binary_semaphore(1);
};

inline FileSystem gFileSystem;