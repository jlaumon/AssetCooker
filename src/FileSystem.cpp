#include "FileSystem.h"
#include "App.h"
#include "Debug.h"
#include "Ticks.h"

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"
#include "win32/threads.h"

#include "xxHash/xxh3.h"

#include <format>
#include <optional>
#include <array>

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

// Convert wide char string to utf8. Always returns a null terminated string. Return an empty string on failure.
std::optional<StringView> gWideCharToUtf8(std::wstring_view inWString, MutStringView ioBuffer)
{
	// If a null terminator is included in the source, WideCharToMultiByte will also add it in the destination.
	// Otherwise we'll need to add it manually.
	bool source_is_null_terminated = (!inWString.empty() && inWString.back() == 0);

	int available_bytes = (int)ioBuffer.size();

	// If we need to add a null terminator, reserve 1 byte for it.
	if (source_is_null_terminated)
		available_bytes--;

	int written_bytes = WideCharToMultiByte(CP_UTF8, 0, inWString.data(), (int)inWString.size(), ioBuffer.data(), available_bytes, nullptr, nullptr);

	if (written_bytes == 0 && !inWString.empty())
		return std::nullopt; // Failed to convert.

	if (written_bytes == available_bytes)
		return std::nullopt; // Might be cropped, consider failed.

	// If there isn't a null terminator, add it.
	if (!source_is_null_terminated)
		ioBuffer[written_bytes + 1] = 0;
	else
		gAssert(ioBuffer[written_bytes] == 0); // Should already have a null terminator.

	return ioBuffer.subspan(0, written_bytes);
}

// Convert utf8 string to wide char. Always returns a null terminated string. Return an empty string on failure.
std::optional<std::wstring_view> gUtf8ToWideChar(StringView inString, std::span<wchar_t> ioBuffer)
{
	// If a null terminator is included in the source, WideCharToMultiByte will also add it in the destination.
	// Otherwise we'll need to add it manually.
	bool source_is_null_terminated = (!inString.empty() && inString.back() == 0);

	int available_bytes = (int)ioBuffer.size();

	// If we need to add a null terminator, reserve 1 byte for it.
	if (source_is_null_terminated)
		available_bytes--;

	int written_wchars = MultiByteToWideChar(CP_UTF8, 0, inString.data(), (int)inString.size(), ioBuffer.data(), available_bytes);

	if (written_wchars == 0 && !inString.empty())
		return std::nullopt; // Failed to convert.

	if (written_wchars == available_bytes)
		return std::nullopt; // Might be cropped, consider failed.

	// If there isn't a null terminator, add it.
	if (!source_is_null_terminated)
		ioBuffer[written_wchars + 1] = 0;
	else
		gAssert(ioBuffer[written_wchars] == 0); // Should already have a null terminator.

	return std::wstring_view{ ioBuffer.data(), (size_t)written_wchars };
}


// Hash the absolute path of a file in a case insensitive manner.
// That's used to get a unique identifier for the file even if the file itself doesn't exist.
// The hash is 128 bits, assume no collision.
// Clearly not the most efficient implementation, but good enough for now.
Hash128 gHashPath(StringView inRootPath, StringView inPath)
{
	gAssert(gIsNormalized(inPath));

	// Build the full path.
	PathBufferUTF8  abs_path_buffer;
	StringView      abs_path = gConcat(abs_path_buffer, inRootPath, inPath);

	// Convert it to wide char.
	PathBufferUTF16 wpath_buffer;
	std::optional wpath = gUtf8ToWideChar(abs_path, wpath_buffer);
	if (!wpath)
		gApp.FatalError(std::format("Failed to convert path {} to WideChar", inPath));

	// Convert it to uppercase.
	PathBufferUTF16 uppercase_buffer;
	int uppercase_size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wpath->data(), (int)wpath->size(), uppercase_buffer.data(), uppercase_buffer.size() / 2, nullptr, nullptr, 0);
	if (uppercase_size == 0)
		gApp.FatalError(std::format("Failed to convert path {} to uppercase", inPath));

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


FileRefNumber::FileRefNumber(const _FILE_ID_128& inFileID128)
{
	*this = inFileID128;
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
		return (uint16)offset;
	else
		return (uint16)inPath.size(); // No extension.
}


FileInfo::FileInfo(FileID inID, StringView inPath, Hash128 inPathHash, FileType inType, FileRefNumber inRefNumber)
	: mID(inID)
	, mIsDirectory(inType == FileType::Directory)
	, mNamePos(sFindNamePos(inPath))
	, mExtensionPos(sFindExtensionPos(mNamePos, inPath))
	, mPath(inPath)
	, mPathHash(inPathHash)
	, mRefNumber(inRefNumber)
{

}

	 
FileRepo::FileRepo(uint32 inIndex, StringView inShortName, StringView inRootPath, FileDrive& inDrive)
	: mDrive(inDrive)
{
	// Store the index, short name and root path.
	mIndex     = inIndex;
	mShortName = mStringPool.AllocateCopy(inShortName);
	mRootPath  = mStringPool.AllocateCopy(inRootPath);

	// Add this repo to the repo list in the drive.
	mDrive.mRepos.push_back(this);

	// Get a handle to the root path.
	// TODO: do we want to keep that handle to make sure the root isn't deleted?
	OwnedHandle root_dir_handle = CreateFileA(mRootPath.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!root_dir_handle.IsValid())
		gApp.FatalError(std::format("Failed to get handle to {} - ", mRootPath) + GetLastErrorString());

	// Get the FileReferenceNumber of the root dir.
	FILE_ID_INFO file_info;
	if (!GetFileInformationByHandleEx(root_dir_handle, FileIdInfo, &file_info, sizeof(file_info)))
		gApp.FatalError(std::format("Failed to get FileReferenceNumber for {} - ", mRootPath) + GetLastErrorString());

	// The root directory file info has an empty path (relative to mRootPath).
	FileInfo& root_dir = GetOrAddFile("", FileType::Directory, file_info.FileId);
	mRootDirID = root_dir.mID;

	gApp.Log(std::format("Initialized FileRepo {} as {}:", mRootPath, mShortName));
}



FileInfo& FileRepo::GetOrAddFile(StringView inPath, FileType inType, FileRefNumber inRefNumber)
{
	// Calculate the case insensitive path hash that will be used to identify the file.
	Hash128 path_hash = gHashPath(mRootPath, inPath);

	// TODO: not great to access these internals, maybe find a better way?
	std::lock_guard lock(gFileSystem.mFilesMutex);

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
						gApp.LogError(std::format("{} chandged ref number unexpectedly (missed event?)", file));

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
	{
		auto [it, inserted] = gFileSystem.mFilesByRefNumber.insert({ inRefNumber, actual_file_id });
		if (!inserted)
		{
			FileID previous_file_id = it->second;

			// Check if the existing file is the same (ie. same path).
			// The file could have been renamed but kept the same ref number (and we've missed that rename event?).
			if (previous_file_id != actual_file_id || GetFile(previous_file_id).mPathHash != path_hash)
			{
				// Mark the old file as deleted, and add the new one instead.
				MarkFileDeleted(GetFile(previous_file_id));
			}
		}
	}

	if (actual_file_id == new_file_id)
	{
		// The file wasn't already known, add it to the list.
		return mFiles.emplace_back(new_file_id, mStringPool.AllocateCopy(inPath), path_hash, inType, inRefNumber);
	}
	else
	{
		// The file was known, return it.
		FileInfo& file = GetFile(actual_file_id);

		if (file.GetType() != inType)
		{
			// TODO we could support changing the file type if we make sure to update any list of all directories
			gApp.FatalError("A file was turned into a directory (or vice versa). This is not supported yet.");
		}

		return file;
	}
}

void FileRepo::MarkFileDeleted(FileInfo& inFile)
{
	// TODO: not great to access these internals, maybe find a better way?
	std::lock_guard lock(gFileSystem.mFilesMutex);

	gFileSystem.mFilesByRefNumber.erase(inFile.mRefNumber);
	inFile.mRefNumber = FileRefNumber::cInvalid();
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
			gApp.LogError(std::format("Failed to open {} - {}", dir, GetLastErrorString()));
		return;
	}

	if (gApp.mLogFSActivity >= LogLevel::Normal)
		gApp.Log(std::format("Added {}", dir));


	// First GetFileInformationByHandleEx call needs a different value to make it "restart".
	FILE_INFO_BY_HANDLE_CLASS file_info_class = FileIdExtdDirectoryRestartInfo;

	while (true)
	{
		// Fill the scan buffer with content to iterate.
		if (!GetFileInformationByHandleEx(dir_handle, file_info_class, ioBuffer.data(), ioBuffer.size()))
		{
			if (GetLastError() == ERROR_NO_MORE_FILES)
				break; // Finished iterating, exit the loop.

			gApp.FatalError(std::format("Enumerating {} failed - ", dir) + GetLastErrorString());
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
				gApp.LogError(std::format("Failed to build the path of a file in {}", dir));
				gAssert(false); // Investigate why that would happen.
				continue;
			}

			// Check if it's a directory.
			const bool is_directory = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			// Add (or get) the file info.
			FileInfo& file = GetOrAddFile(*path, is_directory ? FileType::Directory : FileType::File, entry->FileId);

			if (gApp.mLogFSActivity >= LogLevel::Verbose)
				gApp.Log(std::format("Added {}", file));

			// TODO: read more attributes?

			// Update the USN.
			// TODO: this is by far the slowest part, find another way?
			{
				OwnedHandle file_handle = mDrive.OpenFileByRefNumber(file.mRefNumber);
				if (!dir_handle.IsValid())
				{
					// TODO: depending on error, we should probably re-queue for scan
					if (gApp.mLogFSActivity>= LogLevel::Normal)
						gApp.LogError(std::format("Failed to open {} - {}", file, GetLastErrorString()));
				}

				file.mUSN = mDrive.GetUSN(file_handle);
			}
			
			if (file.IsDirectory())
				ioScanQueue.push_back(file.mID);

		} while (!last_entry);

		if (false && gApp.mLogFSActivity >= LogLevel::Verbose)
		{
			// Print how much of the buffer was used, to help sizing that buffer.
			// Seems most folders need <1 KiB but saw one that used 30 KiB.
			uint8* buffer_end = (uint8*)entry->FileName + entry->FileNameLength;
			gApp.Log(std::format("Used {} of {} buffer.", SizeInBytes(buffer_end - ioBuffer.data()), SizeInBytes(ioBuffer.size())));
		}
	}
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

	USN_RECORD_V3* record = (USN_RECORD_V3*)buffer.data();
	return record->Usn;	
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
		gApp.FatalError(std::format("Failed to read USN journal for {}:\\ - ", mLetter) + GetLastErrorString());
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
			FileInfo* file = gFileSystem.FindFile(record->FileReferenceNumber);
			if (file)
			{
				FileRepo& repo = gFileSystem.GetRepo(file->mID);

				repo.MarkFileDeleted(*file);

				if (gApp.mLogFSActivity >= LogLevel::Verbose)
					gApp.Log(std::format("Deleted {}", *file));

				// If it's a directory, also mark all the file inside as deleted.
				if (file->IsDirectory())
				{
					PathBufferUTF8 dir_path_buffer;
					StringView     dir_path = gConcat(dir_path_buffer, file->mPath, "\\");

					for (FileInfo& file : repo.mFiles)
					{
						if (gStartsWith(file.mPath, dir_path))
						{
							repo.MarkFileDeleted(file);

							if (gApp.mLogFSActivity >= LogLevel::Verbose)
								gApp.Log(std::format("Deleted {}", file));
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
				gApp.LogError(std::format("Failed to open newly created file {} - {}", FileRefNumber(record->FileReferenceNumber), GetLastErrorString()));
				continue;
			}

			// Get its path.
			PathBufferUTF8 buffer;
			std::optional full_path = GetFullPath(file_handle, buffer);
			if (!full_path)
			{
				// TODO: same remark as failing to open
				gApp.LogError(std::format("Failed to get path for newly created file {} - {}", FileRefNumber(record->FileReferenceNumber), GetLastErrorString()));
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

				// If it's a directory, scan it to add all the files inside.
				if (is_directory)
				{
					scan_queue = { file.mID };

					while (!scan_queue.empty())
						repo->ScanDirectory(scan_queue, ioDirScanBuffer);
				}
				else
				{
					if (gApp.mLogFSActivity >= LogLevel::Verbose)
						gApp.Log(std::format("Added {})", file));
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
					gApp.Log(std::format("Modified {}", *file));

				file->mUSN = record->Usn;
			}
		}
	}

	if (false && gApp.mLogFSActivity >= LogLevel::Verbose)
	{
		// Print how much of the buffer was used, to help sizing that buffer.
		gApp.Log(std::format("Used {} of {} buffer.", SizeInBytes(available_bytes), SizeInBytes(ioUSNBuffer.size())));
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



void FileSystem::AddRepo(StringView inShortName, StringView inRootPath)
{
	gAssert(!mMonitorDirThread.joinable()); // Can't add repos once the threads have started, it's not thread safe!

	// Normalize the root path.
	String root_path(inRootPath);
	gNormalizePath(root_path);

	// Validate it.
	{
		if (root_path.size() < 3 
			|| !gIsAlpha(root_path[0]) 
			|| !gStartsWith(root_path.substr(1), R"(:\)"))
		{
			gApp.FatalError(std::format("Failed to init FileRepo {} ({}) - Root Path should start with a drive letter (eg. D:/).", inShortName, inRootPath));
		}

		// Add a trailing slash if there isn't one.
		if (!gEndsWith(root_path, "\\"))
			root_path.append("\\");
	}

	// Check if it overlaps with other repos.
	// TODO: test this
	for (auto& repo : mRepos)
	{
		if (gStartsWith(repo.mRootPath, root_path))
		{
			gApp.FatalError(std::format("Failed to init FileRepo {} ({}) - Root Path is inside another FileRepo ({} {}).", 
				inShortName, inRootPath, 
				repo.mShortName, repo.mRootPath));
		}

		if (gStartsWith(root_path, repo.mRootPath))
		{
			gApp.FatalError(std::format("Failed to init FileRepo {} ({}) - Another FileRepo is inside its root path ({} {}).", 
				inShortName, inRootPath, 
				repo.mShortName, repo.mRootPath));
		}
	}

	//String root_path_no_drive = root_path.substr(3);

	mRepos.emplace_back((uint32)mRepos.size(), inShortName, root_path, GetOrAddDrive(root_path[0]));
}

FileDrive& FileSystem::GetOrAddDrive(char inDriveLetter)
{
	for (FileDrive& drive : mDrives)
		if (drive.mLetter == inDriveLetter)
			return drive;

	return mDrives.emplace_back(inDriveLetter);
}


FileDrive::FileDrive(char inDriveLetter)
{
	// Store the drive letter;
	mLetter = inDriveLetter;

	// Get a handle to the drive.
	// Note: Only request FILE_TRAVERSE to make that work without admin rights.
	mHandle = CreateFileA(std::format(R"(\\.\{}:)", mLetter).c_str(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!mHandle.IsValid())
		gApp.FatalError(std::format(R"(Failed to get handle to {}:\ - )", mLetter) + GetLastErrorString());

	// Query the USN journal to get its ID.
	USN_JOURNAL_DATA_V0 journal_data;
	uint32				unused;
	if (!DeviceIoControl(mHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &unused, nullptr))
		gApp.FatalError(std::format(R"(Failed to query USN journal for {}:\ - )", mLetter) + GetLastErrorString());

	// Store the jorunal ID.
	mUSNJournalID = journal_data.UsnJournalID;

	// Store the current USN.
	// TODO: we should read that from saved stated instead.
	mNextUSN = journal_data.NextUsn;

	gApp.Log(std::format(R"(Queried USN journal for {}:\. ID: 0x{:08X}. Max size: {})", mLetter, mUSNJournalID, SizeInBytes(journal_data.MaximumSize)));
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

	gApp.Log(std::format("Initial scan complete ({} files in {:.2f} seconds).", 
		total_files, gTicksToSeconds(gGetTickCount() - ticks_start)));
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

		if (!any_work_done)
		{
			// Wait for some time before checking the USN journals again (unless we're being signaled).
			std::ignore = mMonitorDirThreadSignal.try_acquire_for(1s);
		}
	}
}