/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
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
	Dir_NoTrailingSlash,
	Path,
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
		"Dir_NoTrailingSlash",
		"Path",
		"Repo",
	};
	static_assert(gElemCount(cNames) == (size_t)CommandVariables::_Count);

	return cNames[(int)inVar];
};

// Check for CommandVariables and replace them by the corresponding part of inFile.
// Eg. "copy.exe {Repo:Source}{Path} {Repo:Bin}" will turn into "copy.exe D:/src/file.txt D:/bin/"
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
	uint32                  mRepoIndex = FileID::cInvalid().mRepoIndex;
	StringView              mPathPattern;

	bool                    Pass(const FileInfo& inFile) const;
};


enum class DepFileFormat : uint8
{
	AssetCooker, // Custom Asset Cooker dep file format.
	Make,        // For dep files generated with -M from Clang/GCC/DXC.
	_Count,
};


constexpr StringView gToStringView(DepFileFormat inVar)
{
	constexpr StringView cStrings[]
	{
		"AssetCooker",
		"Make",
	};
	static_assert(gElemCount(cStrings) == (size_t)DepFileFormat::_Count);

	return cStrings[(int)inVar];
};


enum class CommandType : uint8
{
	CommandLine,
	CopyFile,
	_Count
};


constexpr StringView gToStringView(CommandType inVar)
{
	constexpr StringView cStrings[]
	{
		"CommandLine",
		"CopyFile",
	};
	static_assert(gElemCount(cStrings) == (size_t)CommandType::_Count);

	return cStrings[(int)inVar];
};


struct CookingRule : NoCopy
{
	static constexpr uint16  cInvalidVersion = UINT16_MAX;

	CookingRule(CookingRuleID inID) : mID(inID) {}

	const CookingRuleID      mID;
	StringView               mName;
	int16                    mPriority            = 0;
	uint16                   mVersion             = 0;
	CommandType              mCommandType         = CommandType::CommandLine;
	bool                     mMatchMoreRules      = false; // If false, we'll stop matching rules once an input file is matched with this rule. If true, we'll keep looking.
	DepFileFormat            mDepFileFormat       = DepFileFormat::AssetCooker;
	StringView               mDepFilePath;        // Optional file containing extra inputs/ouputs for the command.
	StringView               mDepFileCommandLine; // Optional separate command line used to generate the dep file (in case the main command cannot generate it directly).
	StringView               mCommandLine;
	std::vector<InputFilter> mInputFilters;
	std::vector<StringView>  mInputPaths;
	std::vector<StringView>  mOutputPaths;

	mutable std::atomic<int> mCommandCount = 0;

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
	CookingCommandID    mID;
	CookingRuleID       mRuleID;
	std::vector<FileID> mInputs;         // Static inputs.
	std::vector<FileID> mOutputs;        // Static outputs.
	std::vector<FileID> mDepFileInputs;  // Dynamic inputs specified by the dep file.
	std::vector<FileID> mDepFileOutputs; // Dynamic outputs specified by the dep file.

	enum DirtyState : uint8
	{
		NotDirty               = 0,
		InputMissing           = 0b0000001, // Inputs can be missing because they'll be created by an earlier command. If they're still missing by the time we try to cook, it's an error.
		InputChanged           = 0b0000010,
		OutputMissing          = 0b0000100,
		AllStaticInputsMissing = 0b0001000, // Command needs to be cleaned up.
		AllOutputsMissing      = 0b0010000,
		Error                  = 0b0100000, // Last cook errored.
		VersionMismatch        = 0b1000000, // Rule version changed.
	};

	// TODO should also store last cook time here, so we can save it in the cached state (currently it's only in the log entries)
	DirtyState                      mDirtyState          = NotDirty;
	bool                            mIsQueued            = false;
	uint16                          mLastCookRuleVersion = CookingRule::cInvalidVersion;
	USN                             mLastDepFileRead     = 0;
	USN                             mLastCookUSN         = 0;
	FileTime                        mLastCookTime        = {};
	CookingLogEntry*                mLastCookingLog      = nullptr;

	void                            UpdateDirtyState();
	bool                            IsDirty() const { return mDirtyState != NotDirty && !IsCleanedUp(); }
	bool                            NeedsCleanup() const { return (mDirtyState & AllStaticInputsMissing) && !IsCleanedUp(); }
	bool                            IsCleanedUp() const { return (mDirtyState & (AllStaticInputsMissing | AllOutputsMissing)) == (AllStaticInputsMissing | AllOutputsMissing); }

	CookingState                    GetCookingState() const { return mLastCookingLog ? mLastCookingLog->mCookingState.load() : CookingState::Unknown; }

	bool                            ReadDepFile();

	FileID                          GetMainInput() const { return mInputs[0]; }
	FileID                          GetDepFile() const;
	const CookingRule&              GetRule() const;

	MultiSpanRange<const FileID, 2> GetAllInputs() const { return { mInputs, mDepFileInputs }; }
	MultiSpanRange<const FileID, 2> GetAllOutputs() const { return { mOutputs, mDepFileOutputs }; }
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
	ExpectFound = 0b10	// For validation only, will assert if not found.
};

constexpr RemoveOption operator|(RemoveOption inA, RemoveOption inB) { return (RemoveOption)((uint8)inA | (uint8)inB); }
constexpr bool         operator&(RemoveOption inA, RemoveOption inB) { return ((uint8)inA & (uint8)inB) != 0; }


struct CookingQueue : NoCopy
{
	void             Push(CookingCommandID inCommandID, PushPosition inPosition = PushPosition::Back);
	CookingCommandID Pop();

	bool             Remove(CookingCommandID inCommandID, RemoveOption inOption = RemoveOption::None);	// Return true if removed.
	void             Clear();

	size_t           GetSize() const;
	bool             IsEmpty() const { return GetSize() == 0; }

	void             PushInternal(std::unique_lock<std::mutex>& ioLock, int inPriority, CookingCommandID inCommandID, PushPosition inPosition);

	struct PrioBucket
	{
		int                           mPriority = 0;
		std::vector<CookingCommandID> mCommands; // TODO replace by a ring buffer or some kind of deque

		auto                          operator<=>(int inOrder) const { return mPriority <=> inOrder; }
		auto                          operator==(int inOrder) const { return mPriority == inOrder; }
		auto                          operator<=>(const PrioBucket& inOther) const { return mPriority <=> inOther.mPriority; }
	};

	std::vector<PrioBucket> mPrioBuckets;
	size_t                  mTotalSize = 0;
	mutable std::mutex      mMutex;
};


struct CookingThreadsQueue : CookingQueue
{
	void                    Push(CookingCommandID inCommandID, PushPosition inPosition = PushPosition::Back);
	CookingCommandID        Pop();
	void                    FinishedCooking(CookingCommandID inCommandID);

	void                    RequestStop();

	struct PrioData
	{
		int mPriority            = 0;
		int mCommandsBeingCooked = 0; // Number of commands not in the list anymore but still cooking.

		auto operator<=>(int inOrder) const { return mPriority <=> inOrder; }
		auto operator==(int inOrder) const { return mPriority == inOrder; }
		auto operator<=>(const PrioBucket& inOther) const { return mPriority <=> inOther.mPriority; }
	};
	std::vector<PrioData>   mPrioData;
	std::condition_variable mBarrier;
	bool                    mStopRequested = false;
};


struct CookingSystem : NoCopy
{
	CookingSystem()  = default;
	~CookingSystem() = default;

	const CookingRule&                    GetRule(CookingRuleID inID) const { return mRules[inID.mIndex]; }
	CookingCommand&                       GetCommand(CookingCommandID inID) { return mCommands[inID.mIndex]; }
	CookingLogEntry&                      GetLogEntry(CookingLogEntryID inID) { return mCookingLog[inID.mIndex]; }

	CookingRule&                          AddRule() { return mRules.Emplace({}, CookingRuleID{ (int16)mRules.Size() }); }
	StringPool&                           GetStringPool() { return mStringPool; }
	void                                  CreateCommandsForFile(FileInfo& ioFile);

	const CookingRule*                    FindRule(StringView inRuleName) const;
	CookingCommand*                       FindCommandByMainInput(CookingRuleID inRule, FileID inFileID);
	Span<const CookingRule>               GetRules() const { return { mRules }; }
	Span<const CookingCommand>            GetCommands() const { return { mCommands }; }

	bool                                  ValidateRules(); // Return false if problems were found (see log).
	void                                  StartCooking();
	void                                  StopCooking();
	void                                  SetCookingPaused(bool inPaused);
	bool                                  IsCookingPaused() const { return mCookingPaused; }
	void                                  SetCookingThreadCount(int inThreadCount) { mWantedCookingThreadCount = inThreadCount; }
	int                                   GetCookingThreadCount() const { return mWantedCookingThreadCount; }

	size_t                                GetCommandCount() const { return mCommands.Size(); } // Total number of commands, for debug/display.

	void                                  QueueUpdateDirtyStates(FileID inFileID);
	void                                  QueueUpdateDirtyState(CookingCommandID inCommandID);
	bool                                  ProcessUpdateDirtyStates(); // Return true if there are still commands to update.
	void                                  UpdateAllDirtyStates(); // Update the dirty state of all commands. Only needed during init.
	void                                  UpdateNotifications();

	void                                  ForceCook(CookingCommandID inCommandID);
	bool                                  IsIdle() const; // Return true if nothing is happening. Used by the UI to decide if it needs to draw.

	CookingLogEntry&                      AllocateCookingLogEntry(CookingCommandID inCommandID);

	bool                                  mSlowMode = false; // Slows down cooking, for debugging.

	// Experimental, used by main.cpp
	void                                  QueueErroredCommands();

private:
	friend struct CookingCommand;
	friend void gDrawCookingQueue();
	friend void gDrawCommandSearch();
	friend void gDrawCookingThreads();
	friend void gDrawDebugWindow();
	struct CookingThread;

	void                                  CookingThreadFunction(CookingThread* ioThread, std::stop_token inStopToken);
	void                                  CookCommand(CookingCommand& ioCommand, CookingThread& ioThread);
	void                                  CleanupCommand(CookingCommand& ioCommand, CookingThread& ioThread); // Delete all outputs.
	void                                  AddTimeOut(CookingLogEntry* inLogEntry);
	void                                  TimeOutUpdateThread(std::stop_token inStopToken);
	void                                  QueueDirtyCommands();

	VMemArray<CookingRule>                mRules      = { 1024ull * 1024, 4096 };
	StringPool                            mStringPool = { 64ull * 1024 };
	VMemArray<CookingCommand>             mCommands;

	SegmentedHashSet<CookingCommandID>    mCommandsQueuedForUpdateDirtyState;
	std::mutex                            mCommandsQueuedForUpdateDirtyStateMutex;

	CookingQueue                          mCommandsDirty;	// All dirty commands.
	CookingThreadsQueue                   mCommandsToCook;	// Commands that will get cooked by the cooking threads.

	struct CookingThread
	{
		std::jthread                   mThread;
		StringPool                     mStringPool;
		std::atomic<CookingLogEntryID> mCurrentLogEntry;
	};
	SegmentedVector<CookingThread, 64>       mCookingThreads;
	bool                                     mCookingStartPaused     = false;
	bool                                     mCookingPaused          = true;
	int                                      mWantedCookingThreadCount = 0;	// Number of threads requested. Actual number of threads created might be lower. 

	friend void                              gDrawCookingLog();
	friend void                              gDrawSelectedCookingLogEntry();
	VMemArray<CookingLogEntry>               mCookingLog;

	std::atomic<size_t>                      mCookingErrors           = 0;	// Total number of commands that ended in error.
	size_t                                   mLastNotifCookingErrors  = 0;
	size_t                                   mLastNotifCookingLogSize = 0;
	int64                                    mLastNotifTicks          = 0;

	HashSet<CookingLogEntry*>                mTimeOutCurrentBatch;
	HashSet<CookingLogEntry*>                mTimeOutNextBatch;
	mutable std::mutex                       mTimeOutMutex;
	std::jthread                             mTimeOutUpdateThread;
	std::condition_variable                  mTimeOutAddedSignal;
	std::binary_semaphore                    mTimeOutTimerSignal = std::binary_semaphore(0);

	OwnedHandle                              mJobObject; // JobObject used to make sure child processes are killed if this process ends.
};


inline CookingSystem gCookingSystem;


inline const CookingRule& CookingCommand::GetRule() const { return gCookingSystem.GetRule(mRuleID); }
