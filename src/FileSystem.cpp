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
#include <Bedrock/Test.h>

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
	PathBufferUTF16 wpath_buffer;
	WStringView     wpath = gUtf8ToWideChar(inAbsolutePath, wpath_buffer);
	if (wpath.empty())
		gAppFatalError("Failed to convert path %s to WideChar", inAbsolutePath.AsCStr());

	// TODO use gToLowerCase instead?
	// Convert it to uppercase.
	// Note: LCMapStringA does not seem to work with UTF8 (or at least not with LOCALE_INVARIANT) so we are forced to use wchars here.
	PathBufferUTF16 uppercase_buffer;
	int uppercase_size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wpath.data(), (int)wpath.size(), uppercase_buffer, gElemCount(uppercase_buffer) / 2, nullptr, nullptr, 0);
	if (uppercase_size == 0)
		gAppFatalError("Failed to convert path %s to uppercase", inAbsolutePath.AsCStr());

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

TempString FileRefNumber::ToString() const
{
	return gTempFormat("0x%llX%016llX", mData[1], mData[0]);
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


TempString FileInfo::ToString() const
{
	return gConcat(GetRepo().mName, ":", mPath);
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
		gAppFatalError("Failed to get handle to %s - %s", mRootPath.AsCStr(), GetLastErrorString().AsCStr());

	// Get the FileReferenceNumber of the root dir.
	FILE_ID_INFO file_info;
	if (!GetFileInformationByHandleEx(root_dir_handle, FileIdInfo, &file_info, sizeof(file_info)))
		gAppFatalError("Failed to get FileReferenceNumber for %s - %s", mRootPath.AsCStr(), GetLastErrorString().AsCStr());

	// The root directory file info has an empty path (relative to mRootPath).
	FileInfo& root_dir = GetOrAddFile("", FileType::Directory, file_info.FileId);
	mRootDirID = root_dir.mID;

	gAppLog("Initialized FileRepo %s as %s:", mRootPath.AsCStr(), mName.AsCStr());
}



FileInfo& FileRepo::GetOrAddFile(StringView inPath, FileType inType, FileRefNumber inRefNumber)
{
	// Make sure the path is normalized.
	TempString path = inPath;
	gNormalizePath(path);

	// Calculate the case insensitive path hash that will be used to identify the file.
	PathHash  path_hash = gHashPath(gConcat(mRootPath, path));

	FileInfo*     file                 = nullptr;
	FileRefNumber ref_number_to_remove = {};

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

			auto [_, value, result] = gFileSystem.mFilesByPathHash.Insert(path_hash, new_file_id);
			if (result == EInsertResult::Found)
			{
				actual_file_id = value;

				if (inRefNumber.IsValid())
				{
					FileInfo& file = GetFile(actual_file_id);

					// If the file is already known, make sure we update the ref number.
					// Three cases to consider here:
					// - The ref number is the same: nothing to do (the function is GetOrAdd after all).
					// - The file had an invalid ref number: it just means it was deleted and now it exists again.
					// - The file had a different (but valid) ref number. This can happen when a file is created and immediately
					// renamed to an existing file. Some apps do this when saving a file (eg. Visual Studio): they create a temp file,
					// delete the target file, then rename the temp file to the target name. When we see the event for the temp file being
					// created, it already has the target name, and the event for the deletion of the target file is after so we haven't seen it yet.
					// It's a weird case, but it's actually not a problem: just make sure the ref number of the target file is immediately removed
					// from the ref number hash map to avoid having two ref numbers pointing to the same FileInfo.
					if (file.mRefNumber != inRefNumber)
					{
						// Case 3: if the file had a valid ref number, remember it to remove it from the hash map below.
						if (file.mRefNumber.IsValid())
							ref_number_to_remove = file.mRefNumber;

						// Update the ref number.
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
		if (inRefNumber.IsValid() || ref_number_to_remove.IsValid())
		{
			// TODO: not great to access these internals, maybe find a better way?
			LockGuard map_lock(mDrive.mFilesByRefNumberMutex);

			// If there's a ref number to remove from the map, do it now that we have the lock.
			if (ref_number_to_remove.IsValid())
			{
				bool erased = mDrive.mFilesByRefNumber.Erase(ref_number_to_remove);
				gAssert(erased);
			}

			auto [_, value, result] = mDrive.mFilesByRefNumber.Insert(inRefNumber, actual_file_id);
			if (result == EInsertResult::Found)
			{
				FileID previous_file_id = value;

				// Check if the existing file is the same (ie. same path).
				// The file could have been renamed but kept the same ref number (and we've missed that rename event?),
				// or it could be a junction/hardlink to the same file (TODO: detect that, at least to error properly?)
				if (previous_file_id != actual_file_id || previous_file_id.GetFile().mPathHash != path_hash)
				{
					gAppLogError(R"(Found two files with the same RefNumber! %c:\%s and %s%s)", 
						mDrive.mLetter, path.AsCStr(),
						previous_file_id.GetRepo().mRootPath.AsCStr(), previous_file_id.GetFile().mPath.AsCStr());

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
				gAppFatalError("%s was a %s but is now a %s. This is not supported yet.",
					file->ToString().AsCStr(),
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

	mDrive.mFilesByRefNumber.Erase(ioFile.mRefNumber);
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



static TempString sBuildFilePath(StringView inParentDirPath, WStringView inFileNameW)
{
	TempString file_name = gWideCharToUtf8(inFileNameW);
	if (file_name.Empty())
	{
		// Failed for some reason.
		gAssert(false); // Investigate.

		return {};
	}

	TempString path;

	// Add the parent dir if there's one (can be empty for the root dir).
	if (!inParentDirPath.Empty())
	{
		path += inParentDirPath;
		path += "\\";
	}

	// Add the file name.
	path += file_name;

	return path;
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
		gAppLog("Added %s", dir.ToString().AsCStr());

	// First GetFileInformationByHandleEx call needs a different value to make it "restart".
	FILE_INFO_BY_HANDLE_CLASS file_info_class = FileIdExtdDirectoryRestartInfo;

	while (true)
	{
		// Fill the scan buffer with content to iterate.
		if (!GetFileInformationByHandleEx(*dir_handle, file_info_class, ioBuffer.Data(), ioBuffer.Size()))
		{
			if (GetLastError() == ERROR_NO_MORE_FILES)
				break; // Finished iterating, exit the loop.

			gAppFatalError("Enumerating %s failed - %s", dir.ToString().AsCStr(), GetLastErrorString().AsCStr());
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
			TempString path = sBuildFilePath(dir.mPath, wfilename);

			// If it fails, ignore the file.
			if (path.Empty())
			{
				gAppLogError("Failed to build the path of a file in %s", dir.ToString().AsCStr());
				gAssert(false); // Investigate why that would happen.
				continue;
			}

			// Check if it's a directory.
			const bool is_directory = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			// Add (or get) the file info.
			FileInfo& file = GetOrAddFile(path, is_directory ? FileType::Directory : FileType::File, entry->FileId);

			if (gApp.mLogFSActivity >= LogLevel::Verbose)
				gAppLog("Added %s", file.ToString().AsCStr());

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
			gAppLog("Used %s of %s buffer.", 
				gFormatSizeInBytes(buffer_end - ioBuffer.Data()).AsCStr(), 
				gFormatSizeInBytes(ioBuffer.Size()).AsCStr());
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
			gAppLogError("Getting attributes for %s failed - %s", ioFile.ToString().AsCStr(), GetLastErrorString().AsCStr());

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
		gAppFatalError(R"(Failed to get handle to %c:\ - %s)", mLetter, GetLastErrorString().AsCStr());

	// Query the USN journal to get its ID.
	USN_JOURNAL_DATA_V0 journal_data;
	uint32				unused;
	if (!DeviceIoControl(mHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &unused, nullptr))
		gAppFatalError(R"(Failed to query USN journal for %c:\ - %s)", mLetter, GetLastErrorString().AsCStr());

	// Store the jorunal ID.
	mUSNJournalID = journal_data.UsnJournalID;

	// Store the first USN. This will be used to know if the cached state is usable.
	mFirstUSN = journal_data.FirstUsn;
	// Store the next USN. This will be overwritten if the cached state is usable.
	mNextUSN  = journal_data.NextUsn;

	gAppLog(R"(Queried USN journal for %c:\. ID: 0x%08llX. Max size: %s)", 
		mLetter, mUSNJournalID, gFormatSizeInBytes(journal_data.MaximumSize).AsCStr());
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
				return file_id.GetFile().ToString();
			else
				return "Unknown";
		};

		uint32 error = GetLastError();

		// In non-verbose mode, don't log errors unless we know they're about a file we care about.
		if (gApp.mLogFSActivity >= LogLevel::Verbose || inFileID.IsValid())
			gAppLogError("Failed to open %s (%s) - %s", 
				BuildFileStr(inRefNumber, inFileID).AsCStr(), inRefNumber.ToString().AsCStr(), GetLastErrorString().AsCStr());
		
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

		gAppFatalError("Failed to open %s (%s) - %s", 
			BuildFileStr(inRefNumber, inFileID).AsCStr(), inRefNumber.ToString().AsCStr(), GetLastErrorString().AsCStr());
	}

	return handle;
}


bool FileDrive::GetFullPath(const OwnedHandle& inFileHandle, TempString& outFullPath) const
{
	// Get the full path as utf16.
	// PFILE_NAME_INFO contains the filename without the drive letter and column in front (ie. without the C:).
	PathBufferUTF16 wpath_buffer;
	PFILE_NAME_INFO file_name_info = (PFILE_NAME_INFO)wpath_buffer;
	if (!GetFileInformationByHandleEx(inFileHandle, FileNameInfo, file_name_info, gElemCount(wpath_buffer) * sizeof(wpath_buffer[0])))
		return false;

	WStringView wpath = { file_name_info->FileName, file_name_info->FileNameLength / 2 };

	// Get the path part.
	TempString path_part = gWideCharToUtf8(wpath);
	if (path_part.Empty())
		return false;

	// Write the drive part.
	outFullPath.Resize(2);
	outFullPath[0] = mLetter;
	outFullPath[1] = ':';

	// Add the path part.
	outFullPath += path_part;

	return true;
}


USN FileDrive::GetUSN(const OwnedHandle& inFileHandle) const
{
	PathBufferUTF16 buffer;
	DWORD available_bytes = 0;
	if (!DeviceIoControl(inFileHandle, FSCTL_READ_FILE_USN_DATA, nullptr, 0, buffer, gElemCount(buffer) * sizeof(buffer[0]), &available_bytes, nullptr))
		gAppFatalError("Failed to get USN data"); // TODO add file path to message

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
		gAppFatalError("Got unexpected USN record version (%u.%u)", record_header->MajorVersion, record_header->MinorVersion);
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
			gAppFatalError("Failed to read USN journal for %c:\\ - Trying to read USN %llx.\nError: %s", 
				mLetter,
				start_usn,
				GetLastErrorString().AsCStr());
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
				FileTime  timestamp    = FileTime(inRecord.TimeStamp.QuadPart);

				FileRepo& repo = gFileSystem.GetRepo(deleted_file.mID);

				repo.MarkFileDeleted(deleted_file, timestamp);

				if (gApp.mLogFSActivity >= LogLevel::Verbose)
					gAppLog("Deleted %s", deleted_file.ToString().AsCStr());

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
								gAppLog("Deleted %s", file.ToString().AsCStr());
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
			TempString full_path;
			if (!GetFullPath(*file_handle, full_path))
			{
				// TODO: same remark as failing to open
				gAppLogError("Failed to get path for newly created file %s - %s", 
					FileRefNumber(inRecord.FileReferenceNumber).ToString().AsCStr(), 
					GetLastErrorString().AsCStr());
				return;
			}

			// Check if it's in a repo, otherwise ignore.
			FileRepo* repo = FindRepoForPath(full_path);
			if (repo)
			{
				// Get the file path relative to the repo root.
				StringView file_path = repo->RemoveRootPath(full_path);

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
						gAppLog("Added %s", file.ToString().AsCStr());

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
					gAppLog("Modified %s", file.ToString().AsCStr());

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

	auto it = mFilesByRefNumber.Find(inRefNumber);
	if (it != mFilesByRefNumber.End())
		return it->mValue;

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

	auto it = mFilesByPathHash.Find(inPathHash);
	if (it != mFilesByPathHash.End())
		return it->mValue;

	return {};
}


FileRepo& FileSystem::AddRepo(StringView inName, StringView inRootPath)
{
	gAssert(!IsMonitoringStarted()); // Can't add repos once the threads have started, it's not thread safe!
	gAssert(gIsNullTerminated(inRootPath));

	// Check that the name is unique.
	for (auto& repo : mRepos)
	{
		if (repo.mName == inName)
			gAppFatalError("Failed to init FileRepo %s (%s) - There is already a repo with that name.", 
				inName.AsCStr(), inRootPath.AsCStr());
	}

	// Get the absolute path (in case it's relative).
	TempString root_path = gGetAbsolutePath(inRootPath);

	gAssert(gIsNormalized(root_path));

	// Add a trailing slash if there isn't one.
	if (!gEndsWith(root_path, "\\"))
		root_path.Append("\\");

	// Check if it overlaps with other repos.
	for (auto& repo : mRepos)
	{
		if (gStartsWith(repo.mRootPath, root_path))
		{
			gAppFatalError("Failed to init FileRepo %s (%s) - Another FileRepo is inside its root path (%s %s).", 
				inName.AsCStr(), inRootPath.AsCStr(), 
				repo.mName.AsCStr(), repo.mRootPath.AsCStr());
		}

		if (gStartsWith(root_path, repo.mRootPath))
		{
			gAppFatalError("Failed to init FileRepo %s (%s) - Root Path is inside another FileRepo (%s %s).", 
				inName.AsCStr(), inRootPath.AsCStr(), 
				repo.mName.AsCStr(), repo.mRootPath.AsCStr());
		}
	}

	return mRepos.Emplace({}, (uint32)mRepos.Size(), inName, root_path, GetOrAddDrive(root_path[0]));
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
		gAppLogError("Failed to create directory for %s", file.ToString().AsCStr());

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
		gAppLogError("Failed to delete %s - %s", abs_path.AsCStr(), GetLastErrorString().AsCStr());

	return success;
}


int FileSystem::GetFileCount() const
{
	int file_count = 0;
	for (const FileRepo& repo : mRepos)
		file_count += repo.mFiles.Size();
	return file_count;
}



void FileSystem::InitialScan(const Thread& inInitialScanThread, Span<uint8> ioBufferUSN)
{
	// Early out if we have everything from the cache already.
	if (gAllOf(mRepos, [](const FileRepo& inRepo) { return inRepo.mLoadedFromCache; }))
		return;

	gAppLog("Starting initial scan.");
	Timer timer;
	mInitState.Store(InitState::Scanning);

	// Don't use too many threads otherwise they'll just spend their time on the hashmap mutex.
	// TODO this could be improved
	constexpr int cMaxScanThreadCount = 4;
	const int scan_thread_count = gMin(gThreadHardwareConcurrency(), cMaxScanThreadCount);

	// Prepare a scan queue that can be used by multiple threads.
	ScanQueue scan_queue;
	scan_queue.mDirectories.Reserve(1024);
	scan_queue.mThreadsBusy = scan_thread_count; // All threads start busy.

	// Put the root dir of each repo in the queue.
	for (FileRepo& repo : mRepos)
	{
		// Skip repos that were already loaded from the cache.
		if (repo.mLoadedFromCache)
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

					// If exiting, skip scanning but keep emptying the queue to make sure others threads aren't stuck waiting inside Pop().
					if (inInitialScanThread.IsStopRequested())
						continue;

					repo.ScanDirectory(dir_id, scan_queue, buffer_scan);
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

	int total_files = 0;
	for (auto& repo : mRepos)
		if (!repo.mLoadedFromCache)
			total_files += repo.mFiles.SizeRelaxed();

	gAppLog("Done. Found %d files in %.2f seconds.", 
		total_files, gTicksToSeconds(timer.GetTicks()));

	mInitState.Store(InitState::ReadingUSNJournal);

	// Read the entire USN journal to get the last USN for as many files as possible.
	// This is faster than requesting USN for individual files even though we have to browse a lot of record.
	for (FileDrive& drive : mDrives)
	{
		// Skip drives that were already loaded from the cache.
		if (gAllOf(drive.mRepos, [](const FileRepo* inRepo) { return inRepo->mLoadedFromCache; }))
			continue;

		timer.Reset();
		gAppLog("Reading USN journal for %c:\\.", drive.mLetter);

		int file_count = 0;

		// Read the entire USN journal.
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

		gAppLog("Done. Found USN for %d files in %.2f seconds.", file_count, gTicksToSeconds(timer.GetTicks()));
	}

	// Files that haven't been touched in a while might not be referenced in the USN journal anymore.
	// For these, we'll need to fetch the last USN manually.
	TempVector<FileID> files_without_usn;
	for (auto& repo : mRepos)
	{
		// Skip repos that were loaded from the cache.
		if (repo.mLoadedFromCache)
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
		gAppLog("%d files were not present in the USN journal. Fetching their USN manually now.", files_without_usn.Size());

		// Don't create too many threads because they'll get stuck in locks in OpenFileByRefNumber if the cache is warm,
		// or they'll be bottlenecked by IO otherwise.
		constexpr int cMaxUSNThreadCount = 4;
		const int     usn_thread_count   = gMin(gThreadHardwareConcurrency(), cMaxUSNThreadCount);

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

		gAppLog("Done. Fetched %d individual USNs in %.2f seconds.", 
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

	// For drives that have repos initialized from the cache, read all the changes since the cache was saved.
	// This needs to be done before processing dirty states/starting cooking, as we don't know the final state of the files yet.
	// Repos not loaded from the cache should be empty by this point, so the changes will be ignored/overriden by the inital scan.
	for (auto& drive : mDrives)
	{
		if (gNoneOf(drive.mRepos, [](const FileRepo* inRepo) { return inRepo->mLoadedFromCache; }))
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

		// If running without UI, we want to exit when cooking is finished.
		if (gApp.mNoUI)
		{
			// Note: Cooking should probably never be paused when running without UI, but if it is don't exit (it's probably for debugging?)
			if (gCookingSystem.IsIdle() && !gCookingSystem.IsCookingPaused())
				gApp.RequestExit();
		}

		if (!any_work_done															  // If there was any work done, do another loop before declaring the thread idle.
			&& mMonitorDirThreadSignal.WaitFor(0) == SyncSignal::WaitResult::Timeout) // Check if the signal is already set without waiting.
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
	gAppLog("Loading cached state.");
	Timer timer;
	mInitState.Store(InitState::LoadingCache);

	TempString cache_file_path = gTempFormat(R"(%s\%s)", gApp.mCacheDirectory.AsCStr(), cCacheFileName.AsCStr());
	FILE*      cache_file      = fopen(cache_file_path.AsCStr(), "rb");

	if (cache_file == nullptr)
	{
		gAppLog(R"(No cached state found ("%s"))", cache_file_path.AsCStr());
		return;
	}

	defer { fclose(cache_file); };

	BinaryReader bin;
	if (!bin.ReadFile(cache_file))
		return;

	if (!bin.ExpectLabel("VERSION"))
	{
		gAppLogError(R"(Corrupted cached state, ignoring cache. ("%s"))", cache_file_path.AsCStr());
		return;
	}

	int format_version = -1;
	bin.Read(format_version);
	if (format_version != cCacheFormatVersion)
	{
		gAppLog("Unsupported cached state version, ignoring cache. (Expected: %d Found: %d).", 
			cCacheFormatVersion, format_version);
		return;
	}

	Vector<StringView> valid_repos;
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

		FileDrive* drive         = FindDrive(drive_letter);
		bool       drive_valid   = true;
		bool       rescan_needed = false;
		if (drive == nullptr)
		{
			gAppLogError(R"(Drive %c:\ is listed in the cache but isn't used anymore, ignoring cache.)", drive_letter);
			drive_valid = false;
		}
		else
		{
			if (drive->mUSNJournalID != journal_id)
			{
				gAppLogError(R"(Drive %c:\ USN journal ID has changed, ignoring cache.)", drive_letter);
				drive_valid = false;
			}

			if (drive->mFirstUSN > next_usn)
			{
				gAppLogError(R"(Drive %c:\ cached state is too old, re-scan required.)", drive_letter);
				if (drive_letter == 'C')
				{
					gAppLogError(R"(  Consider using a different drive than C:\!)");
					gAppLogError(R"(  Windows writes a lot of files and can quickly fill the USN journal.)");
				}

				// We can't rely on the USN journal to catch up on what's changed, but reading the cached state 
				// is still useful since it contains the commands last cook USN.
				// We'll need to consider all the files from the cache to be deleted, then run the initial scan to see what actually exists.
				rescan_needed = true;
			}
		}

		if (!rescan_needed)
		{
			// Set the next USN we should read.
			// (If a rescan is needed, don't overwrite mNextUSN. next_usn is too old and we're going to read everything anyway).
			drive->mNextUSN = next_usn;
		}

		uint16 repo_count = 0;
		bin.Read(repo_count);
		total_repo_count += repo_count;

		for (int repo_index = 0; repo_index < (int)repo_count; ++repo_index)
		{
			if (!bin.ExpectLabel("REPO"))
				break; // Early out if reading is failing.

			TempString repo_name;
			bin.Read(repo_name);

			TempString repo_path;
			bin.Read(repo_path);

			FileRepo* repo       = FindRepo(repo_name);
			bool      repo_valid = true;
			if (repo == nullptr)
			{
				gAppLogError(R"(Repo "%s" is listed in the cache but doesn't exist anymore.)", repo_name.AsCStr());
				repo_valid = false;
			}
			else
			{
				if (!gIsEqualNoCase(repo->mRootPath, repo_path))
				{
					gAppLogError(R"(Repo "%s" root path changed, ignoring cache.)", repo_name.AsCStr());
					repo_valid = false;
				}
			}

			// Add the repos names to the list of valid repos so that we read their content later.
			if (drive_valid && repo_valid)
			{
				valid_repos.PushBack(repo->mName);

				// Remember we're loading this repo from the cache to skip the initial scan
				// (unless a rescan is needed, then we explicitly don't want to skip it!)
				if (!rescan_needed)
					repo->mLoadedFromCache = true;
			}
		}
	}

	for (int repo_index = 0; repo_index < total_repo_count; ++repo_index)
	{
		if (!bin.ExpectLabel("REPO_CONTENT"))
			break;

		TempString repo_name;
		bin.Read(repo_name);

		uint32 file_count = 0;
		bin.Read(file_count);

		uint32 string_pool_bytes = 0;
		bin.Read(string_pool_bytes);

		FileRepo* repo = FindRepo(repo_name);

		const bool repo_valid    = gContains(valid_repos, repo_name);
		const bool rescan_needed = repo_valid && !repo->mLoadedFromCache;

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

				// Mark all the files as deleted, the scan will tell if they actually still exist.
				// Note: Don't mark the root dir as deleted otherwise we won't be able to scan it (because it clears the ref number).
				if (rescan_needed && !file_info.mPath.Empty())
					repo->MarkFileDeleted(file_info, {});
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
	Vector<ErroredCommand> errored_commands;

	// Read the commands.
	int	   total_commands = 0;
	uint16 rule_count	  = 0;
	bin.Read(rule_count);
	for (int rule_index = 0; rule_index < (int)rule_count; ++rule_index)
	{
		if (!bin.ExpectLabel("RULE"))
			break; // Early out if reading is failing.

		TempString rule_name;
		bin.Read(rule_name);

		// This info is also in the CookingRule but we serialize it to be able to
		// properly skip the serialized data if the rule was changed.
		bool rule_use_dep_file = false;
		bin.Read(rule_use_dep_file);

		uint16 rule_version = 0;
		bin.Read(rule_version);

		uint32 command_count = 0;
		bin.Read(command_count);

		const CookingRule* rule       = gCookingSystem.FindRule(rule_name);
		const bool         rule_valid = (rule != nullptr);

		if (rule_valid)
			total_commands += (int)command_count;

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
			log_entry.SetOutput(output_log);
			log_entry.mCookingState.Store(CookingState::Error);

			command.mLastCookingLog    = &log_entry;

			// If running without UI, force all errored commands to recook.
			// They should error again and that error will be reported on exit.
			// Is that really better than considering them to be errors without trying again? Not sure.
			if (gApp.mNoUI)
				command.mLastCookRuleVersion = CookingRule::cInvalidVersion;
		}
	}
	

	bin.ExpectLabel("FIN");

	if (bin.mError)
		gAppFatalError(R"(Corrupted cached state. Delete the file and try again ("%s")).)", cache_file_path.AsCStr());

	int total_files = 0;
	for (auto& repo : mRepos)
		if (repo.mLoadedFromCache)
			total_files += repo.mFiles.SizeRelaxed();

	gAppLog("Done. Found %d Files and %d Commands in %.2f seconds.", 
		total_files, total_commands, gTicksToSeconds(timer.GetTicks()));
}


void FileSystem::SaveCache()
{
	gAppLog("Saving cached state.");
	Timer timer;

	// Make sure the cache dir exists.
	CreateDirectoryA(gApp.mCacheDirectory.AsCStr(), nullptr);

	TempString cache_file_path = gTempFormat(R"(%s\%s)", gApp.mCacheDirectory.AsCStr(), cCacheFileName.AsCStr());
	FILE*      cache_file      = fopen(cache_file_path.AsCStr(), "wb");

	if (cache_file == nullptr)
		gAppFatalError(R"(Failed to save cached state ("%s") - %s (0x%X))", cache_file_path.AsCStr(), strerror(errno), errno);

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
	TempVector<Vector<CookingCommandID>> commands_per_rule;
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

		commands_per_rule[command.mRuleID.mIndex].PushBack(command.mID);
	}

	// Write the commands, sorted by rule.
	bin.Write((uint16)rules.Size());
	for (const CookingRule& rule : rules)
	{
		bin.WriteLabel("RULE");

		bin.Write(rule.mName);
		bin.Write(rule.UseDepFile());
		bin.Write(rule.mVersion);

		Span commands = commands_per_rule[rule.mID.mIndex];
		bin.Write((uint32)commands.Size());

		for (CookingCommandID command_id : commands)
		{
			bin.WriteLabel("CMD");

			const CookingCommand& command = gCookingSystem.GetCommand(command_id);

			// Write the base command data.
			SerializedCommand     serialized_command;
			const FileInfo&       main_input      = command.GetMainInput().GetFile();
			serialized_command.mMainInputPathHash = gHashPath(gConcat(main_input.GetRepo().mRootPath, main_input.mPath));
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
					PathHash path_hash = gHashPath(gConcat(file.GetRepo().mRootPath, file.mPath));
					bin.Write(path_hash);
				}

				for (FileID file_id : command.mDepFileOutputs)
				{
					const FileInfo& file = file_id.GetFile();
					PathHash path_hash = gHashPath(gConcat(file.GetRepo().mRootPath, file.mPath));
					bin.Write(path_hash);
				}
			}
		}
	}

	bin.WriteLabel("FIN");

	if (!bin.WriteFile(cache_file))
		gAppFatalError(R"(Failed to save cached state ("%s") - %s (0x%X))", cache_file_path.AsCStr(), strerror(errno), errno);

	size_t file_size = ftell(cache_file);
	fclose(cache_file);

	gAppLog("Done. Saved %s (%s compressed) in %.2f seconds.", 
		gFormatSizeInBytes(bin.mBuffer.Size()).AsCStr(), 
		gFormatSizeInBytes(file_size).AsCStr(), 
		gTicksToSeconds(timer.GetTicks()));
}


REGISTER_TEST("USNToString")
{
	TEST_TRUE(gUSNToString(0) == "0");
	TEST_TRUE(gUSNToString(123) == "123");
	TEST_TRUE(gUSNToString(1'234) == "1'234");
	TEST_TRUE(gUSNToString(12'345) == "12'345");
	TEST_TRUE(gUSNToString(123'456) == "123'456");
	TEST_TRUE(gUSNToString(1'234'567) == "1'234'567");
};