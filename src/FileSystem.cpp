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


OwnedHandle::~OwnedHandle()
{
	if (mHandle != cInvalid)
		CloseHandle(mHandle);
}


void FileRepo::Init(StringView inRootPath)
{
	StringView root_path = gNormalizePath(mStringPool.AllocateCopy(inRootPath));

	if (root_path.size() < 3 || !gIsAlpha(root_path[0]) || !gStartsWith(root_path.substr(1), R"(:\)"))
		gApp.FatalError(std::format("Failed to init FileRepo {} - Root Path should start with a drive letter.", root_path));

	if (!gEndsWith(root_path, "\\"))
		gApp.FatalError(std::format("Failed to init FileRepo {} - Root Path should end with a slash.", root_path));

	mRootPath         = root_path;
	char drive_letter = mRootPath[0];

	// Get a handle to the drive.
	// Note: Only request FILE_TRAVERSE to make that work without admin rights.
	mDriveHandle = CreateFileA(std::format(R"(\\.\{}:)", drive_letter).c_str(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (mDriveHandle == INVALID_HANDLE_VALUE)
		gApp.FatalError(std::format(R"(Failed to get handle to {}:\ - )", drive_letter) + GetLastErrorString());

	USN_JOURNAL_DATA_V0 journal_data;
	uint32				available_bytes;
	if (!DeviceIoControl(mDriveHandle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &available_bytes, nullptr))
		gApp.FatalError(std::format(R"(Failed to get USN journal for {}:\ - )", drive_letter) + GetLastErrorString());

	gApp.mLog.Add(std::format(R"(Opened USN journal for {}:\. Max size: {})", drive_letter, gFormatSizeInBytes(journal_data.MaximumSize)));


	// Get a handle to the root path.
	mRootDirHandle = CreateFileA(mRootPath.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (mRootDirHandle == INVALID_HANDLE_VALUE)
		gApp.FatalError(std::format(R"(Failed to get handle to {} - )", mRootPath) + GetLastErrorString());

	// Get the FileReferenceNumber of the root dir.
	//FILE_ID_INFO file_info;
	//if (!GetFileInformationByHandleEx(mRootDirHandle, FileIdInfo, &file_info, sizeof(file_info)))
	//	gApp.FatalError(

}
