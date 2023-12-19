#include "FileSystem.h"
#include "App.h"
#include "Debug.h"

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"

#include <format>

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

// Convert wide char string to utf8. Return an empty string on failure.
MutStringView gWideCharToUtf8(std::wstring_view inWString, MutStringView ioBuffer)
{
	int written_bytes = WideCharToMultiByte(CP_UTF8, 0, inWString.data(), (int)inWString.size(), ioBuffer.data(), (int)ioBuffer.size(), nullptr, nullptr);

	if (written_bytes == 0)
		return {}; // Failed to convert.

	if (written_bytes == (int)ioBuffer.size())
		return {}; // Might be cropped, consider failed.

	// Don't count the null terminator in the result string view (if there's one).
	if (ioBuffer[written_bytes] == 0)
		written_bytes--;

	return ioBuffer.subspan(0, written_bytes);
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

	//USN_JOURNAL_DATA_V0 journal_data;
	//uint32				available_bytes;
	//if (!DeviceIoControl(mDriveHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &available_bytes, nullptr))
	//	gApp.FatalError(std::format(R"(Failed to get USN journal for {}:\ - )", drive_letter) + GetLastErrorString());
	//gApp.Log(std::format(R"(Opened USN journal for {}:\. Max size: {})", drive_letter, gFormatSizeInBytes(journal_data.MaximumSize)));

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
	QueueScanDirectory(root_dir.mID);
}


FileInfo& FileRepo::AddFile(FileRefNumber inRefNumber, StringView inPath, bool inIsDirectory)
{
	std::lock_guard lock(mFilesMutex);

	// Check if we already know this file.
	auto [it, inserted] = mFilesByRefNumber.insert({ inRefNumber, {} });
	if (inserted)
	{
		FileID    new_file_id = { mIndex, (uint32)mFiles.size() };
		FileInfo& new_file    = mFiles.emplace_back(new_file_id, mStringPool.AllocateCopy(inPath), inRefNumber, inIsDirectory);

		it->second = &new_file;
	}

	return *it->second;
}

void FileRepo::ScanFile(FileID inFileID)
{

}

void FileRepo::ScanDirectory(FileID inFileID)
{
	FileInfo& dir = GetFile(inFileID);
	gAssert(dir.IsDirectory());

	if (gApp.mLogScanActivity)
		gApp.Log(std::format("Scanning directory {}\\{}", mRootPath, dir.mPath));

	OwnedHandle dir_handle = OpenFileByRefNumber(dir.mRefNumber);

	if (!dir_handle.IsValid())
	{
		// TODO: depending on error, we should probably re-queue for scan
		if (gApp.mLogScanActivity)
			gApp.LogError(std::format("Failed to open directory {}\\{}", mRootPath, dir.mPath));
		return;
	}


	if (mScanDirBuffer == nullptr)
		mScanDirBuffer = std::make_unique<uint8[]>(cScanDirBufferSize);

	// Prepare a buffer for the paths. It'll grow more if necessary.
	String path; path.reserve(512);

	// First GetFileInformationByHandleEx call needs a different value to make it "restart".
	FILE_INFO_BY_HANDLE_CLASS file_info_class = FileIdExtdDirectoryRestartInfo;

	while (true)
	{
		// Fill the scan buffer with content to iterate.
		if (!GetFileInformationByHandleEx(dir_handle, file_info_class, mScanDirBuffer.get(), cScanDirBufferSize))
		{
			if (GetLastError() == ERROR_NO_MORE_FILES)
				break; // Finished iterating, exit the loop.

			gApp.FatalError(std::format("Enumerating directory {}\\{} failed - ", mRootPath, dir.mPath) + GetLastErrorString());
		}

		// Next time keep iterating instead of restarting.
		file_info_class = FileIdExtdDirectoryInfo;

		// Iterate on entries in the buffer.
		PFILE_ID_EXTD_DIR_INFO entry = (PFILE_ID_EXTD_DIR_INFO)mScanDirBuffer.get();
		do
		{
			// Defer iterating to the next entry so that we can use continue.
			defer { entry = (PFILE_ID_EXTD_DIR_INFO)((uint8*)entry + entry->NextEntryOffset); };

			// Convert the filename to utf-8.
			// TODO: test with a tiny buffer to make it fail.
			char file_name_buffer[1024];
			// Note: FileNameLength is in bytes so need to divide by 2 to get the number of wchar (WTF Microsoft).
			StringView file_name = gWideCharToUtf8({ entry->FileName, entry->FileNameLength / 2 }, file_name_buffer);

			// If it fails, ignore the file.
			if (file_name.empty())
			{
				gApp.LogError(std::format("Failed to convert a file name to utf-8 in directory {}\\{}", mRootPath, dir.mPath));
				gAssert(false); // Investigate why that would happen.
				continue;
			}

			// Ignore current/parent dir.
			if (file_name == "." || file_name == "..")
				continue;

			// Build the path of the file.
			// If there's a parent dir path (ie. it's not the root dir), concatenate it.
			if (!dir.mPath.empty())
			{
				path = dir.mPath;
				path += '\\';
				path += file_name;
			}
			else
			{
				path = file_name;
			}

			// Check if it's a directly.
			const bool is_directory = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			// Add (or get) the file info.
			FileInfo& file = AddFile(FileRefNumber(entry->FileId), path, is_directory);

			if (gApp.mLogScanActivity)
				gApp.Log(std::format("\tFound {}{}", file.GetName(), file.IsDirectory() ? " (dir)" : ""));

			// TODO: read more attributes

			if (is_directory)
				QueueScanDirectory(file.mID);

		} while (entry->NextEntryOffset != 0);
	}
}

void FileRepo::QueueScanDirectory(FileID inFileID)
{
	{
		std::lock_guard lock(mScanDirQueueMutex);
		mScanDirQueue.push_back(inFileID);
	}

	// Make sure the scanning thread is awake.
	gFileSystem.KickScanDirectoryThread();
}


bool FileRepo::ProcessScanDirectoryQueue()
{
	// How many directory to process in one go.
	constexpr int cDirectoryScanPerCall = 10;

	for (int i = 0; i < cDirectoryScanPerCall; ++i)
	{
		FileID dir_id;

		{
			std::lock_guard lock(mScanDirQueueMutex);

			if (mScanDirQueue.empty())
				return false;

			dir_id = mScanDirQueue.back();
			mScanDirQueue.pop_back();
		}

		ScanDirectory(dir_id);
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


void FileSystem::StartMonitoring()
{
	// Start the directory scan thread.
	mScanDirThread = std::jthread(std::bind_front(&FileSystem::ScanDirectoryThread, this));
}

void FileSystem::StopMonitoring()
{
	mScanDirThread.request_stop();
	KickScanDirectoryThread();
	mScanDirThread.join();
}


void FileSystem::AddRepo(StringView inShortName, StringView inRootPath)
{
	gAssert(!mScanDirThread.joinable()); // Can't add repos once the threads are started, it's not thread safe!

	mRepos.emplace_back((uint32)mRepos.size(), inShortName, inRootPath);
}


void FileSystem::KickScanDirectoryThread()
{
	mScanDirThreadSignal.release();
}


void FileSystem::ScanDirectoryThread(std::stop_token inStopToken)
{
	while (!inStopToken.stop_requested())
	{
		// Wait until we know there's work to do.
		mScanDirThreadSignal.acquire();

		// Check every repo.
		for (auto& repo : mRepos)
		{
			// Process the queue.
			while (repo.ProcessScanDirectoryQueue())
			{
				if (inStopToken.stop_requested())
					break;
			}

			if (inStopToken.stop_requested())
				break;
		}
	}
}