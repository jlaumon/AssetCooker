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

#include <Bedrock/String.h>
#include <Bedrock/Thread.h>
#include <Bedrock/Mutex.h>
#include <Bedrock/ConditionVariable.h>
#include <Bedrock/Semaphore.h>
#include <Bedrock/Atomic.h>
#include <Bedrock/HashMap.h>



struct InputFilter
{
	uint32     mRepoIndex = FileID::cInvalid().mRepoIndex;
	StringView mPathPattern;

	bool       Pass(const FileInfo& inFile) const;
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
	Vector<InputFilter>      mInputFilters;
	Vector<StringView>       mInputPaths;
	Vector<StringView>       mOutputPaths;

	mutable AtomicInt32      mCommandCount = 0;

	bool                     UseDepFile() const { return !mDepFilePath.Empty(); }
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
	static_assert(gElemCount(cNames) == (int)CookingState::_Count);

	return cNames[(int)inVar];
};


struct CookingLogEntry
{
	CookingLogEntryID         mID;
	CookingCommandID          mCommandID;
	Atomic<CookingState>      mCookingState = CookingState::Unknown;
	bool                      mIsCleanup    = false;
	bool					  mNotAllOutputWrittenError = false; // Can only be detected too late to append to the output log
	FileTime                  mTimeStart;
	FileTime				  mTimeEnd;			  // Unsafe to read unless CookingState is > Cooking. TODO add getters that assert this
	StringView				  mOutput;			  // Unsafe to read unless CookingState is > Cooking.
	Vector<FormatSpan>		  mOutputFormatSpans; // Unsafe to read unless CookingState is > Cooking.
	void					  SetOutput(StringView inOutput);
};


// Instance of a rule for a specific input file.
struct CookingCommand : NoCopy
{
	CookingCommandID    mID;
	CookingRuleID       mRuleID;
	Vector<FileID>      mInputs;         // Static inputs.
	Vector<FileID>      mOutputs;        // Static outputs.
	Vector<FileID>      mDepFileInputs;  // Dynamic inputs specified by the dep file.
	Vector<FileID>      mDepFileOutputs; // Dynamic outputs specified by the dep file.

	enum DirtyState : uint8
	{
		NotDirty               = 0,
		InputMissing           = 0b00000001, // Inputs can be missing because they'll be created by an earlier command. If they're still missing by the time we try to cook, it's an error.
		InputChanged           = 0b00000010,
		OutputMissing          = 0b00000100, // Output file does not exist.
		OutputOutdated         = 0b00001000, // Output file exists but was not written.
		AllStaticInputsMissing = 0b00010000, // Command needs to be cleaned up.
		AllOutputsMissing      = 0b00100000,
		Error                  = 0b01000000, // Last cook errored.
		VersionMismatch        = 0b10000000, // Rule version changed.
	};

	DirtyState                      mDirtyState          = NotDirty;
	bool                            mIsQueued            = false;
	uint16                          mLastCookRuleVersion = CookingRule::cInvalidVersion;
	USN                             mLastDepFileRead     = 0;
	USN                             mLastCookUSN         = 0;		// Value that represents the last time this command was cooked. All outputs USN have to be greater than this for the command to be NotDirty.
	FileTime                        mLastCookTime        = {};
	CookingLogEntry*                mLastCookingLog      = nullptr;

	void                            UpdateDirtyState();
	bool                            IsDirty() const { return mDirtyState != NotDirty && !IsCleanedUp(); }
	bool                            NeedsCleanup() const { return (mDirtyState & AllStaticInputsMissing) && !IsCleanedUp(); }
	bool                            IsCleanedUp() const { return (mDirtyState & (AllStaticInputsMissing | AllOutputsMissing)) == (AllStaticInputsMissing | AllOutputsMissing); }

	CookingState                    GetCookingState() const { return mLastCookingLog ? mLastCookingLog->mCookingState.Load() : CookingState::Unknown; }

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

	int              GetSize() const;
	bool             IsEmpty() const { return GetSize() == 0; }

	void             PushInternal(MutexLockGuard& ioLock, int inPriority, CookingCommandID inCommandID, PushPosition inPosition);

	struct PrioBucket
	{
		int                           mPriority = 0;
		Vector<CookingCommandID>      mCommands; // TODO replace by a ring buffer or some kind of deque

		auto                          operator<=>(int inOrder) const { return mPriority <=> inOrder; }
		auto                          operator==(int inOrder) const { return mPriority == inOrder; }
		auto                          operator<=>(const PrioBucket& inOther) const { return mPriority <=> inOther.mPriority; }
	};

	Vector<PrioBucket> mPrioBuckets;
	int                mTotalSize = 0;
	mutable Mutex      mMutex;
};


struct CookingThreadsQueue : CookingQueue
{
	void                    Push(CookingCommandID inCommandID, PushPosition inPosition = PushPosition::Back);
	CookingCommandID        Pop();
	void                    FinishedCooking(const CookingLogEntry& inLogEntry);

	void                    RequestStop();

	struct PrioData
	{
		int mPriority            = 0;
		int mCommandsBeingCooked = 0; // Number of commands not in the list anymore but still cooking.

		auto operator<=>(int inOrder) const { return mPriority <=> inOrder; }
		auto operator==(int inOrder) const { return mPriority == inOrder; }
		auto operator<=>(const PrioBucket& inOther) const { return mPriority <=> inOther.mPriority; }
	};
	Vector<PrioData>        mPrioData;
	ConditionVariable		mBarrier;
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
	int									  GetCookingErrorCount() const { return mCookingErrors.Load(); }

	int                                   GetCommandCount() const { return mCommands.Size(); } // Total number of commands, for debug/display.
	int									  GetDirtyCommandCount() const { return mCommandsDirty.GetSize(); }
	int									  GetCookedCommandCount() const { return mCookingLog.Size(); }

	void                                  QueueUpdateDirtyStates(FileID inFileID);
	void                                  QueueUpdateDirtyState(CookingCommandID inCommandID);
	bool                                  ProcessUpdateDirtyStates(); // Return true if there are still commands to update.
	void                                  UpdateAllDirtyStates(); // Update the dirty state of all commands. Only needed during init.
	void                                  UpdateNotifications();

	void                                  ForceCook(CookingCommandID inCommandID);
	bool                                  IsIdle() const; // Return true if nothing is happening. Used by the UI to decide if it needs to draw.

	CookingLogEntry&                      AllocateCookingLogEntry(CookingCommandID inCommandID);

	bool                                  mSlowMode = false; // Slows down cooking, for debugging.
private:
	friend struct CookingCommand;
	friend void gDrawCookingQueue();
	friend void gDrawCommandSearch();
	friend void gDrawCookingThreads();
	friend void gDrawDebugWindow();
	struct CookingThread;

	void                                  CookingThreadFunction(CookingThread& ioThread);
	void                                  CookCommand(CookingCommand& ioCommand, CookingThread& ioThread);
	void                                  CleanupCommand(CookingCommand& ioCommand, CookingThread& ioThread); // Delete all outputs.
	void                                  AddTimeOut(CookingLogEntry* inLogEntry);
	void                                  TimeOutUpdateThread();
	void                                  QueueDirtyCommands();
	void                                  QueueErroredCommands();

	VMemArray<CookingRule>                mRules      = { 1024ull * 1024, 4096 };
	StringPool                            mStringPool = { 64ull * 1024 };
	VMemArray<CookingCommand>             mCommands;

	VMemHashSet<CookingCommandID>		  mCommandsQueuedForUpdateDirtyState;
	mutable Mutex						  mCommandsQueuedForUpdateDirtyStateMutex;

	CookingQueue                          mCommandsDirty;	// All dirty commands.
	CookingThreadsQueue                   mCommandsToCook;	// Commands that will get cooked by the cooking threads.

	struct CookingThread
	{
		Thread						      mThread;
		StringPool					      mStringPool;
		Atomic<CookingLogEntryID>	      mCurrentLogEntry;
	};
	FixedVector<CookingThread, 128>       mCookingThreads;
	bool                                  mCookingStartPaused     = false;
	bool                                  mCookingPaused          = true;
	int                                   mWantedCookingThreadCount = 0;	// Number of threads requested. Actual number of threads created might be lower. 

	friend void                           gDrawCookingLog();
	friend void                           gDrawSelectedCookingLogEntry();
	VMemArray<CookingLogEntry>            mCookingLog;

	AtomicInt32                           mCookingErrors           = 0; // Total number of commands that ended in error.
	int                                   mLastNotifCookingErrors  = 0;
	int									  mLastNotifCookingLogSize = 0;
	int64                                 mLastNotifTicks          = 0;

	HashSet<CookingLogEntry*>             mTimeOutCurrentBatch;
	HashSet<CookingLogEntry*>             mTimeOutNextBatch;
	mutable Mutex                         mTimeOutMutex;
	Thread                                mTimeOutUpdateThread;
	ConditionVariable                     mTimeOutAddedSignal;
	Semaphore                             mTimeOutTimerSignal = Semaphore(0, 1);

	OwnedHandle                           mJobObject; // JobObject used to make sure child processes are killed if this process ends.
};


inline CookingSystem gCookingSystem;


inline const CookingRule& CookingCommand::GetRule() const { return gCookingSystem.GetRule(mRuleID); }
