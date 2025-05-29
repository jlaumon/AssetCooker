/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "Core.h"
#include "StringPool.h"
#include "CookingSystemIDs.h"
#include "Queue.h"
#include "SyncSignal.h"
#include "FileUtils.h"
#include "FileTime.h"

#include <Bedrock/Vector.h>
#include <Bedrock/Thread.h>
#include <Bedrock/Mutex.h>
#include <Bedrock/ConditionVariable.h>
#include <Bedrock/StringFormat.h>
#include <Bedrock/HashMap.h>

// Forward declarations.
struct FileID;
struct FileInfo;
struct FileRepo;
struct FileDrive;
struct FileSystem;

// Forward declarations of Win32 types.
struct _FILE_ID_128;
using USN = int64;

constexpr USN cMaxUSN = INT64_MAX;

static constexpr int cFileRepoIndexBits = 6;
static constexpr int cFileIndexBits     = 26;

static constexpr uint32 cMaxFileRepos   = (1u << cFileRepoIndexBits) - 1;
static constexpr uint32 cMaxFilePerRepo = (1u << cFileIndexBits) - 1;


enum class OpenFileError : uint8
{
	NoError,
	FileNotFound,
	AccessDenied,
	SharingViolation,	// File already opened by another process.
};


enum class OpenFileAccess : uint8
{
	GenericRead,
	AttributesOnly,		// Should not fail with SharingViolation.
};


struct HandleOrError
{
	HandleOrError(OwnedHandle&& ioHandle)
	{
		gAssert(ioHandle.IsValid());
		mHandle = gMove(ioHandle);
	}
	HandleOrError(OpenFileError inError)  { mError = inError; }
	bool          IsValid() const { return mHandle.IsValid(); }
	OwnedHandle&  operator*() { return mHandle; }

	OwnedHandle   mHandle;
	OpenFileError mError = OpenFileError::NoError;
};


// Alias for FILE_ID_128.
struct FileRefNumber
{
	uint64 mData[2]                                                          = { (uint64)-1, (uint64)-1 };

	constexpr FileRefNumber()                                                = default;
	constexpr FileRefNumber(const FileRefNumber&)                            = default;
	constexpr ~FileRefNumber()                                               = default;
	constexpr FileRefNumber& operator=(const FileRefNumber&)                 = default;
	constexpr auto           operator<=>(const FileRefNumber& inOther) const = default;

	// Conversion to/from FILE_ID_128.
	FileRefNumber(const _FILE_ID_128& inFileID128) { *this = inFileID128; }
	_FILE_ID_128                   ToWin32() const;
	FileRefNumber&                 operator=(const _FILE_ID_128&);

	constexpr bool                 IsValid() const { return *this != cInvalid(); }
	static constexpr FileRefNumber cInvalid() { return {}; }

	TempString                     ToString() const;
};
static_assert(sizeof(FileRefNumber) == 16);

template <> struct Hash<FileRefNumber>
{
	uint64 operator()(FileRefNumber inRefNumber) const
	{
		return gHash(inRefNumber.mData, sizeof(inRefNumber.mData));
	}
};


// Wrapper for a 128-bits hash value.
struct Hash128
{
	uint64 mData[2] = {};

	constexpr auto operator<=>(const Hash128&) const = default;
};

template <> struct Hash<Hash128>
{
	// Hash128 is already a good quality hash, just return the lower 8 bytes.
	uint64 operator()(Hash128 inHash) const { return inHash.mData[0]; }
};


// Hash type specific to paths.
struct PathHash : Hash128 {};
template <> struct Hash<PathHash> : Hash<Hash128> {};

// Hash the absolute path of a file in a case insensitive manner.
PathHash gHashPath(StringView inAbsolutePath);


// Identifier for a file. 4 bytes.
struct FileID
{
	uint32                  mRepoIndex : cFileRepoIndexBits = cMaxFileRepos;
	uint32                  mFileIndex : cFileIndexBits     = cMaxFilePerRepo;

	FileInfo&				GetFile() const; // Convenience getter for the FileInfo itself.
	FileRepo&				GetRepo() const; // Convenience getter for the FileRepo.

	bool                    IsValid() const { return *this != cInvalid(); }
	static constexpr FileID cInvalid() { return {}; }

	uint32                  AsUInt() const
	{
		uint32 i;
		memcpy(&i, this, sizeof(*this));
		return i;
	}

	auto operator<=>(const FileID& inOther) const = default;
};
static_assert(sizeof(FileID) == 4);

template <> struct Hash<FileID>
{
	uint64 operator()(FileID inID) const { return gHash(inID.AsUInt()); }
};

enum class FileType : int
{
	File,
	Directory
};


struct FileInfo : NoCopy
{
	const FileID                  mID;                  // Our ID for this file.
	const int16                   mNamePos;             // Position in the path of the start of the file name (after the last '/').
	const int16                   mExtensionPos;        // Position in the path of the last '.' in the file name.
	const StringView              mPath;                // Path relative to the root directory.
	const Hash128                 mPathHash;            // Case-insensitive hash of the path.

	bool                          mIsDirectory     : 1; // Is this a directory or a file. Note: could change if a file is deleted then a directory of the same name is created.
	bool                          mIsDepFile       : 1; // Is this a dep file.
	bool                          mCommandsCreated : 1; // Are cooking commands already created for this file.
	FileRefNumber                 mRefNumber      = {}; // File ID used by Windows. Can change when the file is deleted and re-created.
	FileTime                      mCreationTime   = {}; // Time of the creation of this file (or its deletion if the file is deleted).
	USN                           mLastChangeUSN  = 0;  // Identifier of the last change to this file.
	FileTime                      mLastChangeTime = {}; // Time of the last change to this file.

	Vector<CookingCommandID>      mInputOf;             // List of commands that use this file as input.
	Vector<CookingCommandID>      mOutputOf;            // List of commands that use this file as output. There should be only one, otherwise it's an error. // TODO tiny vector optimization // TODO actually detect that error

	bool                          IsDeleted() const { return !mRefNumber.IsValid(); }
	bool                          IsDirectory() const { return mIsDirectory; }
	FileType                      GetType() const { return mIsDirectory ? FileType::Directory : FileType::File; }
	StringView                    GetName() const { return mPath.SubStr(mNamePos); }
	StringView                    GetNameNoExt() const { return mPath.SubStr(mNamePos, mExtensionPos - mNamePos); }
	StringView                    GetExtension() const { return mPath.SubStr(mExtensionPos); }
	StringView                    GetDirectory() const { return mPath.SubStr(0, mNamePos); } // Includes the trailing slash.
	const FileRepo&               GetRepo() const { return mID.GetRepo(); }

	TempString                    ToString() const; // For convenience when we need to log things about this file.

	FileInfo(FileID inID, StringView inPath, Hash128 inPathHash, FileType inType, FileRefNumber inRefNumber);
};


struct ScanQueue
{
	void Push(FileID inDirID)
	{
		{
			LockGuard lock(mMutex);
			mDirectories.PushBack(inDirID);
		}

		// Wake up any waiting thread.
		mConditionVariable.NotifyOne();
	}

	FileID Pop()
	{
		LockGuard lock(mMutex);

		// When the queue appears empty, we need to wait until all the other workers are also idle, because they may add more work to the queue.
		if (mDirectories.Empty())
		{
			if (mThreadsBusy-- == 1)
			{
				// This is the last thread, all the others are already idle/waiting.
				// Wake them up and exit.
				mConditionVariable.NotifyAll();
				return FileID::cInvalid();
			}
			else
			{
				// Wait until more work is pushed into the queue, or all other threads are also idle.
				// Note: while loop needed because there can be spurious wake ups.
				while (mDirectories.Empty() && mThreadsBusy > 0)
					mConditionVariable.Wait(lock);

				// If the queue is indeed empty, exit.
				if (mDirectories.Empty())
					return FileID::cInvalid();
				else
					mThreadsBusy++; // Otherwise this thread is busy again.
			}
		}

		FileID dir = mDirectories.Back();
		mDirectories.PopBack();
		return dir;
	}

	Vector<FileID>    mDirectories;
	Mutex             mMutex;
	ConditionVariable mConditionVariable;
	int               mThreadsBusy = 1;
};


// Top level container for files.
struct FileRepo : NoCopy
{
	FileRepo(uint32 inIndex, StringView inName, StringView inRootPath, FileDrive& inDrive);
	~FileRepo() = default;

	FileInfo&			GetFile(FileID inFileID)		{ gAssert(inFileID.mRepoIndex == mIndex); return mFiles[inFileID.mFileIndex]; }
	const FileInfo&		GetFile(FileID inFileID) const	{ gAssert(inFileID.mRepoIndex == mIndex); return mFiles[inFileID.mFileIndex]; }
	FileInfo&           GetOrAddFile(StringView inPath, FileType inType, FileRefNumber inRefNumber);
	void                MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp);
	void                MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp, const LockGuard<Mutex>& inLock);

	StringView          RemoveRootPath(StringView inFullPath);

	void                ScanDirectory(FileID inDirectoryID, ScanQueue& ioScanQueue, Span<uint8> ioBuffer);

	enum class RequestedAttributes
	{
		USNOnly,
		All
	};
	void                ScanFile(FileInfo& ioFile, RequestedAttributes inRequestedAttributes);

	uint32				mIndex = 0;				// The index of this repo.
	StringView			mName;					// A named used to identify the repo.
	StringView			mRootPath;				// Absolute path to the repo. Starts with the drive letter, ends with a slash.
	FileDrive&			mDrive;					// The drive this repo is on.
	FileID				mRootDirID;				// The FileID of the root dir.
	bool				mNoOrphanFiles = false; // True when the repo is not supposed to contain orphan files (files that are neither inputs or outputs of any command).

	VMemArray<FileInfo> mFiles;					// All the files in this repo.

	StringPool			mStringPool;			// Pool for storing all the paths.
};


struct FileDrive : NoCopy
{
	FileDrive(char inDriveLetter);

	template <typename taFunctionType>
	USN                    ReadUSNJournal(USN inStartUSN, Span<uint8> ioBuffer, taFunctionType inRecordCallback) const; // TODO replace template by a typed std::function_ref equivalent
	bool                   ProcessMonitorDirectory(Span<uint8> ioBufferUSN, ScanQueue &ioScanQueue, Span<uint8> ioBufferScan); // Check if files changed. Return false if there were no changes.
	FileRepo*              FindRepoForPath(StringView inFullPath);                                        // Return nullptr if not in any repo.

	HandleOrError          OpenFileByRefNumber(FileRefNumber inRefNumber, OpenFileAccess inDesiredAccess, FileID inFileID) const;
	[[nodiscard]] bool     GetFullPath(const OwnedHandle& inFileHandle, TempString& outFullPath) const;   // Get the full path of this file, including the drive letter part. Return true on succes.
	USN                    GetUSN(const OwnedHandle& inFileHandle) const;

	FileID                 FindFileID(FileRefNumber inRefNumber) const;                                   // Return an invalid FileID if not found.

	char                   mLetter = 'C';
	OwnedHandle            mHandle;           // Handle to the drive, needed to open files with ref numbers.
	uint64                 mUSNJournalID = 0; // Journal ID, needed to query the USN journal.
	USN                    mFirstUSN     = 0;
	USN                    mNextUSN      = 0;
	bool                   mLoadedFromCache = false;
	Vector<FileRepo*>      mRepos;

	using FilesByRefNumberMap = VMemHashMap<FileRefNumber, FileID>;

	FilesByRefNumberMap    mFilesByRefNumber;      // Map to find files by ref number.
	mutable Mutex          mFilesByRefNumberMutex; // Mutex to protect access to the map.
};



struct FileSystem : NoCopy
{
	FileRepo&       AddRepo(StringView inName, StringView inRootPath);	// Path can be absolute or relative to current directory.

	void            StartMonitoring(); // Only call after adding all repos.
	void            StopMonitoring();

	bool            IsMonitoringStarted() const			{ return mMonitorDirThread.IsJoinable(); }
	bool            IsMonitoringIdle() const			{ return mIsMonitorDirThreadIdle.Load(); }

	FileRepo&		GetRepo(FileID inFileID)			{ return mRepos[inFileID.mRepoIndex]; }
	FileInfo&		GetFile(FileID inFileID)			{ return mRepos[inFileID.mRepoIndex].GetFile(inFileID); }
	Span<const FileRepo> GetRepos() const				{ return mRepos; }

	FileRepo*       FindRepo(StringView inRepoName);                   // Find a repo by name. Return nullptr if not found.
	FileRepo*       FindRepoByPath(StringView inAbsolutePath);         // Find a repo by path. The path can be of a file inside the repo. Return nullptr if not found.
	FileDrive*      FindDrive(char inLetter);                          // Find a drive by its letter. Return nullptr if not found.
	FileID          FindFileIDByPath(StringView inAbsolutePath) const; // Find a file by its full path. Return invalid FileID if not found.
	FileID          FindFileIDByPathHash(PathHash inPathHash) const;   // Find a file by the hash of its full path. See gHashPath(). Return invalid FileID if not found.

	bool            CreateDirectory(FileID inFileID);                  // Make sure all the parent directories for this file exist.
	bool            DeleteFile(FileID inFileID);                       // Delete this file on disk.

	int             GetDriveCount() const { return mDrives.Size(); }   // Number of drives, for debug/display.
	int             GetRepoCount() const { return mRepos.Size(); }     // Number of repos, for debug/display.
	int             GetFileCount() const;                              // Total number of files, for debug/display.

	void			KickMonitorDirectoryThread();

	enum class InitState
	{
		NotInitialized,
		LoadingCache,
		Scanning,
		ReadingUSNJournal,
		ReadingIndividualUSNs,
		PreparingCommands,
		Ready
	};
	InitState       GetInitState() const { return mInitState.Load(); }

	void            SaveCache();
	void            LoadCache();

private:
	void            InitialScan(const Thread& inThread, Span<uint8> ioBufferUSN);
	void			MonitorDirectoryThread(const Thread& ioThread);

	void            RescanLater(FileID inFileID);

	FileDrive&		GetOrAddDrive(char inDriveLetter);

	friend void     gDrawDebugWindow();
	friend void     gDrawStatusBar();
	friend void     gDrawFileSearch();
	friend struct FileRepo;

	VMemArray<FileRepo>        mRepos  = { 10_MiB, gVMemCommitGranularity() };
	VMemArray<FileDrive>       mDrives = { 10_MiB, gVMemCommitGranularity() };        // All the drives that have at least one repo on them.

	Atomic<InitState>          mInitState = InitState::NotInitialized;
	struct InitStats
	{
		int             mIndividualUSNToFetch = 0;
		AtomicInt32		mIndividualUSNFetched = 0;
		int64           mReadyTicks           = 0; // Tick count when the Ready state was reached.
	};
	InitStats                  mInitStats;

	Thread                     mMonitorDirThread;
	SyncSignal                 mMonitorDirThreadSignal;
	AtomicBool                 mIsMonitorDirThreadIdle = true;

	struct FileToRescan
	{
		FileID mFileID;         // The file to re-scan (can be a directory).
		int64  mWaitUntilTicks; // The time to wait before re-scanning the file.
	};

	Queue<FileToRescan> mFilesToRescan;
	Mutex               mFilesToRescanMutex;

	using FilesByPathHash = VMemHashMap<PathHash, FileID>;
	FilesByPathHash mFilesByPathHash;      // Map to find files by path hash.
	mutable Mutex   mFilesByPathHashMutex; // Mutex to protect access to the map.
};


inline FileSystem gFileSystem;


inline FileInfo& FileID::GetFile() const
{
	return gFileSystem.GetFile(*this);
}


inline FileRepo& FileID::GetRepo() const
{
	return gFileSystem.GetRepo(*this);
}


inline TempString gToString(FileRefNumber inRefNumber)
{
	return gTempFormat("0x%llX%016llX", inRefNumber.mData[1], inRefNumber.mData[0]);
}
