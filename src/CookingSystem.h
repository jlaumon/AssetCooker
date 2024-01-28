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
Optional<String> gFormatCommandString(StringView inFormatStr, const FileInfo& inFile);

struct RepoAndFilePath
{
	FileRepo& mRepo;
	String    mPath;
};
// Check for CommandVariables and replace them by the corresponding part of inFile but expects the string to be a single file path.
// One Repo var is needed at the start and of the path and the corresponding FileRepo will be returned instead of be replaced by its path.
Optional<RepoAndFilePath> gFormatFilePath(StringView inFormatStr, const FileInfo& inFile);


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
	Cooking,
	Waiting,	// After cooking, we need to wait a little to get the USN events and see if all outputs were written (otherwise it's an Error instead of Success).
	Error,		// TODO: maybe we need a second error value for the kind that will never go away (ie. generating command line fails)
	Success,
	_Count,
};

constexpr StringView gToStringView(CookingState inVar)
{
	constexpr StringView cNames[]
	{
		"Unknown",
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
	CookingLogEntryID         mID;
	CookingCommandID          mCommandID;
	std::atomic<CookingState> mCookingState = CookingState::Unknown;
	bool                      mIsCleanup    = false;
	FileTime                  mTimeStart;
	FileTime                  mTimeEnd;		// Unsafe to read unless CookingState is > Cooking. TODO add getters that assert this
	StringView                mOutput;		// Unsafe to read unless CookingState is > Cooking.
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
		NotDirty          = 0,
		InputMissing      = 0b00001, // Inputs can be missing because they'll be created by an earlier command. If they're still missing by the time we try to cook, it's an error.
		InputChanged      = 0b00010,
		OutputMissing     = 0b00100,
		AllInputsMissing  = 0b01000, // Command needs to be cleaned up.
		AllOutputsMissing = 0b10000,
	};

	DirtyState                mDirtyState     = NotDirty;
	bool                      mIsQueued       = false;
	USN                       mLastCook       = 0;
	CookingLogEntry*          mLastCookingLog = nullptr;

	void                      UpdateDirtyState();
	bool                      IsDirty() const { return mDirtyState != NotDirty && !IsCleanedUp(); }
	bool                      NeedsCleanup() const { return (mDirtyState & AllInputsMissing) && !IsCleanedUp(); }
	bool                      IsCleanedUp() const { return (mDirtyState & (AllInputsMissing | AllOutputsMissing)) == (AllInputsMissing | AllOutputsMissing); }

	CookingState              GetCookingState() const { return mLastCookingLog ? mLastCookingLog->mCookingState.load() : CookingState::Unknown; }

	void                      ReadDepFile();

	FileID                    GetMainInput() const { return mInputs[0]; }
	FileID                    GetDepFile() const;
	const CookingRule&        GetRule() const;
};

constexpr CookingCommand::DirtyState& operator|=(CookingCommand::DirtyState& ioA, CookingCommand::DirtyState inB) { return ioA = (CookingCommand::DirtyState)(ioA | inB); }

enum class PushPosition
{
	Back,
	Front
};

enum class RemoveOption : uint8
{
	None        = 0b00,
	KeepOrder   = 0b01,
	ExpectFound = 0b10
};

constexpr RemoveOption operator|(RemoveOption inA, RemoveOption inB) { return (RemoveOption)((uint8)inA | (uint8)inB); }
constexpr bool         operator&(RemoveOption inA, RemoveOption inB) { return ((uint8)inA & (uint8)inB) != 0; }

struct CookingQueue : NoCopy
{
	void             SetSemaphore(std::counting_semaphore<>* ioSemaphore) { mSemaphore = ioSemaphore; } // Set a semaphore that will be acquired/released when commands are popped/pushed.

	void             Push(CookingCommandID inCommandID, PushPosition inPosition = PushPosition::Back);
	CookingCommandID Pop();

	void             Remove(CookingCommandID inCommandID, RemoveOption inOption = RemoveOption::None);
	void             Clear();

	struct PrioBucket
	{
		int                           mPriority = 0;
		std::vector<CookingCommandID> mCommands; // TODO replace by a ring buffer or some kind of deque

		auto                          operator<=>(int inOrder) const { return mPriority <=> inOrder; }
		auto                          operator==(int inOrder) const { return mPriority == inOrder; }
		auto                          operator<=>(const PrioBucket& inOther) const { return mPriority <=> inOther.mPriority; }
	};

	std::vector<PrioBucket>    mPrioBuckets;
	size_t                     mTotalSize = 0;
	std::mutex                 mMutex;
	std::counting_semaphore<>* mSemaphore = nullptr;
};


struct CookingSystem : NoCopy
{
	CookingSystem();
	~CookingSystem() = default;

	const CookingRule&                    GetRule(CookingRuleID inID) const { return mRules[inID.mIndex]; }
	CookingCommand&                       GetCommand(CookingCommandID inID) { return mCommands[inID.mIndex]; }
	CookingLogEntry&                      GetLogEntry(CookingLogEntryID inID) { return mCookingLog[inID.mIndex]; }

	CookingRule&                          AddRule() { return mRules.emplace_back(CookingRuleID{ (int16)mRules.size() }); }
	void                                  CreateCommandsForFile(FileInfo& ioFile);

	bool                                  ValidateRules(); // Return false if problems were found (see log).
	void                                  StartCooking();
	void                                  StopCooking();
	void                                  SetCookingPaused(bool inPaused);
	bool                                  IsCookingPaused() const { return mCookingPaused; }

	void                                  QueueUpdateDirtyStates(FileID inFileID);
	bool                                  ProcessUpdateDirtyStates(); // Return true if there are still commands to update.

	void                                  ForceCook(CookingCommandID inCommandID);

	bool                                  mSlowMode = false; // Slows down cooking, for debugging.
private:
	friend struct CookingCommand;
	friend void gDrawCookingQueue();
	friend void gDrawCommandSearch();
	friend void gDrawCookingThreads();
	friend void gDrawDebugWindow();
	struct CookingThread;

	void                                  CookingThreadFunction(CookingThread* ioThread, std::stop_token inStopToken);
	CookingLogEntry&                      AllocateCookingLogEntry(CookingCommandID inCommandID);
	void                                  CookCommand(CookingCommand& ioCommand, CookingThread& ioThread);
	void                                  CleanupCommand(CookingCommand& ioCommand, CookingThread& ioThread); // Delete all outputs.
	void                                  AddTimeOut(CookingLogEntry* inLogEntry);
	void                                  ProcessTimeOuts();
	void                                  TimeOutUpdateThread(std::stop_token inStopToken);

	SegmentedVector<CookingRule, 256>     mRules;
	VMemArray<CookingCommand>             mCommands;

	SegmentedHashSet<CookingCommandID>    mCommandsQueuedForUpdateDirtyState;
	std::mutex                            mCommandsQueuedForUpdateDirtyStateMutex;

	CookingQueue                          mCommandsDirty;	// All dirty commands.
	CookingQueue                          mCommandsToCook;	// Commands that will get cooked by the cooking threads.

	struct CookingThread
	{
		std::jthread                   mThread;
		StringPool                     mStringPool;
		std::atomic<CookingLogEntryID> mCurrentLogEntry;
	};
	SegmentedVector<CookingThread, 64>       mCookingThreads;
	std::counting_semaphore<>                mCookingThreadsSemaphore  = std::counting_semaphore(0);
	bool                                     mCookingPaused          = false;

	friend void                              gDrawCookingLog();
	friend void                              gDrawSelectedCookingLogEntry();
	VMemArray<CookingLogEntry>               mCookingLog;
	std::mutex                               mCookingLogMutex;

	std::array<HashSet<CookingLogEntry*>, 2> mTimeOutBatches;
	std::mutex                               mTimeOutsMutex;
	int                                      mTimeOutBatchCurrentIndex = 0;
	std::jthread                             mTimeOutUpdateThread;
	std::binary_semaphore                    mTimeOutAddedSignal = std::binary_semaphore(0);
	std::binary_semaphore                    mTimeOutTimerSignal = std::binary_semaphore(0);
};


inline CookingSystem gCookingSystem;


inline const CookingRule& CookingCommand::GetRule() const { return gCookingSystem.GetRule(mRuleID); }
