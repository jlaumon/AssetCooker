#include "FileSystem.h"
#include "App.h"
#include "Debug.h"
#include "Ticks.h"

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"
#include "win32/threads.h"

#include "xxHash/xxh3.h"

#include <array>

#include "CookingSystem.h"

// These are not exactly the max path length allowed by Windows in all cases, but should be good enough.
// TODO: these numbers are actually ridiculously high, lower them (except maybe in scanning code) and crash hard and blame user if they need more
constexpr size_t cMaxPathSizeUTF16 = 32768;
constexpr size_t cMaxPathSizeUTF8  = 32768 * 3ull;	// UTF8 can use up to 6 bytes per character, but let's suppose 3 is good enough on average.

using PathBufferUTF16 = std::array<wchar_t, cMaxPathSizeUTF16>;
using PathBufferUTF8  = std::array<char, cMaxPathSizeUTF8>;


// Replaces / by \.
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
	return inPath.size() >= 3 && gIsAlpha(inPath[0]) && inPath.substr(1, 2) == ":\\"; 
}


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
	PathBufferUTF16 wpath_buffer;
	std::optional wpath = gUtf8ToWideChar(abs_path, wpath_buffer);
	if (!wpath)
		gApp.FatalError("Failed to convert path {} to WideChar", inPath);

	// Convert it to uppercase.
	PathBufferUTF16 uppercase_buffer;
	int uppercase_size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wpath->data(), (int)wpath->size(), uppercase_buffer.data(), uppercase_buffer.size() / 2, nullptr, nullptr, 0);
	if (uppercase_size == 0)
		gApp.FatalError("Failed to convert path {} to uppercase", inPath);

	std::wstring_view uppercase_wpath = { uppercase_buffer.data(), (size_t)uppercase_size };

	// Hash the uppercase version.
	XXH128_hash_t hash_xx = XXH3_128bits(uppercase_wpath.data(), uppercase_wpath.size() * sizeof(uppercase_wpath[0]));

	// Convert to our hash wrapper.
	Hash128       path_hash;
	static_assert(sizeof(path_hash.mData) == sizeof(hash_xx));
	memcpy(path_hash.mData, &hash_xx, sizeof(path_hash.mData));

	return path_hash;
}


// Set the name of the current thread.
void gSetCurrentThreadName(const wchar_t* inName)
{
	SetThreadDescription(GetCurrentThread(), inName);
}


String gUSNReasonToString(uint32 inReason)
{
	String str;

	if (inReason & USN_REASON_DATA_OVERWRITE				) str += "DATA_OVERWRITE | ";
	if (inReason & USN_REASON_DATA_EXTEND					) str += "DATA_EXTEND | ";
	if (inReason & USN_REASON_DATA_TRUNCATION				) str += "DATA_TRUNCATION | ";
	if (inReason & USN_REASON_NAMED_DATA_OVERWRITE			) str += "NAMED_DATA_OVERWRITE | ";
	if (inReason & USN_REASON_NAMED_DATA_EXTEND				) str += "NAMED_DATA_EXTEND | ";
	if (inReason & USN_REASON_NAMED_DATA_TRUNCATION			) str += "NAMED_DATA_TRUNCATION | ";
	if (inReason & USN_REASON_FILE_CREATE					) str += "FILE_CREATE | ";
	if (inReason & USN_REASON_FILE_DELETE					) str += "FILE_DELETE | ";
	if (inReason & USN_REASON_EA_CHANGE						) str += "EA_CHANGE | ";
	if (inReason & USN_REASON_SECURITY_CHANGE				) str += "SECURITY_CHANGE | ";
	if (inReason & USN_REASON_RENAME_OLD_NAME				) str += "RENAME_OLD_NAME | ";
	if (inReason & USN_REASON_RENAME_NEW_NAME				) str += "RENAME_NEW_NAME | ";
	if (inReason & USN_REASON_INDEXABLE_CHANGE				) str += "INDEXABLE_CHANGE | ";
	if (inReason & USN_REASON_BASIC_INFO_CHANGE				) str += "BASIC_INFO_CHANGE | ";
	if (inReason & USN_REASON_HARD_LINK_CHANGE				) str += "HARD_LINK_CHANGE | ";
	if (inReason & USN_REASON_COMPRESSION_CHANGE			) str += "COMPRESSION_CHANGE | ";
	if (inReason & USN_REASON_ENCRYPTION_CHANGE				) str += "ENCRYPTION_CHANGE | ";
	if (inReason & USN_REASON_OBJECT_ID_CHANGE				) str += "OBJECT_ID_CHANGE | ";
	if (inReason & USN_REASON_REPARSE_POINT_CHANGE			) str += "REPARSE_POINT_CHANGE | ";
	if (inReason & USN_REASON_STREAM_CHANGE					) str += "STREAM_CHANGE | ";
	if (inReason & USN_REASON_TRANSACTED_CHANGE				) str += "TRANSACTED_CHANGE | ";
	if (inReason & USN_REASON_INTEGRITY_CHANGE				) str += "INTEGRITY_CHANGE | ";
	if (inReason & USN_REASON_DESIRED_STORAGE_CLASS_CHANGE	) str += "DESIRED_STORAGE_CLASS_CHANGE | ";
	if (inReason & USN_REASON_CLOSE							) str += "CLOSE | ";

	if (str.empty())
		str = "None";
	else
		str.resize(str.size() - 3); // Remove " | "

	return str;
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


SystemTime FileTime::ToLocalTime() const
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


SystemTime SystemTime::ToLocalTime() const
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


// Find the offset of the first '.' in the path.
static uint16 sFindExtensionPos(uint16 inNamePos, StringView inPath)
{
	StringView file_name = inPath.substr(inNamePos);

	size_t offset = file_name.find_first_of('.');
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


static bool sDirectoryExistsW(std::wstring_view inPath)
{
	gAssert(!inPath.ends_with(L'\\'));
	gAssert(*(inPath.data() + inPath.size()) == 0);

	DWORD attributes = GetFileAttributesW(inPath.data());

	return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

static bool sCreateDirectoryW(std::wstring_view inPath)
{
	gAssert(!inPath.ends_with(L'\\'));
	gAssert(*(inPath.data() + inPath.size()) == 0);

	BOOL success = CreateDirectoryW(inPath.data(), nullptr);

	return success || GetLastError() == ERROR_ALREADY_EXISTS;
}


static bool sCreateDirectoryRecursiveW(std::span<wchar_t> ioPath)
{
	gAssert(ioPath[ioPath.size() - 1] == 0);
	gAssert(ioPath[ioPath.size() - 2] != L'\\');
	gAssert(ioPath.size() > 3 && ioPath[1] == L':' && ioPath[2] == L'\\'); // We expect an absolute path, first three characters should be the drive.

	// Early out if the directory already exists.
	if (sDirectoryExistsW(std::wstring_view(ioPath.data(), ioPath.size() - 1)))
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

	PathBufferUTF16 wpath_buffer;
	std::optional   wpath_optional = gUtf8ToWideChar(inAbsolutePath, wpath_buffer);
	if (!wpath_optional)
		return false;

	std::wstring_view wpath = *wpath_optional;

	// If the path ends with a slash, remove it, because that's what the other functions expect.
	if (wpath.back() == L'\\')
	{
		wpath.remove_suffix(1);
		wpath_buffer[wpath.size()] = 0; // Replace slash with null terminator.
	}

	return sCreateDirectoryRecursiveW({ wpath_buffer.data(), wpath.size() + 1 }); // +1 to include the null terminator.
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
	PathBufferUTF16 root_path_buffer;
	std::optional   root_path_wchar = gUtf8ToWideChar(mRootPath, root_path_buffer);
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
		std::unique_lock lock(gFileSystem.mFilesMutex);

		// Prepare a new FileID in case this file wasn't already added.
		FileID          new_file_id = { mIndex, (uint32)mFiles.size() };
		FileID          actual_file_id;

		// Try to insert it to the path hash map.
		{
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
							gApp.LogError("{} chandged ref number unexpectedly (missed event?)", file);

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
			auto [it, inserted] = gFileSystem.mFilesByRefNumber.insert({ inRefNumber, actual_file_id });
			if (!inserted)
			{
				FileID previous_file_id = it->second;

				// Check if the existing file is the same (ie. same path).
				// The file could have been renamed but kept the same ref number (and we've missed that rename event?).
				if (previous_file_id != actual_file_id || GetFile(previous_file_id).mPathHash != path_hash)
				{
					gApp.LogError("Unexpected file deletion detected! {}", GetFile(previous_file_id));

					// Mark the old file as deleted, and add the new one instead.
					MarkFileDeleted(GetFile(previous_file_id), {}, lock);
				}
			}
		}


		if (actual_file_id == new_file_id)
		{
			// The file wasn't already known, add it to the list.
			file = &mFiles.emplace_back(new_file_id, gNormalizePath(mStringPool.AllocateCopy(inPath)), path_hash, inType, inRefNumber);
		}
		else
		{
			// The file was known, return it.
			file = &GetFile(actual_file_id);

			if (file->GetType() != inType)
			{
				// TODO we could support changing the file type if we make sure to update any list of all directories
				gApp.FatalError("A file was turned into a directory (or vice versa). This is not supported yet.");
			}
		}
	}

	// Create all the commands that take this file as input (this may add more (non-existing) files).
	gCookingSystem.CreateCommandsForFile(*file);

	return *file;
}

void FileRepo::MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp)
{
	// TODO: not great to access these internals, maybe find a better way?
	std::unique_lock lock(gFileSystem.mFilesMutex);

	MarkFileDeleted(ioFile, inTimeStamp, lock);
}

void FileRepo::MarkFileDeleted(FileInfo& ioFile, FileTime inTimeStamp, const std::unique_lock<std::mutex>& inLock)
{
	gAssert(inLock.mutex() == &gFileSystem.mFilesMutex);

	gFileSystem.mFilesByRefNumber.erase(ioFile.mRefNumber);
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



static std::optional<StringView> sBuildFilePath(StringView inParentDirPath, std::wstring_view inFileNameW, MutStringView ioBuffer)
{
	MutStringView file_path = ioBuffer;

	// Add the parent dir if there's one (can be empty for the root dir).
	if (!inParentDirPath.empty())
	{
		ioBuffer = gAppend(ioBuffer, inParentDirPath);
		ioBuffer = gAppend(ioBuffer, "\\");
	}

	std::optional file_name = gWideCharToUtf8(inFileNameW, ioBuffer);
	if (!file_name)
	{
		// Failed for some reason. Buffer too small?
		gAssert(false); // Investigate.

		return std::nullopt;
	}

	return StringView{ file_path.data(), gEndPtr(*file_name) };
}


void FileRepo::ScanDirectory(std::vector<FileID>& ioScanQueue, std::span<uint8> ioBuffer)
{
	// Grab one directory from the queue.
	FileID dir_id = ioScanQueue.back();
	ioScanQueue.pop_back();

	FileInfo& dir = GetFile(dir_id); 
	gAssert(dir.IsDirectory());

	OwnedHandle dir_handle = mDrive.OpenFileByRefNumber(dir.mRefNumber);
	if (!dir_handle.IsValid())
	{
		// TODO: depending on error, we should probably re-queue for scan
		if (gApp.mLogFSActivity >= LogLevel::Normal)
			gApp.LogError("Failed to open {} - {}", dir, GetLastErrorString());
		return;
	}

	if (gApp.mLogFSActivity >= LogLevel::Normal)
		gApp.Log("Added {}", dir);


	// First GetFileInformationByHandleEx call needs a different value to make it "restart".
	FILE_INFO_BY_HANDLE_CLASS file_info_class = FileIdExtdDirectoryRestartInfo;

	while (true)
	{
		// Fill the scan buffer with content to iterate.
		if (!GetFileInformationByHandleEx(dir_handle, file_info_class, ioBuffer.data(), ioBuffer.size()))
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

			std::wstring_view wfilename = { entry->FileName, entry->FileNameLength / 2 };

			// Ignore current/parent dir.
			if (wfilename == L"." || wfilename == L"..")
				continue;

			// Build the file path.
			PathBufferUTF8 path_buffer;
			std::optional path = sBuildFilePath(dir.mPath, wfilename, path_buffer);

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
				ioScanQueue.push_back(file.mID);
			}
			else
			{
				file.mCreationTime   = entry->ChangeTime.QuadPart;
				file.mLastChangeTime = entry->ChangeTime.QuadPart;

				// Update the USN.
				// TODO: this is by far the slowest part, find another way? FSCTL_ENUM_USN_DATA maybe?
				{
					OwnedHandle file_handle = mDrive.OpenFileByRefNumber(file.mRefNumber);
					if (!dir_handle.IsValid())
					{
						// TODO: depending on error, we should probably re-queue for scan
						if (gApp.mLogFSActivity>= LogLevel::Normal)
							gApp.LogError("Failed to open {} - {}", file, GetLastErrorString());
					}

					file.mLastChangeUSN = mDrive.GetUSN(file_handle);
				}

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


FileDrive::FileDrive(char inDriveLetter)
{
	// Store the drive letter;
	mLetter = inDriveLetter;

	// Get a handle to the drive.
	// Note: Only request FILE_TRAVERSE to make that work without admin rights.
	mHandle = CreateFileA(std::format(R"(\\.\{}:)", mLetter).c_str(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!mHandle.IsValid())
		gApp.FatalError(R"(Failed to get handle to {}:\ - {})", mLetter, GetLastErrorString());

	// Query the USN journal to get its ID.
	USN_JOURNAL_DATA_V0 journal_data;
	uint32				unused;
	if (!DeviceIoControl(mHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &unused, nullptr))
		gApp.FatalError(R"(Failed to query USN journal for {}:\ - {})", mLetter, GetLastErrorString());

	// Store the jorunal ID.
	mUSNJournalID = journal_data.UsnJournalID;

	// Store the current USN.
	// TODO: we should read that from saved stated instead.
	mNextUSN = journal_data.NextUsn;

	gApp.Log(R"(Queried USN journal for {}:\. ID: 0x{:08X}. Max size: {})", mLetter, mUSNJournalID, SizeInBytes(journal_data.MaximumSize));
}


OwnedHandle FileDrive::OpenFileByRefNumber(FileRefNumber inRefNumber) const
{
	FILE_ID_DESCRIPTOR file_id_descriptor;
	file_id_descriptor.dwSize			= sizeof(FILE_ID_DESCRIPTOR);
	file_id_descriptor.Type				= ExtendedFileIdType;
	file_id_descriptor.ExtendedFileId	= inRefNumber.ToWin32();

	constexpr DWORD flags_and_attributes = 0
		| FILE_FLAG_BACKUP_SEMANTICS	// Required to open directories.
		//| FILE_FLAG_SEQUENTIAL_SCAN	 // Helps prefetching if we only read sequentially. Useful if we want to hash the files?
	;

	// TODO: error checking, some errors are probably ok and some aren't
	// Note: invalid parameter error means file does not exist
	OwnedHandle handle = OpenFileById(mHandle, &file_id_descriptor, FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, flags_and_attributes);

	return handle;
}


std::optional<StringView> FileDrive::GetFullPath(const OwnedHandle& inFileHandle, MutStringView ioBuffer) const
{
	// Get the full path as utf16.
	// PFILE_NAME_INFO contains the filename without the drive letter and column in front (ie. without the C:).
	PathBufferUTF16 wpath_buffer;
	PFILE_NAME_INFO file_name_info = (PFILE_NAME_INFO)wpath_buffer.data();
	if (!GetFileInformationByHandleEx(inFileHandle, FileNameInfo, file_name_info, wpath_buffer.size() * sizeof(wpath_buffer[0])))
		return {};

	std::wstring_view wpath = { file_name_info->FileName, file_name_info->FileNameLength / 2 };

	// Write the drive part.
	ioBuffer[0] = mLetter;
	ioBuffer[1] = ':';

	// Write the path part.
	std::optional path_part = gWideCharToUtf8(wpath, ioBuffer.subspan(2));
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


bool FileDrive::ProcessMonitorDirectory(std::span<uint8> ioUSNBuffer, std::span<uint8> ioDirScanBuffer)
{
	constexpr uint32 cInterestingReasons =	USN_REASON_FILE_CREATE |		// File was created.
											USN_REASON_FILE_DELETE |		// File was deleted.
											USN_REASON_DATA_OVERWRITE |		// File was modified.
											USN_REASON_DATA_EXTEND |		// File was modified.
											USN_REASON_DATA_TRUNCATION |	// File was modified.
											USN_REASON_RENAME_NEW_NAME;		// File was renamed or moved (possibly to the recyle bin). That's essentially a delete and a create.

	READ_USN_JOURNAL_DATA_V1 journal_data;
	journal_data.StartUsn          = mNextUSN;
	journal_data.ReasonMask        = cInterestingReasons | USN_REASON_CLOSE; 
	journal_data.ReturnOnlyOnClose = true;			// Only get events when the file is closed (ie. USN_REASON_CLOSE is present). We don't care about earlier events.
	journal_data.Timeout           = 0;				// Never wait.
	journal_data.BytesToWaitFor    = 0;				// Never wait.
	journal_data.UsnJournalID      = mUSNJournalID;	// The journal we're querying.
	journal_data.MinMajorVersion   = 3;				// Doc says it needs to be 3 to use 128-bit file identifiers (ie. FileRefNumbers).
	journal_data.MaxMajorVersion   = 3;				// Don't want to support anything else.
	
	// Note: Use FSCTL_READ_UNPRIVILEGED_USN_JOURNAL to make that work without admin rights.
	uint32 available_bytes;
	if (!DeviceIoControl(mHandle, FSCTL_READ_UNPRIVILEGED_USN_JOURNAL, &journal_data, sizeof(journal_data), ioUSNBuffer.data(), (uint32)ioUSNBuffer.size(), &available_bytes, nullptr))
	{
		// TODO: test this but probably the only thing to do is to restart and re-scan everything (maybe the journal was deleted?)
		gApp.FatalError("Failed to read USN journal for {}:\\ - {}", mLetter, GetLastErrorString());
	}

	std::span<uint8> available_buffer = ioUSNBuffer.subspan(0, available_bytes);

	USN next_usn = *(USN*)available_buffer.data();
	available_buffer = available_buffer.subspan(sizeof(USN));

	if (next_usn == mNextUSN)
	{
		// Nothing happened.
		return false;
	}

	// Update the USN for next time.
	mNextUSN = next_usn;

	// Keep a scan queue outside the loop, to re-use it if we need to scan new directories.
	std::vector<FileID> scan_queue;

	while (!available_buffer.empty())
	{
		const USN_RECORD_V3* record = (USN_RECORD_V3*)available_buffer.data();

		// Defer iterating to the next record so that we can use continue.
		defer { available_buffer = available_buffer.subspan(record->RecordLength); };

		// We get all events where USN_REASON_CLOSE is present, but we don't care about all of them.
		if ((record->Reason & cInterestingReasons) == 0)
			continue;

		bool is_directory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

		if (record->Reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME))
		{
			// If the file is in a repo, mark it as deleted.
			FileInfo* deleted_file = gFileSystem.FindFile(record->FileReferenceNumber);
			if (deleted_file)
			{
				FileTime  timestamp = record->TimeStamp.QuadPart;

				FileRepo& repo = gFileSystem.GetRepo(deleted_file->mID);

				repo.MarkFileDeleted(*deleted_file, timestamp);

				if (gApp.mLogFSActivity >= LogLevel::Verbose)
					gApp.Log("Deleted {}", *deleted_file);

				// If it's a directory, also mark all the file inside as deleted.
				if (deleted_file->IsDirectory())
				{
					PathBufferUTF8 dir_path_buffer;
					StringView     dir_path;

					// Root dir has an empty path, in this case don't att the slash.
					if (!deleted_file->mPath.empty())
						dir_path = gConcat(dir_path_buffer, deleted_file->mPath, "\\");

					for (FileInfo& file : repo.mFiles)
					{
						if (file.mID != deleted_file->mID && gStartsWith(file.mPath, dir_path))
						{
							repo.MarkFileDeleted(file, timestamp);

							if (gApp.mLogFSActivity >= LogLevel::Verbose)
								gApp.Log("Deleted {}", file);
						}
					}
				}
			}

		}

		if (record->Reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME))
		{
			// Get a handle to the file.
			OwnedHandle file_handle = OpenFileByRefNumber(record->FileReferenceNumber);
			if (!file_handle.IsValid())
			{
				// TODO: probably need to retry later or something depending on the error, or scan the parent dir instead? we can't just ignore it (unless it's because the file was deleted already)
				gApp.LogError("Failed to open newly created file {} - {}", FileRefNumber(record->FileReferenceNumber), GetLastErrorString());
				continue;
			}

			// Get its path.
			PathBufferUTF8 buffer;
			std::optional full_path = GetFullPath(file_handle, buffer);
			if (!full_path)
			{
				// TODO: same remark as failing to open
				gApp.LogError("Failed to get path for newly created file {} - {}", FileRefNumber(record->FileReferenceNumber), GetLastErrorString());
				continue;
			}

			// Check if it's in a repo, otherwise ignore.
			FileRepo* repo = FindRepoForPath(*full_path);
			if (repo)
			{
				// Get the file path relative to the repo root.
				StringView file_path = repo->RemoveRootPath(*full_path);

				// Add the file.
				FileInfo& file = repo->GetOrAddFile(file_path, is_directory ? FileType::Directory : FileType::File, record->FileReferenceNumber);

				if (is_directory)
				{
					// If it's a directory, scan it to add all the files inside.
					scan_queue = { file.mID };

					while (!scan_queue.empty())
						repo->ScanDirectory(scan_queue, ioDirScanBuffer);
				}
				else
				{
					// If it's a file, treat it as if it was modified.
					if (gApp.mLogFSActivity >= LogLevel::Verbose)
						gApp.Log("Added {})", file);

					file.mLastChangeUSN  = record->Usn;
					file.mLastChangeTime = record->TimeStamp.QuadPart;

					gCookingSystem.QueueUpdateDirtyStates(file.mID);
				}
			}
		}
		else
		{
			// The file was just modified, update its USN.
			FileInfo* file = gFileSystem.FindFile(record->FileReferenceNumber);
			if (file)
			{
				if (gApp.mLogFSActivity >= LogLevel::Verbose)
					gApp.Log("Modified {}", *file);

				file->mLastChangeUSN  = record->Usn;
				file->mLastChangeTime = record->TimeStamp.QuadPart;

				gCookingSystem.QueueUpdateDirtyStates(file->mID);
			}
		}
	}

	if (false && gApp.mLogFSActivity >= LogLevel::Verbose)
	{
		// Print how much of the buffer was used, to help sizing that buffer.
		gApp.Log("Used {} of {} buffer.", SizeInBytes(available_bytes), SizeInBytes(ioUSNBuffer.size()));
	}

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


FileID FileSystem::FindFileID(FileRefNumber inRefNumber) const
{
	std::lock_guard lock(mFilesMutex);

	auto it = mFilesByRefNumber.find(inRefNumber);
	if (it != mFilesByRefNumber.end())
		return it->second;

	return {};
}


FileInfo* FileSystem::FindFile(FileRefNumber inRefNumber)
{
	FileID file_id = FindFileID(inRefNumber);
	if (file_id.IsValid())
		return &GetFile(file_id);
	else
		return nullptr;
}



void FileSystem::AddRepo(StringView inName, StringView inRootPath)
{
	gAssert(!IsMonitoringStarted()); // Can't add repos once the threads have started, it's not thread safe!

	// Check that the name is unique.
	for (auto& repo : mRepos)
	{
		if (repo.mName == inName)
			gApp.FatalError("Failed to init FileRepo {} ({}) - There is already a repo with that name.", inName, inRootPath);
	}

	// Normalize the root path.
	String root_path(inRootPath);
	gNormalizePath(root_path);

	// Validate it.
	if (root_path.size() < 3 
		|| !gIsAlpha(root_path[0]) 
		|| !gStartsWith(root_path.substr(1), R"(:\)"))
	{
		gApp.FatalError("Failed to init FileRepo {} ({}) - Root Path should start with a drive letter (eg. D:/).", inName, inRootPath);
	}

	// Add a trailing slash if there isn't one.
	if (!gEndsWith(root_path, "\\"))
		root_path.append("\\");

	// Check if it overlaps with other repos.
	// TODO: test this
	for (auto& repo : mRepos)
	{
		if (gStartsWith(repo.mRootPath, root_path))
		{
			gApp.FatalError("Failed to init FileRepo {} ({}) - Root Path is inside another FileRepo ({} {}).", 
				inName, inRootPath, 
				repo.mName, repo.mRootPath);
		}

		if (gStartsWith(root_path, repo.mRootPath))
		{
			gApp.FatalError("Failed to init FileRepo {} ({}) - Another FileRepo is inside its root path ({} {}).", 
				inName, inRootPath, 
				repo.mName, repo.mRootPath);
		}
	}

	mRepos.emplace_back((uint32)mRepos.size(), inName, root_path, GetOrAddDrive(root_path[0]));
}


FileDrive& FileSystem::GetOrAddDrive(char inDriveLetter)
{
	for (FileDrive& drive : mDrives)
		if (drive.mLetter == inDriveLetter)
			return drive;

	return mDrives.emplace_back(inDriveLetter);
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



void FileSystem::InitialScan(std::stop_token inStopToken, std::span<uint8> ioBuffer)
{
	gApp.Log("Starting initial scan.");
	int64 ticks_start = gGetTickCount();

	std::vector<FileID> scan_queue;
	scan_queue.reserve(1024);

	// Check every repo.
	for (auto& repo : mRepos)
	{
		// Initialize the scan queue.
		scan_queue = { repo.mRootDirID };

		// Process the queue.
		do
		{
			repo.ScanDirectory(scan_queue, ioBuffer);

			if (inStopToken.stop_requested())
				break;

		} while (!scan_queue.empty());
	}

	size_t total_files = 0;
	{
		std::lock_guard lock(mFilesMutex);
		for (auto& repo : mRepos)
			total_files += repo.mFiles.size();
	}

	mInitialScanCompleted = true;

	gApp.Log("Initial scan complete ({} files in {:.2f} seconds).", 
		total_files, gTicksToSeconds(gGetTickCount() - ticks_start));
}


void FileSystem::KickMonitorDirectoryThread()
{
	mMonitorDirThreadSignal.release();
}


void FileSystem::MonitorDirectoryThread(std::stop_token inStopToken)
{
	gSetCurrentThreadName(L"Monitor Directory Thread");
	using namespace std::chrono_literals;

	// Allocate a working buffer for querying the USN journal and scanning directories.
	static constexpr size_t cBufferSize = 64 * 1024ull;
	uint8* buffer_ptr  = (uint8*)malloc(cBufferSize);
	defer { free(buffer_ptr); };

	// Split it in two, one for USN reads, one for directory reads.
	std::span buffer_USN  = { buffer_ptr,					cBufferSize / 2 };
	std::span buffer_scan = { buffer_ptr + cBufferSize / 2, cBufferSize / 2 };

	// Scan the repos.
	InitialScan(inStopToken, buffer_scan);

	gCookingSystem.ProcessUpdateDirtyStates();

	// Once the scan is finished, start cooking.
	gCookingSystem.StartCooking();

	while (!inStopToken.stop_requested())
	{
		bool any_work_done = false;

		// Check every drive.
		for (auto& drive : mDrives)
		{
			// Process the queue.
			while (drive.ProcessMonitorDirectory(buffer_USN, buffer_scan))
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
			// Wait for some time before checking the USN journals again (unless we're being signaled).
			std::ignore = mMonitorDirThreadSignal.try_acquire_for(1s);
		}
	}
}
