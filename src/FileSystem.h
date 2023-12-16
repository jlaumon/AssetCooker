#pragma once
#include "StringPool.h"

static constexpr int    cFileRepoIndexBits   = 6;
static constexpr int    cFileBucketIndexBits = 14;
static constexpr int    cFileIndexBits       = 12;

static constexpr size_t cMaxFileRepos        = 1ull << cFileRepoIndexBits;
static constexpr size_t cMaxFileBuckets      = 1ull << cFileBucketIndexBits;
static constexpr size_t cMaxFilePerBucket    = 1ull << cFileIndexBits;
static constexpr size_t cMaxFilePerRepo      = cMaxFileBuckets * cMaxFilePerBucket;


// Identifier for a file. 4 bytes.
struct FileID
{
	uint32 mRepoIndex	: cFileRepoIndexBits;
	uint32 mBucketIndex	: cFileBucketIndexBits;
	uint32 mFileIndex	: cFileIndexBits;
};


struct FileInfo
{
	std::string_view mPath;					// Path relative to the root directory.
	uint16           mNamePos      = 0;		// Points to the start of the file name (after the last '/').
	uint16           mExtensionPos = 0;		// Points to the first '.' in the file name.

	std::string_view GetFileName()	const	{ return mPath.substr(mNamePos); }
	std::string_view GetExtension() const	{ return mPath.substr(mExtensionPos); }
};


// TODO: get rid of this, use segmented_vector instead
struct FileBucket
{
	std::vector<FileInfo> mFiles;

	FileBucket()
	{
		mFiles.reserve(cMaxFilePerBucket);
	}
};


// Top level container for files.
struct FileRepo
{
	std::string_view        mRootPath;		// Absolute path to the repo. Ends with a '/'.
	std::vector<FileBucket> mBuckets;
	StringPool              mStringPool;

	void Init(std::string_view inRootPath);
	void ScanDirectory();
};