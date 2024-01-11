#pragma once

#include "Core.h"
#include "Strings.h"
#include "FileSystem.h"
#include "CookingSystemIDs.h"

#include <vector>

#include "Log.h"

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
	int                      mPriority       = 0;
	uint16                   mVersion        = 0;
	bool                     mMatchMoreRules = false; // If false, we'll stop matching rules once an input file is matched with this rule. If true, we'll keep looking.
	StringView               mDepFilePath;
	std::vector<StringView>  mInputPaths;
	std::vector<StringView>  mOutputPaths;

	bool                     UseDepFile() const { return !mDepFilePath.empty(); }
};

enum class CookingState : uint8
{
	Unknown,
	InQueue,
	Cooking,
	Waiting,	// After cooking, we need to wait a little to get the USN events and see if all outputs were written (otherwise it's an Error instead of Success).
	Error,
	Success,
	_Count,
};

constexpr StringView gToStringView(CookingState inVar)
{
	constexpr StringView cNames[]
	{
		"Unknown",
		"InQueue",
		"Cooking",
		"Waiting",
		"Error",
		"Success",
	};
	static_assert(gElemCount(cNames) == (size_t)CookingState::_Count);

	return cNames[(int)inVar];
};

struct CookingLogEntry
{
	// TODO add a start time (and duration?)
	CookingCommandID          mCommandID;
	std::atomic<CookingState> mCookingState = CookingState::Unknown;
	StringView                mOutput;
};


// Instance of a rule for a specific input file.
struct CookingCommand : NoCopy
{
	CookingCommandID       mID;
	CookingRuleID          mRuleID;
	std::vector<FileID>    mInputs;
	std::vector<FileID>    mOutputs;

	enum DirtyState : uint8
	{
		NotDirty      = 0,
		InputMissing  = 0b0001,	// Inputs can be missing because they'll be created by an earlier command. If they're still missing by the time we try to cook, it's an error (bad ordering, or truly missing input).
		InputChanged  = 0b0010,
		OutputMissing = 0b0100,
	};

	DirtyState                mDirtyState     = NotDirty;
	std::atomic<CookingState> mCookingState   = CookingState::Unknown;
	USN                       mLastCook       = 0;
	CookingLogEntry*          mLastCookingLog = nullptr;

	void                      UpdateDirtyState();
	bool                      IsDirty() const { return mDirtyState != NotDirty; }

	void                      ReadDepFile();

	FileID                    GetMainInput() const { return mInputs[0]; }
	FileID                    GetDepFile() const;
	const CookingRule&        GetRule() const;
};

constexpr CookingCommand::DirtyState& operator|=(CookingCommand::DirtyState& ioA, CookingCommand::DirtyState inB) { return ioA = (CookingCommand::DirtyState)(ioA | inB); }


struct CookingQueue : NoCopy
{
	void             Push(CookingCommandID inCommandID);
	CookingCommandID Pop();

	struct PrioBucket
	{
		int                           mPriority = 0;
		std::vector<CookingCommandID> mCommands;

		auto                          operator<=>(int inOrder) const { return mPriority <=> inOrder; }
		auto                          operator==(int inOrder) const { return mPriority == inOrder; }
		auto                          operator<=>(const PrioBucket& inOther) const { return mPriority <=> inOther.mPriority; }
	};

	std::vector<PrioBucket> mPrioBuckets;
	size_t                  mTotalSize = 0;
	std::mutex              mMutex;

};


struct CookingSystem : NoCopy
{
	const CookingRule&                    GetRule(CookingRuleID inID) const { return mRules[inID.mIndex]; }
	CookingCommand&                       GetCommand(CookingCommandID inID) { return mCommands[inID.mIndex]; }

	CookingRule&                          AddRule() { return mRules.emplace_back(CookingRuleID{ (int16)mRules.size() }); }
	void                                  CreateCommandsForFile(FileInfo& ioFile);

	bool                                  ValidateRules(); // Return false if problems were found (see log).
	void                                  StartCooking();
	void                                  StopCooking();
	void                                  SetCookingPaused(bool inPaused);
	bool                                  IsCookingPaused() const { return mCookingPaused; }

	void                                  KickOneCookingThread();

private:
	struct CookingThread;

	void                                  CookingThreadFunction(CookingThread* ioThread, std::stop_token inStopToken);
	CookingLogEntry&                      AllocateCookingLogEntry(CookingCommandID inCommandID);
	void                                  CookCommand(CookingCommand& ioCommand, StringPool& ioStringPool);
	void                                  AddToWaitList(CookingCommandID);
	void                                  ProcessWaitList();

	SegmentedVector<CookingRule, 256>     mRules;

	SegmentedVector<CookingCommand, 4096> mCommands;
	std::mutex                            mCommandsMutex;

	struct CookingThread
	{
		std::jthread mThread;
		StringPool   mStringPool;
	};
	std::vector<CookingThread>               mCookingThreads;
	std::counting_semaphore<>                mCookingQueueSemaphore  = std::counting_semaphore(0);
	std::counting_semaphore<>                mCookingResumeSemaphore = std::counting_semaphore(0);
	bool                                     mCookingPaused          = false;

	friend void                              gUIDrawCookingLog();
	friend void                              gUIDrawSelectedCookingLogEntry();
	SegmentedVector<CookingLogEntry>         mCookingLog;
	std::mutex                               mCookingLogMutex;

	std::array<HashSet<CookingCommandID>, 2> mWaitingCommands;
	std::mutex                               mWaitingCommandsMutex;
	int                                      mWaitingCommandCurrentIndex = 0;
};


inline CookingSystem gCookingSystem;
inline CookingQueue  gCookingQueue;


inline const CookingRule& CookingCommand::GetRule() const { return gCookingSystem.GetRule(mRuleID); }
