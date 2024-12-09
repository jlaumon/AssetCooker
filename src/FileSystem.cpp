/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "FileSystem.h"
#include "App.h"
#include "Debug.h"
#include "CookingSystem.h"
#include "DepFile.h"
#include "BinaryReadWriter.h"
#include "Strings.h"
#include <Bedrock/Algorithm.h>
#include <Bedrock/Ticks.h>
#include <Bedrock/Random.h>
#include <Bedrock/StringFormat.h>

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"

#include "xxHash/xxh3.h"

#include <algorithm> // for std::sort, sad!

// Debug toggle to fake files failing to open, to test error handling.
bool             gDebugFailOpenFileRandomly = false;

// These are not exactly the max path length allowed by Windows in all cases, but should be good enough.
// This is meant for paths that might be outside repos that the monitoring code has to deal with.
// TODO use the smaller cMaxPathSizeUTF8 where appropriate instead.
constexpr size_t cWin32MaxPathSizeUTF16 = 32768;
constexpr size_t cWin32MaxPathSizeUTF8  = 32768 * 3ull;	// UTF8 can use up to 6 bytes per character, but let's suppose 3 is good enough on average.

using PathBufferUTF16 = wchar_t[cWin32MaxPathSizeUTF16];
using PathBufferUTF8  = char[cWin32MaxPathSizeUTF8];


// Hash the absolute path of a file in a case insensitive manner.
// That's used to get a unique identifier for the file even if the file itself doesn't exist.
// The hash is 128 bits, assume no collision.
// Clearly not the most efficient implementation, but good enough for now.
PathHash gHashPath(StringView inAbsolutePath)
{
	// Make sure it's normalized, absolute, and doesn't contain any relative components.
	gAssert(gIsNormalized(inAbsolutePath));
	gAssert(gIsAbsolute(inAbsolutePath));

	// Convert it to wide char.
	PathBufferUTF16     wpath_buffer;
	OptionalWStringView wpath = gUtf8ToWideChar(inAbsolutePath, wpath_buffer);
	if (!wpath)
		gApp.FatalError("Failed to convert path {} to WideChar", inAbsolutePath);

	// TODO use gToLowerCase instead?
	// Convert it to uppercase.
	// Note: LCMapStringA does not seem to work with UTF8 (or at least not with LOCALE_INVARIANT) so we are forced to use wchars here.
	PathBufferUTF16 uppercase_buffer;
	int uppercase_size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wpath->data(), (int)wpath->size(), uppercase_buffer, gElemCount(uppercase_buffer) / 2, nullptr, nullptr, 0);
	if (uppercase_size == 0)
		gApp.FatalError("Failed to convert path {} to uppercase", inAbsolutePath);

	WStringView uppercase_wpath = { uppercase_buffer, (size_t)uppercase_size };

	// Hash the uppercase version.
	XXH128_hash_t hash_xx = XXH3_128bits(uppercase_wpath.data(), uppercase_wpath.size() * sizeof(uppercase_wpath[0]));

	// Convert to our hash wrapper.
	PathHash      path_hash;
	static_assert(sizeof(path_hash.mData) == sizeof(hash_xx));
	memcpy(path_hash.mData, &hash_xx, sizeof(path_hash.mData));

	return path_hash;
}


_FILE_ID_128 FileRefNumber::ToWin32() const
{
	static_assert(sizeof(_FILE_ID_128) == sizeof(FileRefNumber));

	FILE_ID_128 id;
	memcpy(id.Identifier, mData, sizeof(id));
	return id;
}

FileRefNumber& FileRefNumber::operator=(const _FILE_ID_128& inFileID128)
{
	static_assert(sizeof(FILE_ID_128) == sizeof(FileRefNumber));

	memcpy(mData, inFileID128.Identifier, sizeof(inFileID128));
	return *this;
}



// Find the offset of the character after the last slash, or 0 if there's no slash.
static int16 sFindNamePos(StringView inPath)
{
	int offset = inPath.FindLastOf("\\/");
	if (offset != -1)
		return (int16)(offset + 1); // +1 because file name starts after the slash.
	else
		return 0; // No subdirs in this path, the start of the name is the start of the string.
}


// Find the offset of the last '.' in the path.
static int16 sFindExtensionPos(uint16 inNamePos, StringView inPath)
{
	StringView file_name = inPath.SubStr(inNamePos);

	int offset = file_name.FindLastOf(".");
	if (offset != -1)
		return (int16)offset + inNamePos;
	else
		return (int16)inPath.Size(); // No extension.
}


FileInfo::FileInfo(FileID inID, StringView inPath, Hash128 inPathHash, FileType inType, FileRefNumber inRefNumber)
	: mID(inID)
	, mNamePos(sFindNamePos(inPath))
	, mExtensionPos(sFindExtensionPos(mNamePos, inPath))
	, mPath(inPath)
	, mPathHash(inPathHash)
	, mIsDirectory(inType == FileType::Directory)
	, mIsDepFile(false)
	, mCommandsCreated(false)
	, mRefNumber(inRefNumber)
{
	gAssert(gIsNormalized(inPath));
}






static bool sShouldRetryLater(OpenFileError inError)
{
	// At this point, this is the only error where it makes sense to try again later.
	// Might change as we add more errors to the enum.
	return inError == OpenFileError::SharingViolation;
}

	 
FileRepo::FileRepo(uint32 inIndex, StringView inName, StringView inRootPath, FileDrive& inDrive)
	: mDrive(inDrive)
{
	// Store the index, name and root path.
	mIndex    = inIndex;
	mName     = mStringPool.AllocateCopy(inName);
	mRootPath = mStringPool.AllocateCopy(inRootPath);

	// Add this repo to the repo list in the drive.
	mDrive.mRepos.PushBack(this);

	// Make sure the root path exists.
	gCreateDirectoryRecursive(mRootPath);

	// Get a handle to the root path.
	OwnedHandle root_dir_handle = CreateFileA(mRootPath.AsCStr(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!root_dir_handle.IsValid())
		gApp.FatalError("Failed to get handle to {} - {}", mRootPath, GetLastErrorString());

	// Get the FileReferenceNumber of the root dir.
	FILE_ID_INFO file_info;
	if (!GetFileInformationByHandleEx(root_dir_handle, FileIdInfo, &file_info, sizeof(file_info)))
		gApp.FatalError("Failed to get FileReferenceNumber for {} - {}", mRootPath, GetLastErrorString());

	// The root directory file info has an empty path (relative to mRootPath).
	FileInfo& root_dir = GetOrAddFile("", FileType::Directory, file_info.FileId);
	mRootDirID = root_dir.mID;

	gApp.Log("Initialized FileRepo {} as {}:", mRootPath, mName);
}



FileInfo& FileRepo::GetOrAddFile(StringView inPath, FileType inType, FileRefNumber inRefNumber)
{
	// Make sure the path is normalized.
	TempPath path = inPath;
	gNormalizePath(path.AsSpan());

	// Calculate the case insensitive path hash that will be used to identify the file.
	PathHash  path_hash = gHashPath(TempPath("{}{}", mRootPath, path));

	FileInfo* file      = nullptr;

	{
		// Lock during the entire operation because we need to reserve a file ID.
		auto files_lock = mFiles.Lock();

		// Prepare a new FileID in case this file wasn't already added.
		FileID           new_file_id = { mIndex, (uint32)mFiles.SizeRelaxed() };
		FileID           actual_file_id;

		// Try to insert it to the path hash map.
		{
			// TODO: not great to access these internals, maybe find a better way?
			LockGuard map_lock(gFileSystem.mFilesByPathHashMutex);

			auto [it, inserted] = gFileSystem.mFilesByPathHash.insert({ path_hash, new_file_id });
			if (!inserted)
			{
				actual_file_id = it->second;

				if (inRefNumber.IsValid())
				{
					// If the file is already known, make sure we update the ref number.
					// The file could have been deleted and re-created (and we've missed the event?) and gotten a new ref number.
					FileInfo& file = GetFile(actual_file_id);
					if (file.mRefNumber != inRefNumber)
					{
						if (file.mRefNumber.IsValid())
							gApp.LogError("{} changed RefNumber unexpectedly (missed event?)", file);

						file.mRefNumber = inRefNumber;
					}
				}
			}
			else
			{
				actual_file_id = new_file_id;
			}
		}

		// Update the ref number hash map.
		if (inRefNumber.IsValid())
		{
			// TODO: not great to access these internals, maybe find a better way?
			LockGuard map_lock(mDrive.mFilesByRefNumberMutex);

			auto [it, inserted] = mDrive.mFilesByRefNumber.insert({ inRefNumber, actual_file_id });
			if (!inserted)
			{
				FileID previous_file_id = it->second;

				// Check if the existing file is the same (ie. same path).
				// The file could have been renamed but kept the same ref number (and we've missed that rename event?),
				// or it could be a junction/hardlink to the same file (TODO: detect that, at least to error properly?)
				if (previous_file_id != actual_file_id || previous_file_id.GetFile().mPathHash != path_hash)
				{
					gApp.LogError(R"(Found two files with the same RefNumber! {}:\{} and {}{})", 
						mDrive.mLetter, path,
						previous_file_id.GetRepo().mRootPath, previous_file_id.GetFile().mPath);

					// Mark the old file as deleted, and add the new one instead.
					MarkFileDeleted(GetFile(previous_file_id), {}, map_lock);
				}
			}
		}


		if (actual_file_id == new_file_id)
		{
			// The file wasn't already known, add it to the list.
			file = &mFiles.Emplace(files_lock, new_file_id, gNormalizePath(mStringPool.AllocateCopy(path)), path_hash, inType, inRefNumber);
		}
		else
		{
			// The file was known, return it.
			file = &GetFile(actual_file_id);

			if (file->GetType() != inType)
			{
				// TODO we could support changing the file type if we make sure to update any list of all directories
				gApp.FatalError("{} was a {} but is now a {}. This is not supported yet.",
					*file,
					file->GetType() == FileType::Directory ? "Directory" : "File",
					inType == FileType::Directory ? "Directory" : "File");
			}
		}
	}

	// Create all the commands that take this file as input (this may add more (non-existing) files).
	// Note: Don't do it during initial scan, it's not necessary as we'll do it afterwards anyway.
	if (gFileSystem.GetInitState() == FileSystem::InitState::Ready)
		gCookingSystem.CreateCommandsForFile(*file);

	return *file;
}

void FileRepo::MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp)
{
	// TODO: not great to access these internals, maybe find a better way?
	LockGuard lock(mDrive.mFilesByRefNumberMutex);

	MarkFileDeleted(ioFile, inTimeStamp, lock);
}

void FileRepo::MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp, const LockGuard<Mutex>& inLock)
{
	gAssert(inLock.GetMutex() == &mDrive.mFilesByRefNumberMutex);

	mDrive.mFilesByRefNumber.erase(ioFile.mRefNumber);
	ioFile.mRefNumber      = FileRefNumber::cInvalid();
	ioFile.mCreationTime   = inTimeStamp;	// Store the time of deletion in the creation time. 
	ioFile.mLastChangeTime = {};
	ioFile.mLastChangeUSN  = {};

	gCookingSystem.QueueUpdateDirtyStates(ioFile.mID);
}



StringView FileRepo::RemoveRootPath(StringView inFullPath)
{
	// inFullPath might be the root path without trailing slash, so ignore the trailing slash.
	gAssert(gStartsWithNoCase(inFullPath, gNoTrailingSlash(mRootPath)));

	// Use gMin also in case inFullPath is the root path without trailing slash.
	return inFullPath.SubStr(gMin(mRootPath.Size(), inFullPath.Size()));
}



static OptionalStringView sBuildFilePath(StringView inParentDirPath, WStringView inFileNameW, MutStringView ioBuffer)
{
	MutStringView file_path = ioBuffer;

	// Add the parent dir if there's one (can be empty for the root dir).
	if (!inParentDirPath.Empty())
	{
		ioBuffer = gAppend(ioBuffer, inParentDirPath);
		ioBuffer = gAppend(ioBuffer, "\\");
	}

	OptionalStringView file_name = gWideCharToUtf8(inFileNameW, ioBuffer);
	if (!file_name)
	{
		// Failed for some reason. Buffer too small?
		gAssert(false); // Investigate.

		return {};
	}

	return StringView{ file_path.Data(), gEndPtr(*file_name) };
}


// TODO: do the while loop to drain the scan queue in here, maybe put the queue and the buffer in a context param? (since they're not meaningful to the caller)
void FileRepo::ScanDirectory(FileID inDirectoryID, ScanQueue& ioScanQueue, Span<uint8> ioBuffer)
{
	const FileInfo& dir = GetFile(inDirectoryID); 
	gAssert(dir.IsDirectory());

	HandleOrError dir_handle = mDrive.OpenFileByRefNumber(dir.mRefNumber, OpenFileAccess::GenericRead, inDirectoryID);
	if (!dir_handle.IsValid())
	{
		// If the directory exists but it failed, retry later.
		if (sShouldRetryLater(dir_handle.mError))
			gFileSystem.RescanLater(inDirectoryID);

		return;
	}

	if (gApp.mLogFSActivity >= LogLevel::Verbose)
		gApp.Log("Added {}", dir);

	// First GetFileInformationByHandleEx call needs a different value to make it "restart".
	FILE_INFO_BY_HANDLE_CLASS file_info_class = FileIdExtdDirectoryRestartInfo;

	while (true)
	{
		// Fill the scan buffer with content to iterate.
		if (!GetFileInformationByHandleEx(*dir_handle, file_info_class, ioBuffer.Data(), ioBuffer.Size()))
		{
			if (GetLastError() == ERROR_NO_MORE_FILES)
				break; // Finished iterating, exit the loop.

			gApp.FatalError("Enumerating {} failed - {}", dir, GetLastErrorString());
		}

		// Next time keep iterating instead of restarting.
		file_info_class = FileIdExtdDirectoryInfo;

		// Iterate on entries in the buffer.
		PFILE_ID_EXTD_DIR_INFO entry = (PFILE_ID_EXTD_DIR_INFO)ioBuffer.Data();
		bool last_entry = false;
		do
		{
			// Defer iterating to the next entry so that we can use continue.
			defer
			{
				if (entry->NextEntryOffset == 0)
					last_entry = true;
				entry = (PFILE_ID_EXTD_DIR_INFO)((uint8*)entry + entry->NextEntryOffset);
			};

			WStringView wfilename = { entry->FileName, entry->FileNameLength / 2 };

			// Ignore current/parent dir.
			if (wfilename == L"." || wfilename == L"..")
				continue;

			// Build the file path.
			PathBufferUTF8     path_buffer;
			OptionalStringView path = sBuildFilePath(dir.mPath, wfilename, path_buffer);

			// If it fails, ignore the file.
			if (!path)
			{
				gApp.LogError("Failed to build the path of a file in {}", dir);
				gAssert(false); // Investigate why that would happen.
				continue;
			}

			// Check if it's a directory.
			const bool is_directory = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			// Add (or get) the file info.
			FileInfo& file = GetOrAddFile(*path, is_directory ? FileType::Directory : FileType::File, entry->FileId);

			if (gApp.mLogFSActivity >= LogLevel::Verbose)
				gApp.Log("Added {}", file);

			if (file.IsDirectory())
			{
				// Add directories to the scan queue.
				ioScanQueue.Push(file.mID);
			}
			else
			{
				file.mCreationTime   = entry->CreationTime.QuadPart;
				file.mLastChangeTime = entry->ChangeTime.QuadPart;

				// Update the USN.
				// Note: Don't do it during the initial scan because it's not fast enough to do it on many files. We'll read the entire USN journal later instead.
				if (gFileSystem.GetInitState() == FileSystem::InitState::Ready)
					ScanFile(file, RequestedAttributes::USNOnly);

				gCookingSystem.QueueUpdateDirtyStates(file.mID);
			}

		} while (!last_entry);

		if (false && gApp.mLogFSActivity >= LogLevel::Verbose)
		{
			// Print how much of the buffer was used, to help sizing that buffer.
			// Seems most folders need <1 KiB but saw one that used 30 KiB.
			uint8* buffer_end = (uint8*)entry->FileName + entry->FileNameLength;
			gApp.Log("Used {} of {} buffer.", SizeInBytes(buffer_end - ioBuffer.Data()), SizeInBytes(ioBuffer.Size()));
		}
	}
}


void FileRepo::ScanFile(FileInfo& ioFile, RequestedAttributes inRequestedAttributes)
{
	HandleOrError file_handle = mDrive.OpenFileByRefNumber(ioFile.mRefNumber, OpenFileAccess::GenericRead, ioFile.mID);
	if (!file_handle.IsValid())
	{
		// If the file exists but it failed, retry later.
		if (sShouldRetryLater(file_handle.mError))
			gFileSystem.RescanLater(ioFile.mID);

		return;
	}

	ioFile.mLastChangeUSN = mDrive.GetUSN(*file_handle);

	if (inRequestedAttributes == RequestedAttributes::All)
	{
		FILE_BASIC_INFO basic_info = {};

		if (!GetFileInformationByHandleEx(*file_handle, FileBasicInfo, &basic_info, sizeof(basic_info)))
		{
			// Note: for now don't force a rescan if that fails because we only use the file times for display,
			// and it's unclear why it would fail/if a rescan would fix it.
			gApp.LogError("Getting attributes for {} failed - {}", ioFile, GetLastErrorString());

			return;
		}

		ioFile.mCreationTime   = basic_info.CreationTime.QuadPart;
		ioFile.mLastChangeTime = basic_info.ChangeTime.QuadPart;
	}
}


FileDrive::FileDrive(char inDriveLetter)
{
	// Store the drive letter;
	mLetter = inDriveLetter;

	// Get a handle to the drive.
	// Note: Only request FILE_TRAVERSE to make that work without admin rights.
	mHandle = CreateFileA(gTempFormat(R"(\\.\%c:)", mLetter).AsCStr(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!mHandle.IsValid())
		gApp.FatalError(R"(Failed to get handle to {}:\ - {})", mLetter, GetLastErrorString());

	// Query the USN journal to get its ID.
	USN_JOURNAL_DATA_V0 journal_data;
	uint32				unused;
	if (!DeviceIoControl(mHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &unused, nullptr))
		gApp.FatalError(R"(Failed to query USN journal for {}:\ - {})", mLetter, GetLastErrorString());

	// Store the jorunal ID.
	mUSNJournalID = journal_data.UsnJournalID;

	// Store the first USN. This will be used to know if the cached state is usable.
	mFirstUSN = journal_data.FirstUsn;
	// Store the next USN. This will be overwritten if the cached state is usable.
	mNextUSN  = journal_data.NextUsn;

	gApp.Log(R"(Queried USN journal for {}:\. ID: 0x{:08X}. Max size: {})", mLetter, mUSNJournalID, SizeInBytes(journal_data.MaximumSize));
}


HandleOrError FileDrive::OpenFileByRefNumber(FileRefNumber inRefNumber, OpenFileAccess inDesiredAccess, FileID inFileID) const
{
	FILE_ID_DESCRIPTOR file_id_descriptor;
	file_id_descriptor.dwSize			= sizeof(FILE_ID_DESCRIPTOR);
	file_id_descriptor.Type				= ExtendedFileIdType;
	file_id_descriptor.ExtendedFileId	= inRefNumber.ToWin32();

	constexpr DWORD flags_and_attributes = 0
		| FILE_FLAG_BACKUP_SEMANTICS	// Required to open directories.
		//| FILE_FLAG_SEQUENTIAL_SCAN	 // Helps prefetching if we only read sequentially. Useful if we want to hash the files?
	;

	DWORD desired_access = 0;
	switch (inDesiredAccess)
	{
	case OpenFileAccess::GenericRead:
		desired_access = FILE_GENERIC_READ;
		break;
	case OpenFileAccess::AttributesOnly:
		desired_access = FILE_READ_ATTRIBUTES;
		break;
	}

	OwnedHandle handle = OpenFileById(mHandle, &file_id_descriptor, desired_access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, flags_and_attributes);

	// Fake random failures for debugging.
	if (gDebugFailOpenFileRandomly && (gRand32() % 5) == 0)
	{
		if ((gRand32() % 5) == 0)
			SetLastError(ERROR_INVALID_PARAMETER);
		else
			SetLastError(ERROR_SHARING_VIOLATION);
		handle = {};
	}

	if (!handle.IsValid())
	{
		// Helper lambda to try to get a sensible string for the file being opened.
		auto BuildFileStr = [this](FileRefNumber inRefNumber, FileID inFileID) -> TempString {

			// Try to find the FileInfo for that ref number.
			FileID file_id = inFileID.IsValid() ? inFileID : FindFileID(inRefNumber);

			// Turn it into a string.
			if (file_id.IsValid())
				return gToString(file_id.GetFile());
			else
				return "Unknown";
		};

		uint32 error = GetLastError();

		// In non-verbose mode, don't log errors unless we know they're about a file we care about.
		if (gApp.mLogFSActivity >= LogLevel::Verbose || inFileID.IsValid())
			gApp.LogError("Failed to open {} ({}) - {}", BuildFileStr(inRefNumber, inFileID), inRefNumber, GetLastErrorString());
		
		// Some errors are okay, and we can just ignore the file or try to open it again later.
		// Some are not okay, and we throw a fatal error.
		// The list of okay error is probably incomplete, needs to be amended as we discover them.
		if (error == ERROR_SHARING_VIOLATION)
			return OpenFileError::SharingViolation;
		else if (error == ERROR_ACCESS_DENIED)
			return OpenFileError::AccessDenied;
		else if (error == ERROR_INVALID_PARAMETER	// Yes, invalid parameter means file does not exist (anymore).
			|| error == ERROR_FILE_NOT_FOUND		// This one can also happens sometimes (when the directory is deleted?).
			|| error == ERROR_CANT_ACCESS_FILE)		// Unsure what this means but I've seen it happen for an unknown file on C:/ once.
			return OpenFileError::FileNotFound;

		gApp.FatalError("Failed to open {} ({}) - {}", BuildFileStr(inRefNumber, inFileID), inRefNumber, GetLastErrorString());
	}

	return handle;
}


OptionalStringView FileDrive::GetFullPath(const OwnedHandle& inFileHandle, MutStringView ioBuffer) const
{
	// Get the full path as utf16.
	// PFILE_NAME_INFO contains the filename without the drive letter and column in front (ie. without the C:).
	PathBufferUTF16 wpath_buffer;
	PFILE_NAME_INFO file_name_info = (PFILE_NAME_INFO)wpath_buffer;
	if (!GetFileInformationByHandleEx(inFileHandle, FileNameInfo, file_name_info, gElemCount(wpath_buffer) * sizeof(wpath_buffer[0])))
		return {};

	WStringView wpath = { file_name_info->FileName, file_name_info->FileNameLength / 2 };

	// Write the drive part.
	ioBuffer[0] = mLetter;
	ioBuffer[1] = ':';

	// Write the path part.
	OptionalStringView path_part = gWideCharToUtf8(wpath, ioBuffer.SubSpan(2));
	if (!path_part)
		return {};

	return StringView{ ioBuffer.Data(), gEndPtr(*path_part) };
}


USN FileDrive::GetUSN(const OwnedHandle& inFileHandle) const
{
	PathBufferUTF16 buffer;
	DWORD available_bytes = 0;
	if (!DeviceIoControl(inFileHandle, FSCTL_READ_FILE_USN_DATA, nullptr, 0, buffer, gElemCount(buffer) * sizeof(buffer[0]), &available_bytes, nullptr))
		gApp.FatalError("Failed to get USN data"); // TODO add file path to message

	auto record_header = (USN_RECORD_COMMON_HEADER*)buffer;
	if (record_header->MajorVersion == 2)
	{
		auto record = (USN_RECORD_V2*)buffer;
		return record->Usn;	
	}
	else if (record_header->MajorVersion == 3)
	{
		auto record = (USN_RECORD_V3*)buffer;
		return record->Usn;
	}
	else
	{
		gApp.FatalError("Got unexpected USN record version ({}.{})", record_header->MajorVersion, record_header->MinorVersion);
		return 0;
	}
}

enum class USNReasons : uint32
{
	DATA_OVERWRITE					= 0x00000001,
	DATA_EXTEND						= 0x00000002,
	DATA_TRUNCATION					= 0x00000004,
	NAMED_DATA_OVERWRITE			= 0x00000010,
	NAMED_DATA_EXTEND				= 0x00000020,
	NAMED_DATA_TRUNCATION			= 0x00000040,
	FILE_CREATE						= 0x00000100,
	FILE_DELETE						= 0x00000200,
	EA_CHANGE						= 0x00000400,
	SECURITY_CHANGE					= 0x00000800,
	RENAME_OLD_NAME					= 0x00001000,
	RENAME_NEW_NAME					= 0x00002000,
	INDEXABLE_CHANGE				= 0x00004000,
	BASIC_INFO_CHANGE				= 0x00008000,
	HARD_LINK_CHANGE				= 0x00010000,
	COMPRESSION_CHANGE				= 0x00020000,
	ENCRYPTION_CHANGE				= 0x00040000,
	OBJECT_ID_CHANGE				= 0x00080000,
	REPARSE_POINT_CHANGE			= 0x00100000,
	STREAM_CHANGE					= 0x00200000,
	TRANSACTED_CHANGE				= 0x00400000,
	INTEGRITY_CHANGE				= 0x00800000,
	DESIRED_STORAGE_CLASS_CHANGE	= 0x01000000,
	CLOSE							= 0x80000000,
};

constexpr uint32 operator|(USNReasons inA, USNReasons inB) { return (uint32)inA | (uint32)inB; }
constexpr uint32 operator|(uint32 inA, USNReasons inB) { return inA | (uint32)inB; }
constexpr uint32 operator&(USNReasons inA, USNReasons inB) { return (uint32)inA & (uint32)inB; }
constexpr uint32 operator&(uint32 inA, USNReasons inB) { return inA & (uint32)inB; }
constexpr uint32 operator&(USNReasons inA, uint32 inB) { return (uint32)inA & inB; }

TempString gToString(USNReasons inReasons)
{
	TempString str;

	if (inReasons & USNReasons::DATA_OVERWRITE				) str.Append(" DATA_OVERWRITE");
	if (inReasons & USNReasons::DATA_EXTEND					) str.Append(" DATA_EXTEND");
	if (inReasons & USNReasons::DATA_TRUNCATION				) str.Append(" DATA_TRUNCATION");
	if (inReasons & USNReasons::NAMED_DATA_OVERWRITE		) str.Append(" NAMED_DATA_OVERWRITE");
	if (inReasons & USNReasons::NAMED_DATA_EXTEND			) str.Append(" NAMED_DATA_EXTEND");
	if (inReasons & USNReasons::NAMED_DATA_TRUNCATION		) str.Append(" NAMED_DATA_TRUNCATION");
	if (inReasons & USNReasons::FILE_CREATE					) str.Append(" FILE_CREATE");
	if (inReasons & USNReasons::FILE_DELETE					) str.Append(" FILE_DELETE");
	if (inReasons & USNReasons::EA_CHANGE					) str.Append(" EA_CHANGE");
	if (inReasons & USNReasons::SECURITY_CHANGE				) str.Append(" SECURITY_CHANGE");
	if (inReasons & USNReasons::RENAME_OLD_NAME				) str.Append(" RENAME_OLD_NAME");
	if (inReasons & USNReasons::RENAME_NEW_NAME				) str.Append(" RENAME_NEW_NAME");
	if (inReasons & USNReasons::INDEXABLE_CHANGE			) str.Append(" INDEXABLE_CHANGE");
	if (inReasons & USNReasons::BASIC_INFO_CHANGE			) str.Append(" BASIC_INFO_CHANGE");
	if (inReasons & USNReasons::HARD_LINK_CHANGE			) str.Append(" HARD_LINK_CHANGE");
	if (inReasons & USNReasons::COMPRESSION_CHANGE			) str.Append(" COMPRESSION_CHANGE");
	if (inReasons & USNReasons::ENCRYPTION_CHANGE			) str.Append(" ENCRYPTION_CHANGE");
	if (inReasons & USNReasons::OBJECT_ID_CHANGE			) str.Append(" OBJECT_ID_CHANGE");
	if (inReasons & USNReasons::REPARSE_POINT_CHANGE		) str.Append(" REPARSE_POINT_CHANGE");
	if (inReasons & USNReasons::STREAM_CHANGE				) str.Append(" STREAM_CHANGE");
	if (inReasons & USNReasons::TRANSACTED_CHANGE			) str.Append(" TRANSACTED_CHANGE");
	if (inReasons & USNReasons::INTEGRITY_CHANGE			) str.Append(" INTEGRITY_CHANGE");
	if (inReasons & USNReasons::DESIRED_STORAGE_CLASS_CHANGE) str.Append(" DESIRED_STORAGE_CLASS_CHANGE");
	if (inReasons & USNReasons::CLOSE						) str.Append(" CLOSE");

	return str;
}


template <typename taFunctionType>
USN FileDrive::ReadUSNJournal(USN inStartUSN, Span<uint8> ioBuffer, taFunctionType inRecordCallback) const
{
	USN start_usn = inStartUSN;

	while (true)
	{
		constexpr uint32 cInterestingReasons = USNReasons::FILE_CREATE |     // File was created.
											   USNReasons::FILE_DELETE |     // File was deleted.
											   USNReasons::DATA_OVERWRITE |  // File was modified.
											   USNReasons::DATA_EXTEND |     // File was modified.
											   USNReasons::DATA_TRUNCATION | // File was modified.
											   USNReasons::RENAME_NEW_NAME;  // File was renamed or moved (possibly to the recyle bin). That's essentially a delete and a create.

		READ_USN_JOURNAL_DATA_V1 journal_data;
		journal_data.StartUsn          = start_usn;
		journal_data.ReasonMask        = cInterestingReasons | USNReasons::CLOSE; 
		journal_data.ReturnOnlyOnClose = true;          // Only get events when the file is closed (ie. USN_REASON_CLOSE is present). We don't care about earlier events.
		journal_data.Timeout           = 0;				// Never wait.
		journal_data.BytesToWaitFor    = 0;				// Never wait.
		journal_data.UsnJournalID      = mUSNJournalID;	// The journal we're querying.
		journal_data.MinMajorVersion   = 3;				// Doc says it needs to be 3 to use 128-bit file identifiers (ie. FileRefNumbers).
		journal_data.MaxMajorVersion   = 3;				// Don't want to support anything else.
		
		// Note: Use FSCTL_READ_UNPRIVILEGED_USN_JOURNAL to make that work without admin rights.
		uint32 available_bytes;
		if (!DeviceIoControl(mHandle, FSCTL_READ_UNPRIVILEGED_USN_JOURNAL, &journal_data, sizeof(journal_data), ioBuffer.Data(), (uint32)ioBuffer.Size(), &available_bytes, nullptr))
		{
			// TODO: test this but probably the only thing to do is to restart and re-scan everything (maybe the journal was deleted?)
			gApp.FatalError("Failed to read USN journal for {}:\\ - {}", mLetter, GetLastErrorString());
		}

		Span<uint8> available_buffer = ioBuffer.SubSpan(0, available_bytes);

		USN next_usn = *(USN*)available_buffer.Data();
		available_buffer = available_buffer.SubSpan(sizeof(USN));

		if (next_usn == start_usn)
		{
			// Nothing more to read.
			break;
		}

		// Update the USN for next time.
		start_usn = next_usn;

		while (!available_buffer.Empty())
		{
			const USN_RECORD_V3* record = (USN_RECORD_V3*)available_buffer.Data();

			// Defer iterating to the next record so that we can use continue.
			defer { available_buffer = available_buffer.SubSpan(record->RecordLength); };

			// We get all events where USN_REASON_CLOSE is present, but we don't care about all of them.
			if ((record->Reason & cInterestingReasons) == 0)
				continue;

			// If the file is created and deleted in the same record, ignore it. We can't get any info about the file because it's already gone.
			// I think that can happen because of ReturnOnlyOnClose but unsure (ie. a file is created then deleted before its last handle was closed).
			// Happens a lot when monitoring C:/
			if ((record->Reason & (USNReasons::FILE_DELETE | USNReasons::FILE_CREATE)) == (USNReasons::FILE_DELETE | USNReasons::FILE_CREATE))
				continue;

			inRecordCallback(*record);
		}
	}

	return start_usn;
}


bool FileDrive::ProcessMonitorDirectory(Span<uint8> ioBufferUSN, ScanQueue &ioScanQueue, Span<uint8> ioBufferScan)
{
	USN next_usn = ReadUSNJournal(mNextUSN, ioBufferUSN, [this, ioBufferScan, &ioScanQueue](const USN_RECORD_V3& inRecord)
	{
		bool is_directory = (inRecord.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

		USNReasons reason = (USNReasons)inRecord.Reason;

		if (reason & (USNReasons::FILE_DELETE | USNReasons::RENAME_NEW_NAME))
		{
			// If the file is in a repo, mark it as deleted.
			FileID deleted_file_id = FindFileID(inRecord.FileReferenceNumber);
			if (deleted_file_id.IsValid())
			{
				FileInfo& deleted_file = deleted_file_id.GetFile();
				FileTime  timestamp    = inRecord.TimeStamp.QuadPart;

				FileRepo& repo = gFileSystem.GetRepo(deleted_file.mID);

				repo.MarkFileDeleted(deleted_file, timestamp);

				if (gApp.mLogFSActivity >= LogLevel::Verbose)
					gApp.Log("Deleted {}", deleted_file);

				// If it's a directory, also mark all the file inside as deleted.
				if (deleted_file.IsDirectory())
				{
					TempString dir_path = deleted_file.mPath;

					// Root dir has an empty path, in this case don't add the slash.
					if (!dir_path.Empty())
						dir_path += "\\";

					for (FileInfo& file : repo.mFiles)
					{
						if (file.mID != deleted_file.mID && gStartsWithNoCase(file.mPath, dir_path))
						{
							repo.MarkFileDeleted(file, timestamp);

							if (gApp.mLogFSActivity >= LogLevel::Verbose)
								gApp.Log("Deleted {}", file);
						}
					}
				}
			}

		}

		if (reason & (USNReasons::FILE_CREATE | USNReasons::RENAME_NEW_NAME))
		{
			// Get a handle to the file.
			HandleOrError file_handle = OpenFileByRefNumber(inRecord.FileReferenceNumber, OpenFileAccess::AttributesOnly, FileID::cInvalid());
			if (!file_handle.IsValid())
			{
				// This can fail for many reasons when monitoring a drive that also contains eg. Windows.
				// Files are created then deleted constantly, some files need admin privileges, etc.
				// We can't get their path, so we can't know if we should care. Probably we don't. C'est la vie.
				return;
			}

			// Get its path.
			PathBufferUTF8     buffer;
			OptionalStringView full_path = GetFullPath(*file_handle, buffer);
			if (!full_path)
			{
				// TODO: same remark as failing to open
				gApp.LogError("Failed to get path for newly created file {} - {}", FileRefNumber(inRecord.FileReferenceNumber), GetLastErrorString());
				return;
			}

			// Check if it's in a repo, otherwise ignore.
			FileRepo* repo = FindRepoForPath(*full_path);
			if (repo)
			{
				// Get the file path relative to the repo root.
				StringView file_path = repo->RemoveRootPath(*full_path);

				// Add the file.
				FileInfo& file = repo->GetOrAddFile(file_path, is_directory ? FileType::Directory : FileType::File, inRecord.FileReferenceNumber);

				if (is_directory)
				{
					// If it's a directory, scan it to add all the files inside.
					ioScanQueue.Push(file.mID);

					FileID dir_id;
					while ((dir_id = ioScanQueue.Pop()) != FileID::cInvalid())
						repo->ScanDirectory(dir_id, ioScanQueue, ioBufferScan);
				}
				else
				{
					// If it's a file, treat it as if it was modified.
					if (gApp.mLogFSActivity >= LogLevel::Verbose)
						gApp.Log("Added {}", file);

					file.mLastChangeUSN  = inRecord.Usn;
					file.mLastChangeTime = inRecord.TimeStamp.QuadPart;

					gCookingSystem.QueueUpdateDirtyStates(file.mID);
				}
			}
		}
		else
		{
			// The file was just modified, update its USN.
			FileID file_id = FindFileID(inRecord.FileReferenceNumber);
			if (file_id.IsValid())
			{
				FileInfo& file = file_id.GetFile();

				if (gApp.mLogFSActivity >= LogLevel::Verbose)
					gApp.Log("Modified {}", file);

				file.mLastChangeUSN  = inRecord.Usn;
				file.mLastChangeTime = inRecord.TimeStamp.QuadPart;

				gCookingSystem.QueueUpdateDirtyStates(file.mID);
			}
		}
	});

	if (next_usn == mNextUSN)
		return false;

	mNextUSN = next_usn;
	return true;
}


FileRepo* FileDrive::FindRepoForPath(StringView inFullPath)
{
	gAssert(inFullPath[0] == mLetter);

	for (FileRepo* repo : mRepos)
	{
		// inFullPath might the root path without trailing slash, so ignore that trailing slash.
		if (gStartsWithNoCase(inFullPath, gNoTrailingSlash(repo->mRootPath)))
			return repo;
	}

	return nullptr;
}


FileID FileDrive::FindFileID(FileRefNumber inRefNumber) const
{
	LockGuard lock(mFilesByRefNumberMutex);

	auto it = mFilesByRefNumber.find(inRefNumber);
	if (it != mFilesByRefNumber.end())
		return it->second;

	return {};
}


void FileSystem::StartMonitoring()
{
	// Start the directory monitor thread.
	mMonitorDirThread.Create({ 
		.mName = "Monitor Directory Thread",
		.mTempMemSize = 1_MiB,
	}, [this](Thread& ioThread) { MonitorDirectoryThread(ioThread); });
}


void FileSystem::StopMonitoring()
{
	if (!IsMonitoringStarted())
		return;

	mMonitorDirThread.RequestStop();
	KickMonitorDirectoryThread();
	mMonitorDirThread.Join();

	// Also stop the cooking since we started it.
	gCookingSystem.StopCooking();
}


FileRepo* FileSystem::FindRepo(StringView inRepoName)
{
	for (FileRepo& repo : mRepos)
		if (inRepoName == repo.mName)
			return &repo;

	return nullptr;
}


FileRepo* FileSystem::FindRepoByPath(StringView inAbsolutePath)
{
	for (FileRepo& repo : mRepos)
		if (gStartsWithNoCase(inAbsolutePath, repo.mRootPath))
			return &repo;

	return nullptr;
}


FileDrive* FileSystem::FindDrive(char inLetter)
{
	for (FileDrive& drive : mDrives)
		if (drive.mLetter == inLetter)
			return &drive;

	return nullptr;
}


FileID FileSystem::FindFileIDByPath(StringView inAbsolutePath) const
{
	return FindFileIDByPathHash(gHashPath(inAbsolutePath));
}


FileID FileSystem::FindFileIDByPathHash(PathHash inPathHash) const
{
	LockGuard lock(mFilesByPathHashMutex);

	auto it = mFilesByPathHash.find(inPathHash);
	if (it != mFilesByPathHash.end())
		return it->second;

	return {};
}

void FileSystem::AddRepo(StringView inName, StringView inRootPath)
{
	gAssert(!IsMonitoringStarted()); // Can't add repos once the threads have started, it's not thread safe!
	gAssert(gIsNullTerminated(inRootPath));

	// Check that the name is unique.
	for (auto& repo : mRepos)
	{
		if (repo.mName == inName)
			gApp.FatalError("Failed to init FileRepo {} ({}) - There is already a repo with that name.", inName, inRootPath);
	}

	// Get the absolute path (in case it's relative).
	TempPath root_path = gGetAbsolutePath(inRootPath);

	gAssert(gIsNormalized(root_path));

	// Add a trailing slash if there isn't one.
	if (!gEndsWith(root_path, "\\"))
		root_path.Append("\\");

	// Check if it overlaps with other repos.
	for (auto& repo : mRepos)
	{
		if (gStartsWith(repo.mRootPath, root_path))
		{
			gApp.FatalError("Failed to init FileRepo {} ({}) - Another FileRepo is inside its root path ({} {}).", 
				inName, inRootPath, 
				repo.mName, repo.mRootPath);
		}

		if (gStartsWith(root_path, repo.mRootPath))
		{
			gApp.FatalError("Failed to init FileRepo {} ({}) - Root Path is inside another FileRepo ({} {}).", 
				inName, inRootPath, 
				repo.mName, repo.mRootPath);
		}
	}

	mRepos.Emplace({}, (uint32)mRepos.Size(), inName, root_path, GetOrAddDrive(root_path[0]));
}


FileDrive& FileSystem::GetOrAddDrive(char inDriveLetter)
{
	for (FileDrive& drive : mDrives)
		if (drive.mLetter == inDriveLetter)
			return drive;

	return mDrives.Emplace({}, inDriveLetter);
}


bool FileSystem::CreateDirectory(FileID inFileID)
{
	const FileInfo& file = GetFile(inFileID);
	const FileRepo& repo = GetRepo(inFileID);

	TempString abs_path = repo.mRootPath;
	abs_path += file.GetDirectory();

	bool success = gCreateDirectoryRecursive(abs_path);

	if (!success)
		gApp.LogError("Failed to create directory for {}", file);

	return success;
}


bool FileSystem::DeleteFile(FileID inFileID)
{
	const FileInfo& file = GetFile(inFileID);
	const FileRepo& repo = GetRepo(inFileID);

	TempString abs_path = repo.mRootPath;
	abs_path += file.mPath;

	bool success = DeleteFileA(abs_path.AsCStr());

	if (!success)
		gApp.LogError("Failed to delete {} - {}", abs_path, GetLastErrorString());

	return success;
}


size_t FileSystem::GetFileCount() const
{
	size_t file_count = 0;
	for (const FileRepo& repo : mRepos)
		file_count += repo.mFiles.Size();
	return file_count;
}



void FileSystem::InitialScan(const Thread& inInitialScanThread, Span<uint8> ioBufferUSN)
{
	// Early out if we have everything from the cache already.
	{
		bool all_cached = true;

		for (const FileDrive& drive : mDrives)
			if (!drive.mLoadedFromCache)
				all_cached = false;

		if (all_cached)
			return;
	}

	gApp.Log("Starting initial scan.");
	Timer timer;
	mInitState.Store(InitState::Scanning);

	// Don't use too many threads otherwise they'll just spend their time on the hashmap mutex.
	// TODO this could be improved
	constexpr int cMaxScanThreadCount = 4;
	const int scan_thread_count = gMin((int)std::thread::hardware_concurrency(), cMaxScanThreadCount);

	// Prepare a scan queue that can be used by multiple threads.
	ScanQueue scan_queue;
	scan_queue.mDirectories.Reserve(1024);
	scan_queue.mThreadsBusy = scan_thread_count; // All threads start busy.

	// Put the root dir of each repo in the queue.
	for (FileRepo& repo : mRepos)
	{
		// Skip repos that were already loaded from the cache.
		if (repo.mDrive.mLoadedFromCache)
			continue;

		scan_queue.Push(repo.mRootDirID);
	}

	// Create temporary worker threads to scan directories.
	{
		Thread scan_threads[cMaxScanThreadCount];
		for (auto& thread : Span(scan_threads, scan_thread_count))
		{
			thread.Create({ .mName = "Scan Directory Thread" }, [&](Thread&) 
			{
				uint8 buffer_scan[32 * 1024];

				// Process the queue until it's empty.
				FileID dir_id;
				while ((dir_id = scan_queue.Pop()) != FileID::cInvalid())
				{
					FileRepo& repo = gFileSystem.GetRepo(dir_id);

					repo.ScanDirectory(dir_id, scan_queue, buffer_scan);

					if (inInitialScanThread.IsStopRequested())
						return;
				}
			});
		}

		// Wait for the threads to finish their work.
		for (auto& thread : scan_threads)
			thread.Join();

		if (inInitialScanThread.IsStopRequested())
			return;
	}

	gAssert(scan_queue.mDirectories.Empty());
	gAssert(scan_queue.mThreadsBusy == 0);

	size_t total_files = 0;
	for (auto& repo : mRepos)
		if (!repo.mDrive.mLoadedFromCache)
			total_files += repo.mFiles.SizeRelaxed();

	gApp.Log("Done. Found {} files in {:.2f} seconds.", 
		total_files, gTicksToSeconds(timer.GetTicks()));

	mInitState.Store(InitState::ReadingUSNJournal);

	for (FileDrive& drive : mDrives)
	{
		// Skip drives that were already loaded from the cache.
		if (drive.mLoadedFromCache)
			continue;

		timer.Reset();
		gApp.Log("Reading USN journal for {}:\\.", drive.mLetter);

		int file_count = 0;

		// Read the entire USN journal to get the last USN for as many files as possible.
		// This is faster than requesting USN for individual files even though we have to browse a lot of record.
		USN start_usn = 0;
		drive.ReadUSNJournal(start_usn, ioBufferUSN, [this, &drive, &file_count](const USN_RECORD_V3& inRecord) 
		{
			// If the file is in one of the repos, update its USN.
			FileID file_id = drive.FindFileID(inRecord.FileReferenceNumber);
			if (file_id.IsValid())
			{
				file_count++;
				file_id.GetFile().mLastChangeUSN = inRecord.Usn;
			}
		});

		gApp.Log("Done. Found USN for {} files in {:.2f} seconds.", file_count, gTicksToSeconds(timer.GetTicks()));
	}

	// Files that haven't been touched in a while might not be referenced in the USN journal anymore.
	// For these, we'll need to fetch the last USN manually.
	TempVector<FileID> files_without_usn;
	for (auto& repo : mRepos)
	{
		// Skip repos that were loaded from the cache.
		if (repo.mDrive.mLoadedFromCache)
			continue;

		for (auto& file : repo.mFiles)
		{
			if (file.IsDeleted() || file.IsDirectory())
				continue;

			// Already got a USN?
			if (file.mLastChangeUSN == 0)
				files_without_usn.PushBack(file.mID);
		}
	}

	if (inInitialScanThread.IsStopRequested())
		return;

	mInitStats.mIndividualUSNToFetch = files_without_usn.Size();
	mInitStats.mIndividualUSNFetched.Store(0);
	mInitState.Store(InitState::ReadingIndividualUSNs);

	if (!files_without_usn.Empty())
	{
		gApp.Log("{} files were not present in the USN journal. Fetching their USN manually now.", files_without_usn.Size());

		// Don't create too many threads because they'll get stuck in locks in OpenFileByRefNumber if the cache is warm,
		// or they'll be bottlenecked by IO otherwise.
		constexpr int cMaxUSNThreadCount = 4;
		const int     usn_thread_count   = gMin((int)std::thread::hardware_concurrency(), cMaxUSNThreadCount);

		// Create temporary worker threads to get all the missing USNs.
		Thread usn_threads[cMaxUSNThreadCount];
		AtomicInt32 current_index = 0;
		for (auto& thread : Span(usn_threads, usn_thread_count))
		{
			thread.Create({ .mName = "USN Read Thread" }, [&](Thread&)
			{
				// Note: Could do better than having all threads hammer the same atomic, but the cost is negligible compared to OpenFileByRefNumber.
				int index;
				while ((index = current_index.Add(1)) < files_without_usn.Size())
				{
					FileID      file_id     = files_without_usn[index];
					FileRepo&   repo        = file_id.GetRepo();
					FileInfo&   file        = file_id.GetFile();

					// Get the USN.
					repo.ScanFile(file, FileRepo::RequestedAttributes::USNOnly);

					mInitStats.mIndividualUSNFetched.Add(1);

					if (inInitialScanThread.IsStopRequested())
						return;
				}
			});
		}

		// Wait for the threads to finish their work.
		for (auto& thread : usn_threads)
			thread.Join();

		if (inInitialScanThread.IsStopRequested())
			return;

		gApp.Log("Done. Fetched {} individual USNs in {:.2f} seconds.", 
			files_without_usn.Size(), gTicksToSeconds(timer.GetTicks()));
	}
}


void FileSystem::KickMonitorDirectoryThread()
{
	mMonitorDirThreadSignal.Set();
}


// TODO this is doing a bit more than monitoring the filesystem, give it a more general name and move to app?
void FileSystem::MonitorDirectoryThread(const Thread& inThread)
{
	using namespace std::chrono_literals;

	// Start not idle.
	mIsMonitorDirThreadIdle.Store(false);

	// Allocate a working buffer for querying the USN journal and scanning directories.
	static constexpr size_t cBufferSize = 64 * 1024ull;
	uint8* buffer_ptr  = (uint8*)malloc(cBufferSize);
	defer { free(buffer_ptr); };

	// Split it in two, one for USN reads, one for directory reads.
	Span buffer_usn  = { buffer_ptr,					cBufferSize / 2 };
	Span buffer_scan = { buffer_ptr + cBufferSize / 2,	cBufferSize / 2 };

	ScanQueue scan_queue;

	// Load the cached state.
	LoadCache();

	// For drives initialized from the cache, read all the changes since the cache was saved.
	// This needs to be done before processing dirty states/starting cooking, as we don't know the final state of the files yet.
	for (auto& drive : mDrives)
	{
		if (drive.mLoadedFromCache == false)
			continue;

		// Process the queue.
		while (drive.ProcessMonitorDirectory(buffer_usn, scan_queue, buffer_scan))
		{
			if (inThread.IsStopRequested())
				break;
		}

		if (inThread.IsStopRequested())
			break;
	}
	
	// Scan the drives that were not intialized from the cache.
	InitialScan(inThread, buffer_usn);
	
	mInitState.Store(InitState::PreparingCommands);

	// Create the commands for all the files.
	for (auto& repo : mRepos)
		for (auto& file : repo.mFiles)
			gCookingSystem.CreateCommandsForFile(file);

	// Check which commmands need to cook.
	gCookingSystem.UpdateAllDirtyStates();

	mInitStats.mReadyTicks = gGetTickCount();
	mInitState.Store(InitState::Ready);

	// Once the scan is finished, start cooking.
	gCookingSystem.StartCooking();

	while (!inThread.IsStopRequested())
	{
		bool any_work_done = false;

		// Check the queue of files to re-scan (we try again if it eg. fails because the file was in use).
		while (true)
		{
			int64  current_time = gGetTickCount();
			FileID file_to_rescan;
			{
				// Check if there's an item in the queue, and if it's time to scan it again.
				LockGuard lock(mFilesToRescanMutex);
				if (!mFilesToRescan.IsEmpty() && mFilesToRescan.Front().mWaitUntilTicks <= current_time)
				{
					file_to_rescan = mFilesToRescan.Front().mFileID;
					mFilesToRescan.PopFront();
				}
				else
				{
					// Nothing more to re-scan.
					break;
				}
			}

			// Scan it.
			FileRepo& repo = file_to_rescan.GetRepo();
			if (file_to_rescan.GetFile().IsDirectory())
			{
				FileID dir_id = file_to_rescan;
				do
				{
					repo.ScanDirectory(dir_id, scan_queue, buffer_scan);
				} while ((dir_id = scan_queue.Pop()) != FileID::cInvalid());
			}
			else
			{
				repo.ScanFile(file_to_rescan.GetFile(), FileRepo::RequestedAttributes::All);
			}

			any_work_done = true;
		}

		// Check the USN journal of every drive to see if files changed.
		for (auto& drive : mDrives)
		{
			// Process the queue.
			while (drive.ProcessMonitorDirectory(buffer_usn, scan_queue, buffer_scan))
			{
				any_work_done = true;

				if (inThread.IsStopRequested())
					break;
			}

			if (inThread.IsStopRequested())
				break;
		}

		// Note: we don't update any_work_done here because we don't want to cause a busy loop waiting to update commands that are still cooking.
		// Instead the cooking threads will wake this thread up any time a command finishes (which usually also means there are file changes to process).
		gCookingSystem.ProcessUpdateDirtyStates();

		// Launch notifications if there are errors or cooking is finished.
		gCookingSystem.UpdateNotifications();

		if (!any_work_done															// If there was any work done, do another loop before declaring the thread idle.
			&& mMonitorDirThreadSignal.WaitFor(0) == SyncSignal::WaitResult::Timeout)	// Check if the signal is already set without waiting.
		{
			// Going idle here.
			mIsMonitorDirThreadIdle.Store(true);

			// Wait for some time before checking the USN journals again (unless we're being signaled).
			(void)mMonitorDirThreadSignal.WaitFor(gSecondsToTicks(1.0));

			// Not idle anymore.
			mIsMonitorDirThreadIdle.Store(false);
		}
	}

	// Only save the state if we've finished scanning when we exit (don't save an incomplete state).
	if (GetInitState() == InitState::Ready)
		SaveCache();
}


void FileSystem::RescanLater(FileID inFileID)
{
	constexpr double cFileRescanDelayMS = 300.0; // In milliseconds.

	LockGuard lock(mFilesToRescanMutex);
	mFilesToRescan.PushBack({ inFileID, gGetTickCount() + gMillisecondsToTicks(cFileRescanDelayMS) });
}



struct SerializedFileInfo
{
	uint32        mPathOffset       = 0;
	uint32        mPathSize    : 31 = 0;
	uint32        mIsDirectory : 1  = 0;
	FileRefNumber mRefNumber        = {};
	FileTime      mCreationTime     = {};
	USN           mLastChangeUSN    = 0;
	FileTime      mLastChangeTime   = {};

	FileType GetType() const { return mIsDirectory ? FileType::Directory : FileType::File; }
};
static_assert(sizeof(SerializedFileInfo) == 48);

struct SerializedCommand
{
	PathHash mMainInputPathHash    = {};
	uint64   mLastCookUSN     : 63 = 0;
	uint64   mLastCookIsError : 1  = 0;
	FileTime mLastCookTime         = {};
};
static_assert(sizeof(SerializedCommand) == 32);

struct SerializedDepFileHeader
{
	USN      mLastDepFileRead    = 0;
	uint32   mDepFileInputCount  = 0;
	uint32   mDepFileOutputCount = 0;
};
static_assert(sizeof(SerializedDepFileHeader) == 16);


constexpr int        cCacheFormatVersion = 5;
constexpr StringView cCacheFileName      = "cache.bin";

void FileSystem::LoadCache()
{
	gApp.Log("Loading cached state.");
	Timer timer;
	mInitState.Store(InitState::LoadingCache);

	TempString cache_file_path = gTempFormat(R"(%s\%s)", gApp.mCacheDirectory.AsCStr(), cCacheFileName.AsCStr());
	FILE*      cache_file      = fopen(cache_file_path.AsCStr(), "rb");

	if (cache_file == nullptr)
	{
		gApp.Log(R"(No cached state found ("{}"))", cache_file_path);
		return;
	}

	defer { fclose(cache_file); };

	BinaryReader bin;
	if (!bin.ReadFile(cache_file))
		return;

	if (!bin.ExpectLabel("VERSION"))
	{
		gApp.LogError(R"(Corrupted cached state, ignoring cache. ("{}"))", cache_file_path);
		return;
	}

	int format_version = -1;
	bin.Read(format_version);
	if (format_version != cCacheFormatVersion)
	{
		gApp.Log("Unsupported cached state version, ignoring cache. (Expected: {} Found: {}).", cCacheFormatVersion, format_version);
		return;
	}

	Vector<StringView> all_valid_repos;
	int total_repo_count = 0;

	// Read all drives and repos.
	uint16 drive_count = 0;
	bin.Read(drive_count);
	for (int drive_index = 0; drive_index < (int)drive_count; ++drive_index)
	{
		if (!bin.ExpectLabel("DRIVE"))
			break; // Early out if reading is failing.

		char drive_letter = 0;
		bin.Read(drive_letter);

		uint64 journal_id = 0;
		bin.Read(journal_id);

		USN next_usn = 0;
		bin.Read(next_usn);

		FileDrive* drive       = FindDrive(drive_letter);
		bool       drive_valid = true;
		if (drive == nullptr)
		{
			gApp.LogError(R"(Drive {}:\ is listed in the cache but isn't used anymore, ignoring cache.)", drive_letter);
			drive_valid = false;
		}
		else
		{
			if (drive->mUSNJournalID != journal_id)
			{
				gApp.LogError(R"(Drive {}:\ USN journal ID has changed, ignoring cache.)", drive_letter);
				drive_valid = false;
			}

			if (drive->mFirstUSN > next_usn)
			{
				gApp.LogError(R"(Drive {}:\ cached state is too old, ignoring cache.)", drive_letter);
				if (drive_letter == 'C')
				{
					gApp.LogError(R"(  Consider using a different drive than C:\!)");
					gApp.LogError(R"(  Windows writes a lot of files and can quickly fill the USN journal.)");
				}

				drive_valid = false;
			}
		}

		uint16 repo_count = 0;
		bin.Read(repo_count);
		total_repo_count += repo_count;

		TempVector<StringView> valid_repos;

		for (int repo_index = 0; repo_index < (int)repo_count; ++repo_index)
		{
			if (!bin.ExpectLabel("REPO"))
				break; // Early out if reading is failing.

			FixedString128 repo_name;
			bin.Read(repo_name);

			TempPath repo_path;
			bin.Read(repo_path);

			FileRepo* repo       = FindRepo(repo_name);
			bool      repo_valid = true;
			if (repo == nullptr)
			{
				gApp.LogError(R"(Repo "{}" is listed in the cache but doesn't exist anymore, ignoring cache.)", repo_name);
				repo_valid = false;
			}
			else
			{
				if (!gIsEqualNoCase(repo->mRootPath, repo_path))
				{
					gApp.LogError(R"(Repo "{}" root path changed, ignoring cache.)", repo_name);
					repo_valid = false;
				}
			}

			if (drive_valid && repo_valid)
				valid_repos.PushBack(repo->mName);
		}

		// If all repos for this drive are valid, we can use the cached state.
		if (drive_valid && valid_repos.Size() == drive->mRepos.Size())
		{
			// Set the next USN we should read.
			drive->mNextUSN = next_usn;

			// Remember we're loading this drive from the cache to skip the initial scan.
			drive->mLoadedFromCache = true;

			// Add the repos names to the list of valid repos so that we read their content later.
			all_valid_repos.Insert(all_valid_repos.Size(), valid_repos);
		}
	}

	for (int repo_index = 0; repo_index < total_repo_count; ++repo_index)
	{
		if (!bin.ExpectLabel("REPO_CONTENT"))
			break;

		FixedString128 repo_name;
		bin.Read(repo_name);

		uint32 file_count = 0;
		bin.Read(file_count);

		uint32 string_pool_bytes = 0;
		bin.Read(string_pool_bytes);

		bool repo_valid = gContains(all_valid_repos, repo_name.AsStringView());
		
		// We found it earlier, so this shouldn't fail.
		FileRepo* repo = FindRepo(repo_name);

		if (!bin.ExpectLabel("STRINGS"))
			break;

		Span<char> all_strings;
		if (repo_valid)
		{
			// Read the strings.
			// Note: -1 because StringPool always allocate one more byte for a null terminator, but it's already counted in the size here.
			all_strings = repo->mStringPool.Allocate(string_pool_bytes - 1);
			bin.Read(all_strings);
		}
		else
		{
			// Or skip them.
			bin.Skip(string_pool_bytes);
		}

		if (!bin.ExpectLabel("FILES"))
			break;

		if (repo_valid)
		{
			// Read the files.
			for (int file_index = 0; file_index < (int)file_count; ++file_index)
			{
				SerializedFileInfo serialized_file_info;
				bin.Read(serialized_file_info);

				FileInfo& file_info = repo->GetOrAddFile(
					MutStringView(all_strings.SubSpan(serialized_file_info.mPathOffset, serialized_file_info.mPathSize)), 
					serialized_file_info.GetType(), 
					serialized_file_info.mRefNumber);

				file_info.mCreationTime   = serialized_file_info.mCreationTime;
				file_info.mLastChangeUSN  = serialized_file_info.mLastChangeUSN;
				file_info.mLastChangeTime = serialized_file_info.mLastChangeTime;
			}
		}
		else
		{
			// Or skip them.
			bin.Skip(file_count * sizeof(SerializedFileInfo));
		}
	}

	struct ErroredCommand
	{
		CookingCommandID mCommandID;
		StringView       mLastCookOutput;
	};
	TempVector<ErroredCommand> errored_commands;

	// Read the commands.
	size_t total_commands = 0;
	uint16 rule_count     = 0;
	bin.Read(rule_count);
	for (int rule_index = 0; rule_index < (int)rule_count; ++rule_index)
	{
		if (!bin.ExpectLabel("RULE"))
			break; // Early out if reading is failing.

		FixedString128 rule_name;
		bin.Read(rule_name);

		// This info is also in the CookingRule but we serialize it to be able to
		// properly skip the serialized data if the rule was changed.
		bool rule_use_dep_file;
		bin.Read(rule_use_dep_file);

		uint16 rule_version = 0;
		bin.Read(rule_version);

		uint32 command_count = 0;
		bin.Read(command_count);

		const CookingRule* rule       = gCookingSystem.FindRule(rule_name);
		const bool         rule_valid = (rule != nullptr);

		if (rule_valid)
			total_commands += command_count;

		for (int command_index = 0; command_index < (int)command_count; ++command_index)
		{
			if (!bin.ExpectLabel("CMD"))
				break; // Early out if reading is failing.

			SerializedCommand serialized_command;
			bin.Read(serialized_command);

			FileID          main_input = FindFileIDByPathHash(serialized_command.mMainInputPathHash);
			CookingCommand* command    = nullptr;
			if (rule_valid && main_input.IsValid())
			{
				// Make sure the commands are created for this file.
				gCookingSystem.CreateCommandsForFile(main_input.GetFile());

				// Find the command. Should be found, unless the rule changed.
				command = gCookingSystem.FindCommandByMainInput(rule->mID, main_input);

				if (command)
				{
					command->mLastCookUSN         = (USN)serialized_command.mLastCookUSN;
					command->mLastCookTime        = serialized_command.mLastCookTime;
					command->mLastCookRuleVersion = rule_version;
				}
			}

			// If that command had an error, store it because we still want to display the error.
			if (serialized_command.mLastCookIsError)
			{
				// If the rule/command are valid, also read the last cooking log output, otherwise skip it.
				if (command)
					errored_commands.PushBack({ command->mID, bin.Read(gCookingSystem.GetStringPool()) });
				else
					bin.SkipString();
			}

			if (rule_use_dep_file)
			{
				SerializedDepFileHeader serialized_dep_file;
				bin.Read(serialized_dep_file);

				Vector<FileID> inputs, outputs;
				inputs.Reserve(serialized_dep_file.mDepFileInputCount);
				inputs.Reserve(serialized_dep_file.mDepFileOutputCount);

				for (int input_index = 0; input_index < (int)serialized_dep_file.mDepFileInputCount; ++input_index)
				{
					PathHash path_hash;
					bin.Read(path_hash);

					FileID input_file = FindFileIDByPathHash(path_hash);
					if (input_file.IsValid())
						inputs.PushBack(input_file);
				}

				for (int output_index = 0; output_index < (int)serialized_dep_file.mDepFileOutputCount; ++output_index)
				{
					PathHash path_hash;
					bin.Read(path_hash);

					FileID output_file = FindFileIDByPathHash(path_hash);
					if (output_file.IsValid())
						outputs.PushBack(output_file);
				}

				if (rule_valid && rule->UseDepFile() && command != nullptr)
				{
					command->mLastDepFileRead = serialized_dep_file.mLastDepFileRead;
					gApplyDepFileContent(*command, inputs, outputs);
				}
			}
		}
	}

	// Now we need to add a cooking log entry for the errored commands.
	{
		// Sort the commands in error by cooking time, so that the log entries are in a sensible order.
		std::sort(errored_commands.begin(), errored_commands.end(), [](const ErroredCommand& inA, const ErroredCommand& inB) {
			return gCookingSystem.GetCommand(inA.mCommandID).mLastCookTime.mDateTime < gCookingSystem.GetCommand(inB.mCommandID).mLastCookTime.mDateTime;
		});

		// Add a cooking log for each.
		for (auto [command_id, output_log] : errored_commands)
		{
			CookingCommand&  command   = gCookingSystem.GetCommand(command_id);

			CookingLogEntry& log_entry = gCookingSystem.AllocateCookingLogEntry(command_id);
			log_entry.mTimeStart       = command.mLastCookTime;
			log_entry.mOutput          = output_log;
			log_entry.mCookingState.Store(CookingState::Error);

			command.mLastCookingLog    = &log_entry;
		}
	}
	

	bin.ExpectLabel("FIN");

	if (bin.mError)
		gApp.FatalError(R"(Corrupted cached state. Delete the file and try again ("{}")).)", cache_file_path);

	size_t total_files = 0;
	for (auto& repo : mRepos)
		if (repo.mDrive.mLoadedFromCache)
			total_files += repo.mFiles.SizeRelaxed();

	gApp.Log("Done. Found {} Files and {} Commands in {:.2f} seconds.", 
		total_files, total_commands, gTicksToSeconds(timer.GetTicks()));
}


void FileSystem::SaveCache()
{
	gApp.Log("Saving cached state.");
	Timer timer;

	// Make sure the cache dir exists.
	CreateDirectoryA(gApp.mCacheDirectory.AsCStr(), nullptr);

	TempString cache_file_path = gTempFormat(R"(%s\%s)", gApp.mCacheDirectory.AsCStr(), cCacheFileName.AsCStr());
	FILE*      cache_file      = fopen(cache_file_path.AsCStr(), "wb");

	if (cache_file == nullptr)
		gApp.FatalError(R"(Failed to save cached state ("{}") - {} (0x{:X}))", cache_file_path, strerror(errno), errno);

	BinaryWriter bin;

	bin.WriteLabel("VERSION");
	bin.Write(cCacheFormatVersion);

	// Write all drives and repos.
	bin.Write((uint16)mDrives.Size());
	for (const FileDrive& drive : mDrives)
	{
		bin.WriteLabel("DRIVE");

		bin.Write(drive.mLetter);
		bin.Write(drive.mUSNJournalID);
		bin.Write(drive.mNextUSN);

		bin.Write((uint16)drive.mRepos.Size());
		for (const FileRepo* repo : drive.mRepos)
		{
			bin.WriteLabel("REPO");
			bin.Write(repo->mName);
			bin.Write(repo->mRootPath);
		}
	}

	for (const FileRepo& repo : mRepos)
	{
		bin.WriteLabel("REPO_CONTENT");

		bin.Write(repo.mName);

		// Get the number of files and the total size of the file paths.
		uint32 file_count        = 0;
		uint32 string_pool_bytes = 0;
		for (const FileInfo& file : repo.mFiles)
		{
			// Skip deleted files.
			if (file.IsDeleted())
				continue;

			file_count++;
			string_pool_bytes += file.mPath.Size() + 1; // + 1 for null terminator.
		}

		bin.Write(file_count);
		bin.Write(string_pool_bytes);

		bin.WriteLabel("STRINGS");

		// Write the paths.
		for (const FileInfo& file : repo.mFiles)
		{
			// Skip deleted files.
			if (file.IsDeleted())
				continue;

			bin.Write(Span(file.mPath.Data(), file.mPath.Size() + 1)); // + 1 to include null terminator.
		}

		bin.WriteLabel("FILES");

		// Write the files.
		uint32 current_offset = 0;
		for (const FileInfo& file : repo.mFiles)
		{
			// Skip deleted files.
			if (file.IsDeleted())
				continue;

			SerializedFileInfo serialized_file_info;
			serialized_file_info.mPathOffset     = current_offset;
			serialized_file_info.mPathSize       = file.mPath.Size();
			serialized_file_info.mIsDirectory    = file.IsDirectory();
			serialized_file_info.mRefNumber      = file.mRefNumber;
			serialized_file_info.mCreationTime   = file.mCreationTime;
			serialized_file_info.mLastChangeUSN  = file.mLastChangeUSN;
			serialized_file_info.mLastChangeTime = file.mLastChangeTime;

			bin.Write(serialized_file_info);

			current_offset += file.mPath.Size() + 1;
		}
	}

	Span rules = gCookingSystem.GetRules();

	// Build the list of commands for each rule.
	Vector<SegmentedVector<CookingCommandID>> commands_per_rule;
	commands_per_rule.Resize(rules.Size());
	for (const CookingCommand& command : gCookingSystem.GetCommands())
	{
		// Skip cleaned up commands. Their inputs don't exist anymore, we don't need to save anything.
		if (command.IsCleanedUp())
			continue;

		// Skip commands that didn't cook since the rule version changed.
		// They are dirty and not saving them will make them appear dirty when we restart.
		if (command.mLastCookRuleVersion != command.GetRule().mVersion)
			continue;

		commands_per_rule[command.mRuleID.mIndex].emplace_back(command.mID);
	}

	// Write the commands, sorted by rule.
	bin.Write((uint16)rules.Size());
	for (const CookingRule& rule : rules)
	{
		bin.WriteLabel("RULE");

		bin.Write(rule.mName);
		bin.Write(rule.UseDepFile());
		bin.Write(rule.mVersion);

		const SegmentedVector<CookingCommandID>& commands = commands_per_rule[rule.mID.mIndex];
		bin.Write((uint32)commands.size());

		for (CookingCommandID command_id : commands)
		{
			bin.WriteLabel("CMD");

			const CookingCommand& command = gCookingSystem.GetCommand(command_id);

			// Write the base command data.
			SerializedCommand     serialized_command;
			const FileInfo&       main_input      = command.GetMainInput().GetFile();
			serialized_command.mMainInputPathHash = gHashPath(TempPath("{}{}", main_input.GetRepo().mRootPath, main_input.mPath));
			serialized_command.mLastCookUSN       = command.mLastCookUSN;
			serialized_command.mLastCookIsError   = (command.mDirtyState & CookingCommand::Error) != 0;
			serialized_command.mLastCookTime      = command.mLastCookTime;
			bin.Write(serialized_command);

			// If the command had an error, also write the last cooking log output.
			if (serialized_command.mLastCookIsError)
			{
				if (command.mLastCookingLog)
					bin.Write(command.mLastCookingLog->mOutput);
				else
					bin.Write("No output recorded."); // Can this case happen? Probably not, but better be safe.
			}

			// Write the dep file data, if needed.
			if (rule.UseDepFile())
			{
				SerializedDepFileHeader serialized_dep_file;
				serialized_dep_file.mLastDepFileRead    = command.mLastDepFileRead;
				serialized_dep_file.mDepFileInputCount  = (uint32)command.mDepFileInputs.Size();
				serialized_dep_file.mDepFileOutputCount = (uint32)command.mDepFileOutputs.Size();
				bin.Write(serialized_dep_file);

				for (FileID file_id : command.mDepFileInputs)
				{
					const FileInfo& file = file_id.GetFile();
					PathHash path_hash = gHashPath(TempPath("{}{}", file.GetRepo().mRootPath, file.mPath));
					bin.Write(path_hash);
				}

				for (FileID file_id : command.mDepFileOutputs)
				{
					const FileInfo& file = file_id.GetFile();
					PathHash path_hash = gHashPath(TempPath("{}{}", file.GetRepo().mRootPath, file.mPath));
					bin.Write(path_hash);
				}
			}
		}
	}

	bin.WriteLabel("FIN");

	if (!bin.WriteFile(cache_file))
		gApp.FatalError(R"(Failed to save cached state ("{}") - {} (0x{:X}))", cache_file_path, strerror(errno), errno);

	size_t file_size = ftell(cache_file);
	fclose(cache_file);

	gApp.Log("Done. Saved {} ({} compressed) in {:.2f} seconds.", SizeInBytes(bin.mBuffer.SizeRelaxed()), SizeInBytes(file_size), gTicksToSeconds(timer.GetTicks()));
}