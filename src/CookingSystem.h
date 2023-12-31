#pragma once

#include "Core.h"
#include "Strings.h"
#include "FileSystem.h"

#include <vector>

enum class PatternVariables : uint8
{
	Ext,
	File,
	Dir,
	FullPath,
	Repo
};

struct InputPattern
{
	StringView mRepo;
	StringView mExtension;
	StringView mDirectoryPrefix;
	StringView mNamePrefix;
	StringView mNameSuffix;
};

struct CookingRuleID
{
	int16 mIndex = -1;

	bool  IsValid() const { return *this != CookingRuleID{}; }
	auto  operator<=>(const CookingRuleID& inOther) const = default;
};

struct CookingRule : NoCopy
{
	StringView                mName;
	std::vector<InputPattern> mInputPatterns;
	StringView                mCommandLine;
	int                       mBuildOrder     = 0;
	uint16                    mVersion		  = 0;
	bool                      mMatchMoreRules = false;	// If false, we'll stop matching rules once an input file is matched with this rule. If true, we'll keep looking.
	bool                      mUseDepFile     = false;
	StringView                mDepFilePath;
	std::vector<StringView>   mOutputPaths;
};


struct BuildCommandID
{
	uint32 mIndex = (uint32)-1;

	bool  IsValid() const { return *this != BuildCommandID{}; }
	auto  operator<=>(const BuildCommandID& inOther) const = default;
};


// Instance of a rule for a specific input file.
struct CookingCommand : NoCopy
{
	CookingRuleID       mRuleID;
	FileID              mDepFile;

	std::vector<FileID> mInputs;
	std::vector<FileID> mOutputs;
};


struct CookingSystem : NoCopy
{
	const CookingRule&                    GetRule(CookingRuleID inID) const { return mRules[inID.mIndex]; }
	const CookingCommand&                 GetBuildCommand(BuildCommandID inID) const { return mBuildCommands[inID.mIndex]; }

	CookingRule&                          AddRule() { return mRules.emplace_back(); }
	void                                  CreateCommandsForFile(FileInfo& ioFile) const;

	bool                                  ValidateRules(); // Return false if problems were found (see log).

	SegmentedVector<CookingRule, 256>     mRules;
	SegmentedVector<CookingCommand, 4096> mBuildCommands;
};


inline CookingSystem gCookingSystem;