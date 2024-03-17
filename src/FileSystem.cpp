#include "FileSystem.h"
#include "App.h"
#include "Debug.h"
#include "Ticks.h"
#include "CookingSystem.h"
#include "Paths.h"
#include "lz4.h"

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"

#include "xxHash/xxh3.h"

#include <array>

// Debug toggle to fake files failing to open, to test error handling.
bool             gDebugFailOpenFileRandomly = false;

// These are not exactly the max path length allowed by Windows in all cases, but should be good enough.
// TODO: these numbers are actually ridiculously high, lower them (except maybe in scanning code) and crash hard and blame user if they need more
constexpr size_t cMaxPathSizeUTF16 = 32768;
constexpr size_t cMaxPathSizeUTF8  = 32768 * 3ull;	// UTF8 can use up to 6 bytes per character, but let's suppose 3 is good enough on average.

using PathBufferUTF16 = std::array<wchar_t, cMaxPathSizeUTF16>;
using PathBufferUTF8  = std::array<char, cMaxPathSizeUTF8>;


// Hash the absolute path of a file in a case insensitive manner.
// That's used to get a unique identifier for the file even if the file itself doesn't exist.
// The hash is 128 bits, assume no collision.
// Clearly not the most efficient implementation, but good enough for now.
Hash128 gHashPath(StringView inRootPath, StringView inPath)
{
	// Build the full path.
	PathBufferUTF8  abs_path_buffer;
	MutStringView   abs_path = gConcat(abs_path_buffer, inRootPath, inPath);

	// Make sure it's normalized.
	gNormalizePath(abs_path);
	gAssert(gIsAbsolute(abs_path));

	// Convert it to wide char.
	PathBufferUTF16     wpath_buffer;
	OptionalWStringView wpath = gUtf8ToWideChar(abs_path, wpath_buffer);
	if (!wpath)
		gApp.FatalError("Failed to convert path {} to WideChar", inPath);

	// Convert it to uppercase.
	PathBufferUTF16 uppercase_buffer;
	int uppercase_size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wpath->data(), (int)wpath->size(), uppercase_buffer.data(), uppercase_buffer.size() / 2, nullptr, nullptr, 0);
	if (uppercase_size == 0)
		gApp.FatalError("Failed to convert path {} to uppercase", inPath);

	WStringView uppercase_wpath = { uppercase_buffer.data(), (size_t)uppercase_size };

	// Hash the uppercase version.
	XXH128_hash_t hash_xx = XXH3_128bits(uppercase_wpath.data(), uppercase_wpath.size() * sizeof(uppercase_wpath[0]));

	// Convert to our hash wrapper.
	Hash128       path_hash;
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


_FILETIME FileTime::ToWin32() const
{
	static_assert(sizeof(FileTime) == sizeof(_FILETIME));

	_FILETIME file_time;
	memcpy(&file_time, this, sizeof(*this));
	return file_time;
}


FileTime& FileTime::operator=(const _FILETIME& inFileTime)
{
	static_assert(sizeof(FileTime) == sizeof(_FILETIME));

	memcpy(this, &inFileTime, sizeof(FileTime));
	return *this;
}


SystemTime FileTime::ToSystemTime() const
{
	const FILETIME ft = ToWin32();
	SYSTEMTIME     st = {};
	FileTimeToSystemTime(&ft, &st);
	return st;
}


LocalTime FileTime::ToLocalTime() const
{
	return ToSystemTime().ToLocalTime();
}



_SYSTEMTIME SystemTime::ToWin32() const
{
	static_assert(sizeof(SystemTime) == sizeof(_SYSTEMTIME));

	_SYSTEMTIME system_time;
	memcpy(&system_time, this, sizeof(*this));
	return system_time;
}


SystemTime& SystemTime::operator=(const _SYSTEMTIME& inSystemTime)
{
	static_assert(sizeof(SystemTime) == sizeof(_SYSTEMTIME));

	memcpy(this, &inSystemTime, sizeof(*this));
	return *this;
}



FileTime SystemTime::ToFileTime() const
{
	const SYSTEMTIME st = ToWin32();
	FILETIME         ft = {};
	SystemTimeToFileTime(&st, &ft);
	return ft;
}


LocalTime SystemTime::ToLocalTime() const
{
	const SYSTEMTIME st = ToWin32();
	SYSTEMTIME       local_st = {};
	SystemTimeToTzSpecificLocalTime(nullptr, &st, &local_st);
	return local_st;
}


SystemTime gGetSystemTime()
{
	SYSTEMTIME st = {};
	GetSystemTime(&st);
	return st;
}


LocalTime  gGetLocalTime()
{
	return gGetSystemTime().ToLocalTime();
}


FileTime gGetSystemTimeAsFileTime()
{
	FILETIME ft = {};
	GetSystemTimeAsFileTime(&ft);
	return ft;
}


OwnedHandle::~OwnedHandle()
{
	if (mHandle != cInvalid)
		CloseHandle(mHandle);
}


// Find the offset of the character after the last slash, or 0 if there's no slash.
static uint16 sFindNamePos(StringView inPath)
{
	size_t offset = inPath.find_last_of("\\/");
	if (offset != StringView::npos)
		return (uint16)(offset + 1); // +1 because file name starts after the slash.
	else
		return 0; // No subdirs in this path, the start of the name is the start of the string.
}


// Find the offset of the last '.' in the path.
static uint16 sFindExtensionPos(uint16 inNamePos, StringView inPath)
{
	StringView file_name = inPath.substr(inNamePos);

	size_t offset = file_name.find_last_of('.');
	if (offset != StringView::npos)
		return (uint16)offset + inNamePos;
	else
		return (uint16)inPath.size(); // No extension.
}


FileInfo::FileInfo(FileID inID, StringView inPath, Hash128 inPathHash, FileType inType, FileRefNumber inRefNumber)
	: mID(inID)
	, mNamePos(sFindNamePos(inPath))
	, mExtensionPos(sFindExtensionPos(mNamePos, inPath))
	, mPath(inPath)
	, mPathHash(inPathHash)
	, mIsDirectory(inType == FileType::Directory)
	, mCommandsCreated(false)
	, mRefNumber(inRefNumber)
{
	gAssert(gIsNormalized(inPath));
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

	PathBufferUTF16     wpath_buffer;
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
	mDrive.mRepos.push_back(this);

	// Make sure the root path exists.
	gCreateDirectoryRecursive(mRootPath);

	// Convert the root path to wchars.
	PathBufferUTF16     root_path_buffer;
	OptionalWStringView root_path_wchar = gUtf8ToWideChar(mRootPath, root_path_buffer);
	if (!root_path_wchar)
		gApp.FatalError("Failed to convert root path {} to WideChar", mRootPath);

	// Get a handle to the root path.
	OwnedHandle root_dir_handle = CreateFileW(root_path_wchar->data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
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
	// Calculate the case insensitive path hash that will be used to identify the file.
	Hash128   path_hash = gHashPath(mRootPath, inPath);

	FileInfo* file      = nullptr;

	{
		// TODO: not great to access these internals, maybe find a better way?
		std::unique_lock lock(mDrive.mFilesMutex);

		// Prepare a new FileID in case this file wasn't already added.
		FileID           new_file_id = { mIndex, (uint32)mFiles.Size() };
		FileID           actual_file_id;

		// Try to insert it to the path hash map.
		{
			auto [it, inserted] = mDrive.mFilesByPathHash.insert({ path_hash, new_file_id });
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
						mDrive.mLetter, inPath,
						previous_file_id.GetRepo().mRootPath, previous_file_id.GetFile().mPath);

					// Mark the old file as deleted, and add the new one instead.
					MarkFileDeleted(GetFile(previous_file_id), {}, lock);
				}
			}
		}


		if (actual_file_id == new_file_id)
		{
			// The file wasn't already known, add it to the list.
			file = &mFiles.Emplace({}, new_file_id, gNormalizePath(mStringPool.AllocateCopy(inPath)), path_hash, inType, inRefNumber);
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
	// Note: Don't do it during initial scan, it's not necesarry as we'll do it afterwards anyway.
	if (gFileSystem.GetInitState() == FileSystem::InitState::Ready)
		gCookingSystem.CreateCommandsForFile(*file);

	return *file;
}

void FileRepo::MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp)
{
	// TODO: not great to access these internals, maybe find a better way?
	std::unique_lock lock(mDrive.mFilesMutex);

	MarkFileDeleted(ioFile, inTimeStamp, lock);
}

void FileRepo::MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp, const std::unique_lock<std::mutex>& inLock)
{
	gAssert(inLock.mutex() == &mDrive.mFilesMutex);

	mDrive.mFilesByRefNumber.erase(ioFile.mRefNumber);
	ioFile.mRefNumber      = FileRefNumber::cInvalid();
	ioFile.mCreationTime   = inTimeStamp;	// Store the time of deletion in the creation time. 
	ioFile.mLastChangeTime = {};
	ioFile.mLastChangeUSN  = {};

	gCookingSystem.QueueUpdateDirtyStates(ioFile.mID);
}



StringView FileRepo::RemoveRootPath(StringView inFullPath)
{
	gAssert(gStartsWith(inFullPath, mRootPath));

	return inFullPath.substr(mRootPath.size());
}



static OptionalStringView sBuildFilePath(StringView inParentDirPath, WStringView inFileNameW, MutStringView ioBuffer)
{
	MutStringView file_path = ioBuffer;

	// Add the parent dir if there's one (can be empty for the root dir).
	if (!inParentDirPath.empty())
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

	return StringView{ file_path.data(), gEndPtr(*file_name) };
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
		if (!GetFileInformationByHandleEx(*dir_handle, file_info_class, ioBuffer.data(), ioBuffer.size()))
		{
			if (GetLastError() == ERROR_NO_MORE_FILES)
				break; // Finished iterating, exit the loop.

			gApp.FatalError("Enumerating {} failed - {}", dir, GetLastErrorString());
		}

		// Next time keep iterating instead of restarting.
		file_info_class = FileIdExtdDirectoryInfo;

		// Iterate on entries in the buffer.
		PFILE_ID_EXTD_DIR_INFO entry = (PFILE_ID_EXTD_DIR_INFO)ioBuffer.data();
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
			gApp.Log("Used {} of {} buffer.", SizeInBytes(buffer_end - ioBuffer.data()), SizeInBytes(ioBuffer.size()));
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
	mHandle = CreateFileA(TempString32(R"(\\.\{}:)", mLetter).AsCStr(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
		auto BuildFileStr = [this](FileRefNumber inRefNumber, FileID inFileID) {

			// Try to find the FileInfo for that ref number.
			FileID file_id = inFileID.IsValid() ? inFileID : FindFileID(inRefNumber);

			// Turn it into a string.
			if (file_id.IsValid())
				return TempString<cMaxPathSizeUTF8>("{}", file_id.GetFile());
			else
				return TempString<cMaxPathSizeUTF8>("Unknown");
		};

		uint32 error = GetLastError();

		// In non-verbose mode, don't log errors unless we know they're about a file we care about.
		if (gApp.mLogFSActivity >= LogLevel::Verbose || inFileID.IsValid())
			gApp.LogError("Failed to open {} ({}) - {}", BuildFileStr(inRefNumber, inFileID).AsStringView(), inRefNumber, GetLastErrorString());
		
		// Some errors are okay, and we can just ignore the file or try to open it again later.
		// Some are not okay, and we throw a fatal error.
		// The list of okay error is probably incomplete, needs to be amended as we discover them.
		if (error == ERROR_SHARING_VIOLATION)
			return OpenFileError::SharingViolation;
		else if (error == ERROR_ACCESS_DENIED)
			return OpenFileError::AccessDenied;
		else if (error == ERROR_INVALID_PARAMETER	// Yes, invalid parameter means file does not exist (anymore).
			|| error == ERROR_CANT_ACCESS_FILE)		// Unsure what this means but I've seen it happen for an unknown file on C:/ once.
			return OpenFileError::FileNotFound;

		gApp.FatalError("Failed to open {} ({}) - {}", BuildFileStr(inRefNumber, inFileID).AsStringView(), inRefNumber, GetLastErrorString());
	}

	return handle;
}


OptionalStringView FileDrive::GetFullPath(const OwnedHandle& inFileHandle, MutStringView ioBuffer) const
{
	// Get the full path as utf16.
	// PFILE_NAME_INFO contains the filename without the drive letter and column in front (ie. without the C:).
	PathBufferUTF16 wpath_buffer;
	PFILE_NAME_INFO file_name_info = (PFILE_NAME_INFO)wpath_buffer.data();
	if (!GetFileInformationByHandleEx(inFileHandle, FileNameInfo, file_name_info, wpath_buffer.size() * sizeof(wpath_buffer[0])))
		return {};

	WStringView wpath = { file_name_info->FileName, file_name_info->FileNameLength / 2 };

	// Write the drive part.
	ioBuffer[0] = mLetter;
	ioBuffer[1] = ':';

	// Write the path part.
	OptionalStringView path_part = gWideCharToUtf8(wpath, ioBuffer.subspan(2));
	if (!path_part)
		return {};

	return StringView{ ioBuffer.data(), gEndPtr(*path_part) };
}


USN FileDrive::GetUSN(const OwnedHandle& inFileHandle) const
{
	PathBufferUTF16 buffer;
	DWORD available_bytes = 0;
	if (!DeviceIoControl(inFileHandle, FSCTL_READ_FILE_USN_DATA, nullptr, 0, buffer.data(), buffer.size() * sizeof(buffer[0]), &available_bytes, nullptr))
		gApp.FatalError("Failed to get USN data"); // TODO add file path to message

	auto record_header = (USN_RECORD_COMMON_HEADER*)buffer.data();
	if (record_header->MajorVersion == 2)
	{
		auto record = (USN_RECORD_V2*)buffer.data();
		return record->Usn;	
	}
	else if (record_header->MajorVersion == 3)
	{
		auto record = (USN_RECORD_V3*)buffer.data();
		return record->Usn;
	}
	else
	{
		gApp.FatalError("Got unexpected USN record version ({}.{})", record_header->MajorVersion, record_header->MinorVersion);
		return 0;
	}
}

template <typename taFunctionType>
USN FileDrive::ReadUSNJournal(USN inStartUSN, Span<uint8> ioBuffer, taFunctionType inRecordCallback) const
{
	USN start_usn = inStartUSN;

	while (true)
	{
		constexpr uint32 cInterestingReasons =	USN_REASON_FILE_CREATE |		// File was created.
												USN_REASON_FILE_DELETE |		// File was deleted.
												USN_REASON_DATA_OVERWRITE |		// File was modified.
												USN_REASON_DATA_EXTEND |		// File was modified.
												USN_REASON_DATA_TRUNCATION |	// File was modified.
												USN_REASON_RENAME_NEW_NAME;		// File was renamed or moved (possibly to the recyle bin). That's essentially a delete and a create.

		READ_USN_JOURNAL_DATA_V1 journal_data;
		journal_data.StartUsn          = start_usn;
		journal_data.ReasonMask        = cInterestingReasons | USN_REASON_CLOSE; 
		journal_data.ReturnOnlyOnClose = true;			// Only get events when the file is closed (ie. USN_REASON_CLOSE is present). We don't care about earlier events.
		journal_data.Timeout           = 0;				// Never wait.
		journal_data.BytesToWaitFor    = 0;				// Never wait.
		journal_data.UsnJournalID      = mUSNJournalID;	// The journal we're querying.
		journal_data.MinMajorVersion   = 3;				// Doc says it needs to be 3 to use 128-bit file identifiers (ie. FileRefNumbers).
		journal_data.MaxMajorVersion   = 3;				// Don't want to support anything else.
		
		// Note: Use FSCTL_READ_UNPRIVILEGED_USN_JOURNAL to make that work without admin rights.
		uint32 available_bytes;
		if (!DeviceIoControl(mHandle, FSCTL_READ_UNPRIVILEGED_USN_JOURNAL, &journal_data, sizeof(journal_data), ioBuffer.data(), (uint32)ioBuffer.size(), &available_bytes, nullptr))
		{
			// TODO: test this but probably the only thing to do is to restart and re-scan everything (maybe the journal was deleted?)
			gApp.FatalError("Failed to read USN journal for {}:\\ - {}", mLetter, GetLastErrorString());
		}

		Span<uint8> available_buffer = ioBuffer.subspan(0, available_bytes);

		USN next_usn = *(USN*)available_buffer.data();
		available_buffer = available_buffer.subspan(sizeof(USN));

		if (next_usn == start_usn)
		{
			// Nothing more to read.
			break;
		}

		// Update the USN for next time.
		start_usn = next_usn;

		while (!available_buffer.empty())
		{
			const USN_RECORD_V3* record = (USN_RECORD_V3*)available_buffer.data();

			// Defer iterating to the next record so that we can use continue.
			defer { available_buffer = available_buffer.subspan(record->RecordLength); };

			// We get all events where USN_REASON_CLOSE is present, but we don't care about all of them.
			if ((record->Reason & cInterestingReasons) == 0)
				continue;

			// If the file is created and deleted in the same record, ignore it. We can't get any info about the file because it's already gone.
			// I think that can happen because of ReturnOnlyOnClose but unsure (ie. a file is created then deleted before its last handle was closed).
			// Happens a lot when monitoring C:/
			if ((record->Reason & (USN_REASON_FILE_DELETE | USN_REASON_FILE_CREATE)) == (USN_REASON_FILE_DELETE | USN_REASON_FILE_CREATE))
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

		if (inRecord.Reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME))
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
					PathBufferUTF8 dir_path_buffer;
					StringView     dir_path;

					// Root dir has an empty path, in this case don't add the slash.
					if (!deleted_file.mPath.empty())
						dir_path = gConcat(dir_path_buffer, deleted_file.mPath, "\\");

					for (FileInfo& file : repo.mFiles)
					{
						if (file.mID != deleted_file.mID && gStartsWith(file.mPath, dir_path))
						{
							repo.MarkFileDeleted(file, timestamp);

							if (gApp.mLogFSActivity >= LogLevel::Verbose)
								gApp.Log("Deleted {}", file);
						}
					}
				}
			}

		}

		if (inRecord.Reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME))
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
		if (gStartsWith(inFullPath, repo->mRootPath))
			return repo;
	}

	return nullptr;
}




void FileSystem::StartMonitoring()
{
	// Start the directory monitor thread.
	mMonitorDirThread = std::jthread(std::bind_front(&FileSystem::MonitorDirectoryThread, this));
}

void FileSystem::StopMonitoring()
{
	if (!IsMonitoringStarted())
		return;

	mMonitorDirThread.request_stop();
	KickMonitorDirectoryThread();
	mMonitorDirThread.join();

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


FileDrive* FileSystem::FindDrive(char inLetter)
{
	for (FileDrive& drive : mDrives)
		if (drive.mLetter == inLetter)
			return &drive;

	return nullptr;
}


FileID FileDrive::FindFileID(FileRefNumber inRefNumber) const
{
	std::lock_guard lock(mFilesMutex);

	auto it = mFilesByRefNumber.find(inRefNumber);
	if (it != mFilesByRefNumber.end())
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
	TempString512 root_path;
	root_path.mSize = GetFullPathNameA(inRootPath.AsCStr(), root_path.cCapacity, root_path.mBuffer, nullptr);

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

	PathBufferUTF8 abs_path_buffer;
	StringView abs_path = gConcat(abs_path_buffer, repo.mRootPath, file.GetDirectory());

	bool success = gCreateDirectoryRecursive(abs_path);

	if (!success)
		gApp.LogError("Failed to create directory for {}", file);

	return success;
}


bool FileSystem::DeleteFile(FileID inFileID)
{
	const FileInfo& file = GetFile(inFileID);
	const FileRepo& repo = GetRepo(inFileID);

	PathBufferUTF8 abs_path_buffer;
	StringView abs_path = gConcat(abs_path_buffer, repo.mRootPath, file.mPath);

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



void FileSystem::InitialScan(std::stop_token inStopToken, Span<uint8> ioBufferUSN)
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
	mInitState = InitState::Scanning;

	// Don't use too many threads otherwise they'll just spend their time on the hashmap mutex.
	// TODO this could be improved
	const int scan_thread_count = gMin((int)std::thread::hardware_concurrency(), 4);

	// Prepare a scan queue that can be used by multiple threads.
	ScanQueue scan_queue;
	scan_queue.mDirectories.reserve(1024);
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
		std::vector<std::thread> scan_threads;
		scan_threads.resize(scan_thread_count);
		for (auto& thread : scan_threads)
		{
			thread = std::thread([&]() 
			{
				gSetCurrentThreadName(L"Scan Directory Thread");

				uint8 buffer_scan[32 * 1024];

				// Process the queue until it's empty.
				FileID dir_id;
				while ((dir_id = scan_queue.Pop()) != FileID::cInvalid())
				{
					FileRepo& repo = gFileSystem.GetRepo(dir_id);

					repo.ScanDirectory(dir_id, scan_queue, buffer_scan);

					if (inStopToken.stop_requested())
						return;
				}
			});
		}

		// Wait for the threads to finish their work.
		for (auto& thread : scan_threads)
			thread.join();

		if (inStopToken.stop_requested())
			return;
	}

	gAssert(scan_queue.mDirectories.empty());
	gAssert(scan_queue.mThreadsBusy == 0);

	size_t total_files = 0;
	for (auto& repo : mRepos)
		if (!repo.mDrive.mLoadedFromCache)
			total_files += repo.mFiles.SizeRelaxed();

	gApp.Log("Done. Found {} files in {:.2f} seconds.", 
		total_files, gTicksToSeconds(timer.GetTicks()));

	mInitState = InitState::ReadingUSNJournal;

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
	SegmentedVector<FileID> files_without_usn;
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
				files_without_usn.emplace_back(file.mID);
		}
	}

	if (inStopToken.stop_requested())
		return;

	mInitStats.mIndividualUSNToFetch = (int)files_without_usn.size();
	mInitStats.mIndividualUSNFetched = 0;
	mInitState = InitState::ReadingIndividualUSNs;

	if (!files_without_usn.empty())
	{
		gApp.Log("{} files were not present in the USN journal. Fetching their USN manually now.", files_without_usn.size());

		// Don't create too many threads because they'll get stuck in locks in OpenFileByRefNumber if the cache is warm,
		// or they'll be bottlenecked by IO otherwise.
		const int usn_thread_count = gMin((int)std::thread::hardware_concurrency(), 4);

		// Create temporary worker threads to get all the missing USNs.
		std::vector<std::thread> usn_threads;
		usn_threads.resize(usn_thread_count);
		std::atomic_int current_index = 0;
		for (auto& thread : usn_threads)
		{
			thread = std::thread([&]() 
			{
				gSetCurrentThreadName(L"USN Read Thread");

				// Note: Could do better than having all threads hammer the same atomic, but the cost is negligible compared to OpenFileByRefNumber.
				int index;
				while ((index = current_index++) < (int)files_without_usn.size())
				{
					FileID      file_id     = files_without_usn[index];
					FileRepo&   repo        = file_id.GetRepo();
					FileInfo&   file        = file_id.GetFile();

					// Get the USN.
					repo.ScanFile(file, FileRepo::RequestedAttributes::USNOnly);

					mInitStats.mIndividualUSNFetched++;

					if (inStopToken.stop_requested())
						return;
				}
			});
		}

		// Wait for the threads to finish their work.
		for (auto& thread : usn_threads)
			thread.join();

		if (inStopToken.stop_requested())
			return;

		gApp.Log("Done. Fetched {} individual USNs in {:.2f} seconds.", 
			files_without_usn.size(), gTicksToSeconds(timer.GetTicks()));
	}
}


void FileSystem::KickMonitorDirectoryThread()
{
	mMonitorDirThreadSignal.release();
}


void FileSystem::MonitorDirectoryThread(std::stop_token inStopToken)
{
	gSetCurrentThreadName(L"Monitor Directory Thread");
	using namespace std::chrono_literals;

	// Start not idle.
	mIsMonitorDirThreadIdle = false;

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
			if (inStopToken.stop_requested())
				break;
		}

		if (inStopToken.stop_requested())
			break;
	}
	
	// Scan the drives that were not intialized from the cache.
	InitialScan(inStopToken, buffer_usn);
	
	mInitState = InitState::PreparingCommands;

	// Create the commands for all the files.
	for (auto& repo : mRepos)
		for (auto& file : repo.mFiles)
			gCookingSystem.CreateCommandsForFile(file);

	// Check which commmands need to cook.
	gCookingSystem.UpdateDirtyStates();

	mInitStats.mReadyTicks = gGetTickCount();
	mInitState = InitState::Ready;

	// Once the scan is finished, start cooking.
	gCookingSystem.StartCooking();

	while (!inStopToken.stop_requested())
	{
		bool any_work_done = false;

		// Check the queue of files to re-scan (we try again if it eg. fails because the file was in use).
		while (true)
		{
			int64  current_time = gGetTickCount();
			FileID file_to_rescan;
			{
				// Check if there's an item in the queue, and if it's time to scan it again.
				std::lock_guard lock(mFilesToRescanMutex);
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

				if (inStopToken.stop_requested())
					break;
			}

			if (inStopToken.stop_requested())
				break;
		}

		// Note: we don't update any_work_done here because we don't want to cause a busy loop waiting to update commands that are still cooking.
		// Instead the cooking threads will wake this thread up any time a command finishes (which usually also means there are file changes to process).
		gCookingSystem.ProcessUpdateDirtyStates();

		if (!any_work_done)
		{
			// Going idle here.
			mIsMonitorDirThreadIdle = true;

			// Wait for some time before checking the USN journals again (unless we're being signaled).
			std::ignore = mMonitorDirThreadSignal.try_acquire_for(1s);

			// Not idle anymore.
			mIsMonitorDirThreadIdle = false;
		}
	}

	// Only save the state if we've finished scanning when we exit (don't save an incomplete state).
	if (GetInitState() == InitState::Ready)
		SaveCache();
}


void FileSystem::RescanLater(FileID inFileID)
{
	constexpr double cFileRescanDelayMS = 300.0; // In milliseconds.

	std::lock_guard lock(mFilesToRescanMutex);
	mFilesToRescan.PushBack({ inFileID, gGetTickCount() + gMillisecondsToTicks(cFileRescanDelayMS) });
}


// Helper class to write a binary file.
struct BinaryWriter : NoCopy
{
	BinaryWriter()
	{
		// Lock once to avoid the overhead of locking many times.
		// TODO make the mutex a template param to have a dummy one when not needed?
		mBufferLock = mBuffer.Lock();
	}

	// Write the internal buffer to this file.
	bool WriteFile(FILE* ioFile)
	{
		// Compress the data with LZ4.
		// LZ4HC gives a slightly better ratio but is 10 times as slow, so not worth it here.
		int    uncompressed_size   = (int)mBuffer.SizeRelaxed();
		int    compressed_size_max = LZ4_compressBound(uncompressed_size);
		char*  compressed_buffer   = (char*)malloc(compressed_size_max);
		int    compressed_size     = LZ4_compress_default((const char*)mBuffer.Begin(), compressed_buffer, uncompressed_size, compressed_size_max);

		defer { free(compressed_buffer); };

		// Write the uncompressed size first, we'll need it to decompress.
		if (fwrite(&uncompressed_size, sizeof(uncompressed_size), 1, ioFile) != 1)
			return false;

		// Write the compressed data.
		int written_size = fwrite(compressed_buffer, 1, compressed_size, ioFile);
		return written_size == compressed_size;
	}

	template <typename taType>
	void Write(Span<const taType> inSpan)
	{
		static_assert(std::has_unique_object_representations_v<taType>); // Don't write padding into the file.

		size_t size_bytes = inSpan.size_bytes();

		Span dest = mBuffer.EnsureCapacity(size_bytes, mBufferLock);
		memcpy(dest.data(), inSpan.data(), size_bytes);
		mBuffer.IncreaseSize(size_bytes, mBufferLock);
	}

	template <typename taType>
	void Write(const taType& inValue)
	{
		Write(Span(&inValue, 1));
	}

	void Write(StringView inStr)
	{
		Write((uint16)inStr.size());
		Write(Span(inStr));
	}

	void WriteLabel(Span<const char> inLabel)
	{
		// Don't write the null terminator, we don't need it/don't read it back.
		Write(inLabel.subspan(0, inLabel.size() - 1));
	}

	VMemArray<uint8> mBuffer = { 1'000'000'000, 256ull * 1024 };
	VMemArrayLock    mBufferLock;
};


// Helper class to read a binary file.
struct BinaryReader : NoCopy
{
	BinaryReader()
	{
		// Lock once to avoid the overhead of locking many times.
		// TODO make the mutex a template param to have a dummy one when not needed?
		mBufferLock = mBuffer.Lock();
	}

	// Read the entire file into the internal buffer.
	bool ReadFile(FILE* inFile)
	{
		if (fseek(inFile, 0, SEEK_END) != 0)
			return false;

		int file_size = ftell(inFile);
		if (file_size == -1)
			return false;

		if (fseek(inFile, 0, SEEK_SET) != 0)
			return false;

		// First read the uncompressed size.
		int uncompressed_size = 0;
		if (fread(&uncompressed_size, sizeof(uncompressed_size), 1, inFile) != 1)
			return false;

		// Then read the compressed data.
		int   compressed_size   = file_size - (int)sizeof(uncompressed_size);
		char* compressed_buffer = (char*)malloc(compressed_size);
		defer { free(compressed_buffer); };

		if (fread(compressed_buffer, 1, compressed_size, inFile) != compressed_size)
			return false;

		Span uncompressed_buffer = mBuffer.EnsureCapacity(uncompressed_size, mBufferLock);

		// Decompress the data.
		int actual_uncompressed_size = LZ4_decompress_safe(compressed_buffer, (char*)uncompressed_buffer.data(), compressed_size, uncompressed_buffer.size());
		gAssert(actual_uncompressed_size == uncompressed_size);

		mBuffer.IncreaseSize(uncompressed_size, mBufferLock);

		return true;
	}

	template <typename taType>
	void Read(Span<taType> outSpan)
	{
		if (mCurrentOffset + outSpan.size_bytes() > mBuffer.SizeRelaxed())
		{
			mError = true;
			return;
		}

		memcpy(outSpan.data(), mBuffer.Begin() + mCurrentOffset, outSpan.size_bytes());
		mCurrentOffset += outSpan.size_bytes();
	}

	template <typename taType>
	void Read(taType& outValue)
	{
		Read(Span(&outValue, 1));
	}

	template <size_t taSize>
	void Read(TempString<taSize>& outStr)
	{
		uint16 size = 0;
		Read(size);

		if (size > outStr.cCapacity - 1)
		{
			gAssert(false);
			gApp.LogError("FileReader tried to read a string of size {} in a TempString{}, it does not fit!", size, outStr.cCapacity);
			mError = true;

			// Skip the string instead of reading it.
			Skip(size);
		}
		else
		{
			outStr.mSize = size;
			outStr.mBuffer[size] = 0;
			Read(Span(outStr.mBuffer, size));
		}
	}

	void Skip(size_t inSizeInBytes)
	{
		if (mCurrentOffset + inSizeInBytes > mBuffer.SizeRelaxed())
		{
			mError = true;
			return;
		}

		mCurrentOffset += inSizeInBytes;
	}

	template <size_t taSize>
	bool ExpectLabel(const char (& inLabel)[taSize])
	{
		gAssert(inLabel[taSize - 1] == 0);
		constexpr int cLabelSize = taSize - 1; // Ignore null terminator

		char read_label[16];
		static_assert(cLabelSize <= gElemCount(read_label));
		Read(Span(read_label, cLabelSize));

		if (memcmp(inLabel, read_label, cLabelSize) != 0)
		{
			// TODO log an error here
			mError = true;
		}

		return !mError; // This will return false if there was an error before, even if the label is correct. That's on purpose, we use this to early out.
	}

	VMemArray<uint8> mBuffer = { 1'000'000'000, 256ull * 1024 };
	VMemArrayLock    mBufferLock;
	size_t           mCurrentOffset = 0;
	bool             mError = false;
};



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


constexpr int        cStateFormatVersion = 1;
constexpr StringView cCacheFileName      = "cache.bin";

void FileSystem::LoadCache()
{
	gApp.Log("Loading cached state.");
	Timer timer;
	mInitState = InitState::LoadingCache;

	TempString256 cache_file_path(R"({}\{})", gApp.mCacheDirectory, cCacheFileName);
	FILE*         cache_file = fopen(cache_file_path.AsCStr(), "rb");

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
	if (format_version != cStateFormatVersion)
	{
		gApp.Log("Unsupported cached state version, ignoring cache. (Expected: {} Found: {}).", cStateFormatVersion, format_version);
		return;
	}

	std::vector<StringView> all_valid_repos;
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
				drive_valid = false;
			}
		}

		uint16 repo_count = 0;
		bin.Read(repo_count);
		total_repo_count += repo_count;

		std::vector<StringView> valid_repos;

		for (int repo_index = 0; repo_index < (int)repo_count; ++repo_index)
		{
			if (!bin.ExpectLabel("REPO"))
				break; // Early out if reading is failing.

			TempString128 repo_name;
			bin.Read(repo_name);

			TempString512 repo_path;
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
				if (!gIsEqual(repo->mRootPath, repo_path))
				{
					gApp.LogError(R"(Repo "{}" root path changed, ignoring cache.)", repo_name);
					repo_valid = false;
				}
			}

			if (drive_valid && repo_valid)
				valid_repos.push_back(repo->mName);
		}

		// If all repos for this drive are valid, we can use the cached state.
		if (valid_repos.size() == drive->mRepos.size())
		{
			// Set the next USN we should read.
			drive->mNextUSN = next_usn;

			// Remember we're loading this drive from the cache to skip the initial scan.
			drive->mLoadedFromCache = true;

			// Add the repos names to the list of valid repos so that we read their content later.
			all_valid_repos.insert(all_valid_repos.end(), valid_repos.begin(), valid_repos.end());
		}
	}

	for (int repo_index = 0; repo_index < total_repo_count; ++repo_index)
	{
		if (!bin.ExpectLabel("REPO_CONTENT"))
			break;

		TempString128 repo_name;
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

		MutStringView all_strings;
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
					all_strings.subspan(serialized_file_info.mPathOffset, serialized_file_info.mPathSize), 
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

	bin.ExpectLabel("FIN");

	if (bin.mError)
		gApp.FatalError(R"(Corrupted cached state. Delete the file and try again ("{}")).)", cache_file_path);

	size_t total_files = 0;
	for (auto& repo : mRepos)
		if (repo.mDrive.mLoadedFromCache)
			total_files += repo.mFiles.SizeRelaxed();

	gApp.Log("Done. Found {} Files in {:.2f} seconds.", 
		total_files, gTicksToSeconds(timer.GetTicks()));
}


void FileSystem::SaveCache()
{
	// Make sure the cache dir exists.
	CreateDirectoryA(gApp.mCacheDirectory.c_str(), nullptr);

	TempString256 cache_file_path(R"({}\{})", gApp.mCacheDirectory, cCacheFileName);
	FILE*         cache_file = fopen(cache_file_path.AsCStr(), "wb");

	if (cache_file == nullptr)
		gApp.FatalError(R"(Failed to save cached state ("{}") - {} (0x{:X}))", cache_file_path, strerror(errno), errno);

	BinaryWriter bin;

	bin.WriteLabel("VERSION");
	bin.Write(cStateFormatVersion);

	std::vector<StringView> valid_repos;
	int total_repo_count = 0;

	// Read all drives and repos.
	bin.Write((uint16)mDrives.Size());
	for (const FileDrive& drive : mDrives)
	{
		bin.WriteLabel("DRIVE");

		bin.Write(drive.mLetter);
		bin.Write(drive.mUSNJournalID);
		bin.Write(drive.mNextUSN);

		bin.Write((uint16)drive.mRepos.size());
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
			string_pool_bytes += file.mPath.size() + 1; // + 1 for null terminator.
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

			bin.Write(Span(file.mPath.data(), file.mPath.size() + 1)); // + 1 to include null terminator.
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
			serialized_file_info.mPathSize       = file.mPath.size();
			serialized_file_info.mIsDirectory    = file.IsDirectory();
			serialized_file_info.mRefNumber      = file.mRefNumber;
			serialized_file_info.mCreationTime   = file.mCreationTime;
			serialized_file_info.mLastChangeUSN  = file.mLastChangeUSN;
			serialized_file_info.mLastChangeTime = file.mLastChangeTime;

			bin.Write(serialized_file_info);

			current_offset += file.mPath.size() + 1;
		}
	}

	bin.WriteLabel("FIN");

	if (!bin.WriteFile(cache_file))
		gApp.FatalError(R"(Failed to save cached state ("{}") - {} (0x{:X}))", cache_file_path, strerror(errno), errno);

	fclose(cache_file);
}