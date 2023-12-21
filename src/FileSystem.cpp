#include "FileSystem.h"
#include "App.h"
#include "Debug.h"

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"
#include "win32/threads.h"

#include <format>
#include <optional>
#include <array>

// These are not exactly the max path length allowed by Windows in all cases, but should be good enough.
constexpr size_t cMaxPathSizeInBytesUTF16 = 32768 * sizeof(wchar_t);
constexpr size_t cMaxPathSizeInBytesUTF8  = 32768 * 3ull;	// UTF8 can use up to 6 bytes per character, but let's suppose 3 is good enough on average.

using PathBufferUTF16 = std::array<char, cMaxPathSizeInBytesUTF16>;
using PathBufferUTF8  = std::array<char, cMaxPathSizeInBytesUTF8>;

constexpr size_t operator ""_B(size_t inValue)	 { return inValue; }
constexpr size_t operator ""_KiB(size_t inValue) { return inValue * 1024; }
constexpr size_t operator ""_MiB(size_t inValue) { return inValue * 1024 * 1024; }
constexpr size_t operator ""_GiB(size_t inValue) { return inValue * 1024 * 1024 * 1024; }

constexpr String gFormatSizeInBytes(size_t inBytes)
{
	if (inBytes < 10_KiB)
		return std::format("{} B", inBytes);
	else if (inBytes < 10_MiB)
		return std::format("{} KiB", inBytes / 1_KiB);
	else if (inBytes < 10_GiB)
		return std::format("{} MiB", inBytes / 1_MiB);
	else
		return std::format("{} GiB", inBytes / 1_GiB);
}


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


FileInfo::FileInfo(FileID inID, StringView inPath, FileRefNumber inRefNumber, bool inIsDirectory)
	: mID(inID)
	, mIsDirectory(inIsDirectory)
	, mNamePos(sFindNamePos(inPath))
	, mExtensionPos(sFindExtensionPos(mNamePos, inPath))
	, mRefNumber(inRefNumber)
	, mPath(inPath)
{

}


	 
FileRepo::FileRepo(uint32 inIndex, StringView inShortName, StringView inRootPath)
{
	// Store the index and short name.
	mIndex     = inIndex;
	mShortName = mStringPool.AllocateCopy(inShortName);

	// Check and store the root path.
	{
		StringView root_path = gNormalizePath(mStringPool.AllocateCopy(inRootPath));

		if (root_path.size() < 3 || !gIsAlpha(root_path[0]) || !gStartsWith(root_path.substr(1), R"(:\)"))
			gApp.FatalError(std::format("Failed to init FileRepo {} ({}) - Root Path should start with a drive letter.", mShortName, root_path));

		// Remove the trailing slash.
		if (gEndsWith(root_path, "\\"))
			root_path.remove_suffix(1);

		mRootPath = root_path;
	}

	char drive_letter = mRootPath[0];

	// Get a handle to the drive.
	// Note: Only request FILE_TRAVERSE to make that work without admin rights.
	mDriveHandle = CreateFileA(std::format(R"(\\.\{}:)", drive_letter).c_str(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!mDriveHandle.IsValid())
		gApp.FatalError(std::format(R"(Failed to get handle to {}:\ - )", drive_letter) + GetLastErrorString());

	// Query the USN journal to get its ID.
	USN_JOURNAL_DATA_V0 journal_data;
	uint32				unused;
	if (!DeviceIoControl(mDriveHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &unused, nullptr))
		gApp.FatalError(std::format(R"(Failed to query USN journal for {}:\ - )", drive_letter) + GetLastErrorString());

	// Store the jorunal ID.
	mUSNJournalID = journal_data.UsnJournalID;

	// Store the current USN.
	// TODO: we should read that from saved stated instead.
	mNextUSN = journal_data.NextUsn;

	gApp.Log(std::format(R"(Queried USN journal for {}:\. ID: 0x{:08X}. Max size: {})", drive_letter, mUSNJournalID, gFormatSizeInBytes(journal_data.MaximumSize)));

	// Get a handle to the root path.
	OwnedHandle root_dir_handle = CreateFileA(mRootPath.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!root_dir_handle.IsValid())
		gApp.FatalError(std::format("Failed to get handle to {}\\ - ", mRootPath) + GetLastErrorString());

	// Get the FileReferenceNumber of the root dir.
	FILE_ID_INFO file_info;
	if (!GetFileInformationByHandleEx(root_dir_handle, FileIdInfo, &file_info, sizeof(file_info)))
		gApp.FatalError(std::format("Failed to get FileReferenceNumber for {}\\ - ", mRootPath) + GetLastErrorString());

	// The root directory file info has an empty path (relative to mRootPath).
	FileInfo& root_dir = AddFile(FileRefNumber(file_info.FileId), "", true);

	gApp.Log(std::format("Initialized FileRepo {}\\", mRootPath));

	// Start scanning the repo.
	QueueScanDirectory(root_dir.mRefNumber);
}


FileInfo& FileRepo::AddFile(FileRefNumber inRefNumber, StringView inPath, bool inIsDirectory)
{
	gAssert(inPath.empty() || inPath[0] != '\\'); // Paths should not start by a slash.

	std::lock_guard lock(mFilesMutex);

	// Prepare a new FileID in case this file wasn't already added.
	FileID new_file_id = { mIndex, (uint32)mFiles.size() };

	// Try to insert it into the hashmap.
	auto [it, inserted] = mFilesByRefNumber.insert({ inRefNumber, new_file_id });
	if (inserted)
		// The file wasn't already known, add it to the list.
		return mFiles.emplace_back(new_file_id, mStringPool.AllocateCopy(inPath), inRefNumber, inIsDirectory);
	else
		// The file was known, return it.
		return GetFile(it->second);
}

FileID FileRepo::FindFile(FileRefNumber inRefNumber) const
{
	std::lock_guard lock(mFilesMutex);

	auto it = mFilesByRefNumber.find(inRefNumber);
	if (it != mFilesByRefNumber.end())
		return it->second;

	return {};
}

// Return the path if it's an already known file.
std::optional<StringView> FileRepo::FindPath(FileRefNumber inRefNumber) const
{
	std::lock_guard lock(mFilesMutex);

	auto it = mFilesByRefNumber.find(inRefNumber);
	if (it != mFilesByRefNumber.end())
		return GetFile(it->second).mPath;

	return {};
}


static std::optional<StringView> sBuildFilePath(StringView inParentDirPath, std::wstring_view inFileNameW, MutStringView ioBuffer)
{
	MutStringView file_path = ioBuffer;

	// Add the parent dir if there's one (can be empty for the root dir).
	if (!inParentDirPath.empty())
		ioBuffer = gAppend(ioBuffer, inParentDirPath, "\\");

	std::optional file_name = gWideCharToUtf8(inFileNameW, ioBuffer);
	if (!file_name)
	{
		// Failed for some reason. Buffer too small?
		gAssert(false); // Investigate.

		return std::nullopt;
	}

	return StringView{ file_path.data(), gEndPtr(*file_name) };
}


void FileRepo::ScanDirectory(FileRefNumber inRefNumber, std::span<uint8> ioBuffer)
{
	OwnedHandle dir_handle = OpenFileByRefNumber(inRefNumber);
	if (!dir_handle.IsValid())
	{
		// TODO: depending on error, we should probably re-queue for scan
		if (gApp.mLogScanActivity >= LogLevel::Normal)
			gApp.LogError(std::format("Failed to open directory {} - {}", FindPath(inRefNumber).value_or(std::format("{}", inRefNumber)), GetLastErrorString()));
		return;
	}

	FileID dir_id = FindFile(inRefNumber);
	if (!dir_id.IsValid())
	{
		// PFILE_NAME_INFO contains the filename without the drive letter and column in front (ie. without the C:).
		PathBufferUTF16 wpath_buffer;
		PFILE_NAME_INFO file_name_info = (PFILE_NAME_INFO)wpath_buffer.data();
		if (!GetFileInformationByHandleEx(dir_handle, FileNameInfo, file_name_info, wpath_buffer.size()))
		{
			gApp.LogError(std::format("Failed to get directoy path for {} - {}", inRefNumber, GetLastErrorString()));
			return;
		}
		std::wstring_view wpath = { file_name_info->FileName, file_name_info->FileNameLength / 2 };

		PathBufferUTF8 path_buffer;
		std::optional opt_path = gWideCharToUtf8(wpath, path_buffer);
		if (!opt_path)
		{
			gApp.LogError(std::format("Failed to get directoy path for {} - {}", inRefNumber, GetLastErrorString()));
			return;
		}

		StringView path			= opt_path->substr(1);	// Skip initial slash.
		StringView root_path	= mRootPath.substr(3);	// Skip drive and slash (eg. "C:/").

		// Check if this file is in the rooth path.
		if (!path.starts_with(root_path))
			return; // Not in this repo, ignore.

		// Only keep the path after the root path and the following slash.
		path = path.substr(root_path.size() + 1);

		// Add the directory.
		dir_id = AddFile(inRefNumber, path, true).mID;
	}

	FileInfo& dir = GetFile(dir_id);
	gAssert(dir.IsDirectory());

	if (gApp.mLogScanActivity >= LogLevel::Normal)
		gApp.Log(std::format("Scanning directory {}\\{}", mRootPath, dir.mPath));


	// First GetFileInformationByHandleEx call needs a different value to make it "restart".
	FILE_INFO_BY_HANDLE_CLASS file_info_class = FileIdExtdDirectoryRestartInfo;

	while (true)
	{
		// Fill the scan buffer with content to iterate.
		if (!GetFileInformationByHandleEx(dir_handle, file_info_class, ioBuffer.data(), ioBuffer.size()))
		{
			if (GetLastError() == ERROR_NO_MORE_FILES)
				break; // Finished iterating, exit the loop.

			gApp.FatalError(std::format("Enumerating directory {}\\{} failed - ", mRootPath, dir.mPath) + GetLastErrorString());
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
				gApp.LogError(std::format("Failed to build the path of a file in directory {}\\{}", mRootPath, dir.mPath));
				gAssert(false); // Investigate why that would happen.
				continue;
			}

			// Check if it's a directory.
			const bool is_directory = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			// Add (or get) the file info.
			FileInfo& file = AddFile(FileRefNumber(entry->FileId), *path, is_directory);

			if (gApp.mLogScanActivity >= LogLevel::Verbose)
				gApp.Log(std::format("\tFound {}{}", file.GetName(), file.IsDirectory() ? " (dir)" : ""));

			// TODO: read more attributes

			if (is_directory)
				QueueScanDirectory(file.mRefNumber);

		} while (!last_entry);

		if (false && gApp.mLogScanActivity >= LogLevel::Verbose)
		{
			// Print how much of the buffer was used, to help sizing that buffer.
			// Seems most folders need <1 KiB but saw one that used 30 KiB.
			uint8* buffer_end = (uint8*)entry->FileName + entry->FileNameLength;
			gApp.Log(std::format("Used {} of {} buffer.", gFormatSizeInBytes(buffer_end - ioBuffer.data()), gFormatSizeInBytes(ioBuffer.size())));
		}
	}
}


void FileRepo::QueueScanDirectory(FileRefNumber inRefNumber)
{
	{
		std::lock_guard lock(mScanDirQueueMutex);
		mScanDirQueue.insert(inRefNumber);
	}

	// Tell the scanning thread there's work to do.
	gFileSystem.KickScanDirectoryThread();
}


bool FileRepo::IsInScanDirectoryQueue(FileRefNumber inRefNumber) const
{
	std::lock_guard lock(mScanDirQueueMutex);
	return mScanDirQueue.contains(inRefNumber);
}


bool FileRepo::ProcessScanDirectoryQueue(std::span<uint8> ioBuffer)
{
	// How many directory to process in one go.
	constexpr int cDirectoryScanPerCall = 10;

	for (int i = 0; i < cDirectoryScanPerCall; ++i)
	{
		FileRefNumber dir_ref_number;

		{
			std::lock_guard lock(mScanDirQueueMutex);

			if (mScanDirQueue.empty())
				return false;

			auto it = --mScanDirQueue.end();
			dir_ref_number = *it;

			mScanDirQueue.erase(it);
		}

		ScanDirectory(dir_ref_number, ioBuffer);
	}

	return true;
}


OwnedHandle FileRepo::OpenFileByRefNumber(FileRefNumber inRefNumber) const
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
	OwnedHandle handle = OpenFileById(mDriveHandle, &file_id_descriptor, FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, flags_and_attributes);

	return handle;
}


// TODO: this is monitoring an entire drive, needs to be moved outisde of repo since several repos can be on the same drive 
bool FileRepo::ProcessMonitorDirectory(std::span<uint8> ioBuffer)
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
	if (!DeviceIoControl(mDriveHandle, FSCTL_READ_UNPRIVILEGED_USN_JOURNAL, &journal_data, sizeof(journal_data), ioBuffer.data(), (uint32)ioBuffer.size(), &available_bytes, nullptr))
	{
		// TODO: test this but probably the only thing to do is to restart and re-scan everything (maybe the journal was deleted?)
		gApp.FatalError(std::format("Failed to read USN journal for {}:\\ - ", mRootPath[0]) + GetLastErrorString());
	}

	std::span<uint8> available_buffer = ioBuffer.subspan(0, available_bytes);

	USN next_usn = *(USN*)available_buffer.data();
	available_buffer = available_buffer.subspan(sizeof(USN));

	if (next_usn == mNextUSN)
	{
		// Nothing happened.
		return false;
	}

	// Update the USN for next time.
	mNextUSN = next_usn;

	while (!available_buffer.empty())
	{
		const USN_RECORD_V3* record = (USN_RECORD_V3*)available_buffer.data();

		// Defer iterating to the next record so that we can use continue.
		defer { available_buffer = available_buffer.subspan(record->RecordLength); };

		// We get all events where USN_REASON_CLOSE is present, but we don't care about all of them.
		if ((record->Reason & cInterestingReasons) == 0)
			continue;

		if (record->Reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME))
		{
			// If the file is in the repo, mark it as deleted.
			FileID file_id = FindFile(record->FileReferenceNumber);
			if (file_id.IsValid())
			{
				FileInfo& file = GetFile(file_id);
				file.mExists   = false;

				// If it's a directory, also mark all the file inside as deleted.
				if (file.IsDirectory())
				{
					// TODO
				}
			}

		}

		if (record->Reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME))
		{
			// Add the file.

			// If it's a directory, scan it to add all the files inside.

		}
		else
		{
			// The file was just modified, update the file USN.
			FileID file_id = FindFile(record->FileReferenceNumber);
			if (file_id.IsValid())
			{
				FileInfo& file = GetFile(file_id);
				file.mUSN = record->Usn;
			}
		}

		if (gApp.mLogDiskActivity >= LogLevel::Normal)
		{
			FileID file_id = FindFile(record->FileReferenceNumber);
			if (file_id.IsValid())
			{
				gApp.Log(std::format(R"(File {}. Reason: {})", GetFile(file_id).mPath, gUSNReasonToString(record->Reason)));
			}
		}
	}

	if (false && gApp.mLogDiskActivity >= LogLevel::Verbose)
	{
		// Print how much of the buffer was used, to help sizing that buffer.
		gApp.Log(std::format("Used {} of {} buffer.", gFormatSizeInBytes(available_bytes), gFormatSizeInBytes(ioBuffer.size())));
	}

	return true;
}


void FileSystem::StartMonitoring()
{
	// Start the directory scan thread.
	mScanDirThread = std::jthread(std::bind_front(&FileSystem::ScanDirectoryThread, this));

	// Start the directory monitor thread.
	mMonitorDirThread = std::jthread(std::bind_front(&FileSystem::MonitorDirectoryThread, this));
}

void FileSystem::StopMonitoring()
{
	mScanDirThread.request_stop();
	KickScanDirectoryThread();
	mScanDirThread.join();
}


void FileSystem::AddRepo(StringView inShortName, StringView inRootPath)
{
	// TODO: add path validation/normalization here instead of in FileRepo
	// TODO: check if this rooth path is inside another repo, or contains another repo (not allowed!)

	gAssert(!mScanDirThread.joinable()); // Can't add repos once the threads are started, it's not thread safe!

	mRepos.emplace_back((uint32)mRepos.size(), inShortName, inRootPath);
}


void FileSystem::KickScanDirectoryThread()
{
	mScanDirThreadSignal.release();
}


void FileSystem::ScanDirectoryThread(std::stop_token inStopToken)
{
	gSetCurrentThreadName(L"Scan Directory Thread");

	// Allocate a working buffer for scanning.
	static constexpr size_t cBufferSize = 64 * 1024ull;
	uint8* buffer_ptr  = (uint8*)malloc(cBufferSize);
	defer { free(buffer_ptr); };

	std::span buffer = { buffer_ptr, cBufferSize };

	while (!inStopToken.stop_requested())
	{
		// Wait until we know there's work to do.
		mScanDirThreadSignal.acquire();

		if (mInitialScan)
		{
			gApp.Log("Starting initial scan.");
		}

		// Check every repo.
		for (auto& repo : mRepos)
		{
			// Process the queue.
			while (repo.ProcessScanDirectoryQueue(buffer))
			{
				if (inStopToken.stop_requested())
					break;
			}

			if (inStopToken.stop_requested())
				break;
		}

		if (mInitialScan)
		{
			gApp.Log("Initial scan complete.");
			mInitialScan = false;

			// Let the monitor thread know the inital scan is finished..
			KickMonitorDirectoryThread();
		}
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

	// Allocate a working buffer for querying the USN journal.
	static constexpr size_t cBufferSize = 64 * 1024ull;
	uint8* buffer_ptr  = (uint8*)malloc(cBufferSize);
	defer { free(buffer_ptr); };

	std::span buffer = { buffer_ptr, cBufferSize };

	// Wait for the initial scan to finish.
	while (mInitialScan)
		mMonitorDirThreadSignal.acquire();

	while (!inStopToken.stop_requested())
	{
		bool any_work_done = false;

		// Check every repo.
		for (auto& repo : mRepos)
		{
			// Process the queue.
			while (repo.ProcessMonitorDirectory(buffer))
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