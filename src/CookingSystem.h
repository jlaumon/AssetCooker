#pragma once

#include "Core.h"
#include "Strings.h"
#include "FileSystem.h"
#include "CookingSystemIDs.h"

#include <vector>

enum class CommandVariables : uint8
{
	Ext,
	File,
	Dir,
	FullPath,
	Repo,
	_Count
};

constexpr StringView gToStringView(CommandVariables inVar)
{
	constexpr StringView cNames[]
	{
		"Ext",
		"File",
		"Dir",
		"FullPath",
		"Repo",
	};
	static_assert(gElemCount(cNames) == (size_t)CommandVariables::_Count);

	return cNames[(int)inVar];
};

// Check for CommandVariables and replace them by the corresponding part of inFile.
// Eg. "copy.exe {Repo:Source}{FullPath} {Repo:Bin}" will turn into "copy.exe D:/src/file.txt D:/bin/"
std::optional<String> gFormatCommandString(StringView inFormatStr, const FileInfo& inFile);

struct RepoAndFilePath
{
	FileRepo& mRepo;
	String    mPath;
};
// Check for CommandVariables and replace them by the corresponding part of inFile but expects the string to be a single file path.
// One Repo var is needed at the start and of the path and the corresponding FileRepo will be returned instead of be replaced by its path.
std::optional<RepoAndFilePath> gFormatFilePath(StringView inFormatStr, const FileInfo& inFile);


// Format the file path and get (or add) the corresponding file. Return an invalid FileID if the format is invalid.
FileID                         gGetOrAddFileFromFormat(StringView inFormatStr, const FileInfo& inFile);


struct InputFilter
{
	uint32     mRepoIndex = FileID::cInvalid().mRepoIndex;
	StringView mExtension;
	StringView mDirectoryPrefix;
	StringView mNamePrefix;
	StringView mNameSuffix;

	bool       Pass(const FileInfo& inFile) const;
};



struct CookingRule : NoCopy
{
	CookingRule(CookingRuleID inID) : mID(inID) {}

	const CookingRuleID      mID;
	StringView               mName;
	std::vector<InputFilter> mInputFilters;
	StringView               mCommandLine;
	int                      mBuildOrder     = 0;
	uint16                   mVersion        = 0;
	bool                     mMatchMoreRules = false; // If false, we'll stop matching rules once an input file is matched with this rule. If true, we'll keep looking.
	bool                     mUseDepFile     = false;
	StringView               mDepFilePath;
	std::vector<StringView>  mInputPaths;
	std::vector<StringView>  mOutputPaths;
};



// Instance of a rule for a specific input file.
struct CookingCommand : NoCopy
{
	CookingCommandID       mID;
	CookingRuleID          mRuleID;
	FileID                 mDepFile;

	std::vector<FileID>    mInputs;
	std::vector<FileID>    mOutputs;
};


struct CookingSystem : NoCopy
{
	const CookingRule&                    GetRule(CookingRuleID inID) const { return mRules[inID.mIndex]; }
	const CookingCommand&                 GetCommand(CookingCommandID inID) const { return mCommands[inID.mIndex]; }

	CookingRule&                          AddRule() { return mRules.emplace_back(CookingRuleID{ (int16)mRules.size() }); }
	void                                  CreateCommandsForFile(FileInfo& ioFile);

	bool                                  ValidateRules(); // Return false if problems were found (see log).

	SegmentedVector<CookingRule, 256>     mRules;

	SegmentedVector<CookingCommand, 4096> mCommands;
	std::mutex                            mCommandsMutex;
};


inline CookingSystem gCookingSystem;