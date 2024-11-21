/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "CookingSystem.h"
#include "App.h"
#include "Debug.h"
#include "DepFile.h"
#include "Notifications.h"
#include <Bedrock/Test.h>
#include <Bedrock/Algorithm.h>
#include <Bedrock/Ticks.h>

#include "subprocess/subprocess.h"
#include "win32/file.h"
#include "win32/misc.h"
#include "win32/process.h"

// Debug toggle to fake cooking failures, to test error handling.
bool gDebugFailCookingRandomly = false;


// Helper to add a value to a vector-like container only if it's not already in it.
template<typename taValue, typename taContainer>
constexpr bool gPushBackUnique(taContainer& ioContainer, const taValue& inElem)
{
	if (gContains(ioContainer, inElem))
		return false;

	ioContainer.PushBack(inElem);
	return true;
}



static bool sIsSpace(char inChar)
{
	return inChar == ' ' || inChar == '\t';
}


// Return a StringView on the text part of {text}.
static StringView sParseArgument(const char*& ioPtr, const char* inPtrEnd)
{
	gAssert(*ioPtr == '{');
	ioPtr++;

	// Skip white space before the argument.
	while (ioPtr != inPtrEnd && sIsSpace(*ioPtr))
		++ioPtr;

	const char* arg_begin = ioPtr;
	const char* arg_end   = ioPtr;

	while (ioPtr != inPtrEnd)
	{
		if (*ioPtr == '}' || sIsSpace(*ioPtr))
		{
			arg_end = ioPtr;
			++ioPtr;
			break;
		}

		ioPtr++;
	}

	// Skip white space and } after the argument.
	while (ioPtr != inPtrEnd && (*ioPtr == '}' || sIsSpace(*ioPtr)))
		++ioPtr;

	return { arg_begin, arg_end };
}


template<class taFormatter>
static Optional<String> sParseCommandVariables(StringView inFormatStr, const taFormatter& inFormatter)
{
	String str;
	const char* p_begin = inFormatStr.Data();
	const char* p_end   = gEndPtr(inFormatStr);
	const char* p       = p_begin;

	while (p != p_end)
	{
		if (*p == '{')
		{
			str.Append({ p_begin, p });

			StringView arg = sParseArgument(p, p_end);

			// sParseArgument made p point after the argument itself.
			p_begin = p;

			if (gStartsWith(arg, gToStringView(CommandVariables::Repo)))
			{
				arg.RemovePrefix(gToStringView(CommandVariables::Repo).Size());

				if (arg.Size() < 2 || arg[0] != ':')
					return {}; // Failed to get the repo name part.
			
				StringView repo_name = arg.SubStr(1);

				if (!inFormatter(CommandVariables::Repo, repo_name, { p, p_end }, str))
					return {}; // Formatter says error.
			}
			else
			{
				bool matched = false;
				for (int i = 0; i < (int)CommandVariables::_Count; ++i)
				{
					CommandVariables var = (CommandVariables)i;

					if (var == CommandVariables::Repo)
						continue; // Treated separately.

					if (arg == gToStringView(var))
					{
						matched = true;
						if (!inFormatter(var, "", { p, p_end }, str))
							return {}; // Formatter says error.

						break;
					}
				}

				if (!matched)
					return {}; // Invalid variable name.
			}
		}
		else
		{
			p++;
		}
	}

	str.Append({ p_begin, p });

	return str;
}


// TODO add an output error string to help understand why it fails
Optional<String> gFormatCommandString(StringView inFormatStr, const FileInfo& inFile)
{
	if (inFormatStr.Empty())
		return {}; // Consider empty format string is an error.

	return sParseCommandVariables(inFormatStr, [&inFile](CommandVariables inVar, StringView inRepoName, StringView inRemainingFormatStr, String& outStr) 
	{
		switch (inVar)
		{
		case CommandVariables::Ext:
			outStr.Append(inFile.GetExtension());
			break;
		case CommandVariables::File:
			outStr.Append(inFile.GetNameNoExt());
			break;
		case CommandVariables::Dir:
			if (!inFile.GetDirectory().Empty())
				outStr.Append(inFile.GetDirectory());

			// If the following character is a quote, the backslash at the end of the dir will escape it and the command line won't work.
			// Add a second backslash to avoid that.
			// Note: we also do it if the first backslash wasn't added by the Dir itself (if Dir is empty) as it's often preceded by a Repo (which also ends with a '\').
			if (!outStr.Empty() && outStr.Back() == '\\')
			{
				if (!inRemainingFormatStr.Empty() && inRemainingFormatStr[0] == '"')
					outStr.Append("\\");
			}
			break;
		case CommandVariables::Dir_NoTrailingSlash:
			if (!inFile.GetDirectory().Empty())
				outStr.Append(inFile.GetDirectory().SubStr(0, inFile.GetDirectory().Size() - 1));
			break;
		case CommandVariables::Path:
			outStr.Append(inFile.mPath);
			break;
		case CommandVariables::Repo:
			{
				FileRepo* repo = gFileSystem.FindRepo(inRepoName);

				// Invalid repo name.
				if (repo == nullptr)
					return false;

				outStr.Append(repo->mRootPath);

				// If the following character is a quote, the backslash at the end of the dir will escape it and the command line won't work.
				// Add a second backslash to avoid that.
				if (!inRemainingFormatStr.Empty() && inRemainingFormatStr[0] == '"')
					outStr.Append("\\");
			}
			break;
		default:
			return false;
		}

		return true;
	});
}


Optional<RepoAndFilePath> gFormatFilePath(StringView inFormatStr, const FileInfo& inFile)
{
	FileRepo*        repo = nullptr;
	Optional<String> path = sParseCommandVariables(inFormatStr, [&inFile, &repo](CommandVariables inVar, StringView inRepoName, StringView inRemainingFormatStr, String& outStr) 
	{
		switch (inVar)
		{
		case CommandVariables::Ext:
			outStr.Append(inFile.GetExtension());
			break;
		case CommandVariables::File:
			outStr.Append(inFile.GetNameNoExt());
			break;
		case CommandVariables::Dir:
			outStr.Append(inFile.GetDirectory());
			break;
		case CommandVariables::Dir_NoTrailingSlash:
			if (!inFile.GetDirectory().Empty())
				outStr.Append(inFile.GetDirectory().SubStr(0, inFile.GetDirectory().Size() - 1));
			break;
		case CommandVariables::Path:
			outStr.Append(inFile.mPath);
			break;
		case CommandVariables::Repo:
			// There can only be 1 Repo arg and it should be at the very beginning of the path.
			if (repo != nullptr || !outStr.Empty())
				return false;

			repo = gFileSystem.FindRepo(inRepoName);
			break;
		default:
			return false;
		}

		return true;
	});

	if (!path)
		return {};

	return RepoAndFilePath{ *repo, *path };
}


FileID gGetOrAddFileFromFormat(StringView inFormatStr, const FileInfo& inFile)
{
	Optional repo_and_path = gFormatFilePath(inFormatStr, inFile);
	if (!repo_and_path)
		return FileID::cInvalid();

	auto& [repo, path] = *repo_and_path;
	return repo.GetOrAddFile(path, FileType::File, {}).mID;
}


// Test a path against a pattern. Return true if the path matches the pattern (case-insensitive).
// Pattern supports wild cards '*' (any number of characters) and '?' (single character).
bool gMatchPath(StringView inPath, StringView inPattern)
{
	gAssert(!inPattern.Empty());
	gAssert(gIsNormalized(inPath) && gIsNormalized(inPattern));

	// Convert the path and pattern to lowercase to make sure the search is case-insensitive.
	TempPath path_lowercase = inPath;
	gToLowercase(path_lowercase.AsSpan());
	TempPath pattern_lowercase = inPattern;
	gToLowercase(pattern_lowercase.AsSpan());

	StringView str          = path_lowercase;
	StringView pattern      = pattern_lowercase;
	bool       pending_star = false;

	while (true)
	{
		// Find the next wild card in the pattern.
		int next_wildcard_index = pattern.FindFirstOf("?*");

		// If the next char isn't a wild card and we have a pending '*', process it now.
		// This case happens when we encounter '*?'. More explanations where pending_star is set to true below.
		if (next_wildcard_index != 0 && pending_star)
		{
			pending_star = false;

			// If the pattern ends with '*', it's an automatic match.
			if (pattern.Empty())
				return true;

			// Find where the string starts matching the pattern again.
			int next_match = str.Find(pattern[0]);

			// Never? Then it's a fail.
			if (next_match == -1)
				return false;

			// Skip to where it matches again.
			str.RemovePrefix(next_match);
		}

		// Strings should be equal until there (or until end of the string).
		if (str.SubStr(0, next_wildcard_index) != pattern.SubStr(0, next_wildcard_index))
			return false;

		// If there was no wild card, we're done! Strings match.
		if (next_wildcard_index == -1)
			return true;

		// Skip the parts that match.
		str.RemovePrefix(next_wildcard_index);
		pattern.RemovePrefix(next_wildcard_index);

		// Also skip the wild card, but keep a copy.
		char wild_card = pattern[0];
		pattern.RemovePrefix(1);

		if (wild_card == '?')
		{
			// If there is no character left, it's a fail.
			if (str.Size() < 1)
				return false;

			// Skip one character.
			str.RemovePrefix(1);
		}
		else
		{
			gAssert(wild_card == '*');

			// If the pattern ends with '*', it's an automatic match.
			if (pattern.Empty())
				return true;

			// If the * is followed by another '*', continue to process the next wild card directly, '**' is equivalent to '*'.
			if (pattern[0] == '*')
				continue;

			// If the '*' is followed by '?', it is THE annoying case.
			// '*?' is equivalent to '?*', so continue to process the '?', but remember we have a pending '*'.
			// This way both '*???' and '*?*?*?' will just be interpreted as '???*'.
			if (pattern[0] == '?')
			{
				pending_star = true;
				continue;
			}

			// Clear the pending '*' if we encounter another one.
			pending_star = false;

			// Find where the string starts matching the pattern again.
			int next_match = str.Find(pattern[0]);

			// Never? Then it's a fail.
			if (next_match == -1)
				return false;

			// Skip to where it matches again.
			str.RemovePrefix(next_match);
		}
	}
}


REGISTER_TEST("MatchPath")
{
	TEST_TRUE(gMatchPath ("YOYO.txt", "yoyo.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "*.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "y?yo.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "????????"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "*"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "?*"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "**"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "*?"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "*?oyo.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "*????.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "y*?*?*?.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "y*y*.txt"));
	TEST_TRUE(gMatchPath ("YOYO.txt", "y*?.*"));
	TEST_FALSE(gMatchPath("Y.txt", "y*?.*"));
	TEST_FALSE(gMatchPath("YOYO.txt", "yoyo.txt*?"));
};


bool InputFilter::Pass(const FileInfo& inFile) const
{
	if (mRepoIndex != inFile.mID.mRepoIndex)
		return false;

	return gMatchPath(inFile.mPath, mPathPattern);
}


FileID CookingCommand::GetDepFile() const
{
	const CookingRule& rule = gCookingSystem.GetRule(mRuleID);
	if (!rule.UseDepFile())
		return FileID::cInvalid();

	// The dep file is always the first output.
	gAssert(mOutputs[0].GetFile().mIsDepFile);
	return mOutputs[0];
}


void CookingCommand::UpdateDirtyState()
{
	// TODO there is no dedicated dirty state for the case where an output is outdated (ie. was not written), instead it's just an nondescript error and it's very confusing

	// Dirty state should not be updated while still cooking!
	gAssert(!mLastCookingLog || mLastCookingLog->mCookingState > CookingState::Cooking);

	DirtyState dirty_state = NotDirty;

	FileID dep_file = GetDepFile();
	// If the dep file is out of date, read it. The dirty state depends on its content.
	if (dep_file.IsValid() && dep_file.GetFile().mLastChangeUSN != mLastDepFileRead)
	{
		if (ReadDepFile())
		{
			// Update the last cook USN. This is normally updated just after cooking the command,
			// but when there's a dep file, we don't know all the inputs at that point.
			// Note: we also can't read the dep file at that point because it's happening on a cooking thread
			// and updating the list of inputs/outputs isn't thread safe.
			// TODO this does not work if multiple drives are involved, we can only compare USNs from the same journal
			USN max_input_usn = 0;
			for (FileID input_id : GetAllInputs())
				max_input_usn = gMax(max_input_usn, input_id.GetFile().mLastChangeUSN);
			mLastCookUSN = max_input_usn;
		}
		else
		{
			dirty_state |= Error;
		}
	}

	// If the rule version changed since the last time this command was cooked, it needs to cook again.
	if (mLastCookRuleVersion != GetRule().mVersion)
		dirty_state |= VersionMismatch;

	USN last_cook = mLastCookUSN;

	// If we don't have a last cook USN, estimate one.
	// Normally this should be stored in the cached state and read on start up,
	// but when doing an initial scan we use the oldest output (ie. min USN) as the
	// probable last point when the command was cooked.
	if (last_cook == 0 && !GetAllOutputs().Empty())
	{
		// TODO this does not work if multiple drives are involved, we can only compare USNs from the same journal
		last_cook = cMaxUSN;
		for (auto& file_id : GetAllOutputs())
			last_cook = gMin(last_cook, file_id.GetFile().mLastChangeUSN);
	}

	for (FileID file_id : GetAllInputs())
	{
		const FileInfo& file = file_id.GetFile();

		if (file.IsDeleted())
		{
			dirty_state |= InputMissing;
		}
		else
		{
			if (file.mLastChangeUSN > last_cook)
				dirty_state |= InputChanged;
		}
	}

	if (gAllOf(mInputs, [](FileID inFileID) { return inFileID.GetFile().IsDeleted(); }))
		dirty_state |= AllStaticInputsMissing;

	bool all_output_written = true;
	bool all_output_missing = true;
	for (FileID file_id : GetAllOutputs())
	{
		const FileInfo& file = file_id.GetFile();

		if (file.IsDeleted())
			dirty_state |= OutputMissing;
		else
			all_output_missing = false;

		// TODO this comparison does not work if multiple drives are involved, we can only compare USNs from the same journal
		if (file.mLastChangeUSN < mLastCookUSN)
			all_output_written = false;
	}
	
	if (all_output_missing)
		dirty_state |= AllOutputsMissing;

	bool last_cook_is_waiting = mLastCookingLog && mLastCookingLog->mCookingState == CookingState::Waiting;
	bool last_cook_is_cleanup = mLastCookingLog && mLastCookingLog->mIsCleanup;
	bool last_cook_is_error   = mLastCookingLog && mLastCookingLog->mCookingState == CookingState::Error;

	if (last_cook_is_error)
		dirty_state |= Error;

	// If the command is waiting for results and all outputs were written (or deleted in case of cleanup), change its state to success.
	if (last_cook_is_waiting)
	{
		if (!last_cook_is_cleanup && all_output_written ||
			last_cook_is_cleanup && all_output_missing)
		{
			mLastCookingLog->mCookingState = CookingState::Success;
			last_cook_is_waiting = false;
		}
	}

	mDirtyState = dirty_state;

	// Wait until the last cook is finished before re-adding to the queue (or removing it from the queue).
	if (last_cook_is_waiting)
		return;

	// The command wasn't dirty but is now.
	if (IsDirty() && !mIsQueued)
	{
		// TODO move all that to functions in CookingSystem and remove friend
		mIsQueued = true;

		gCookingSystem.mCommandsDirty.Push(mID);

		if (!gCookingSystem.IsCookingPaused())
			gCookingSystem.mCommandsToCook.Push(mID);
	}
	// The command was dirty but isn't anymore.
	else if (!IsDirty() && mIsQueued)
	{
		mIsQueued = false;

		// TODO these removes are slow! fix it!
		// Keep the order because it makes the UI much nicer.
		gCookingSystem.mCommandsDirty.Remove(mID, RemoveOption::KeepOrder | RemoveOption::ExpectFound);

		// Don't care about the order in the cooking queue as much since it's not displayed (and might not be found if a worker already grabbed it).
		gCookingSystem.mCommandsToCook.Remove(mID);
	}
	// Special last case: the command is already dirty, had an error, and its inputs changed again since.
	else if ((mDirtyState & Error) && (mDirtyState & InputChanged))
	{
		// Try cooking again.
		gAssert(mIsQueued);
		if (!gCookingSystem.IsCookingPaused())
			gCookingSystem.mCommandsToCook.Push(mID);
	}
}



bool CookingCommand::ReadDepFile()
{
	const FileInfo& dep_file = GetDepFile().GetFile();
	gAssert(mLastDepFileRead != dep_file.mLastChangeUSN); // Don't read the dep file if it's not necessary.

	// Update the USN of the last time we read the dep file.
	mLastDepFileRead = dep_file.mLastChangeUSN;

	Vector<FileID> inputs, outputs;

	// If the file is deleted, don't actually try to read it.
	if (!dep_file.IsDeleted())
	{
		// Read the dep file.
		if (!gReadDepFile(GetRule().mDepFileFormat, GetDepFile(), inputs, outputs))
		{
			// If the command was cooking, set its state to error.
			if (mLastCookingLog && mLastCookingLog->mCookingState == CookingState::Waiting)
				mLastCookingLog->mCookingState = CookingState::Error;

			// Return failure.
			return false;
		}
	}

	// Update this command with the new list of input/output.
	gApplyDepFileContent(*this, inputs, outputs);

	return true;
}


void CookingQueue::Push(CookingCommandID inCommandID, PushPosition inPosition/* = PushPosition::Back*/)
{
	const CookingCommand& command = gCookingSystem.GetCommand(inCommandID);
	int priority = gCookingSystem.GetRule(command.mRuleID).mPriority;

	std::unique_lock lock(mMutex);
	PushInternal(lock, priority, inCommandID, inPosition);
}


void CookingQueue::PushInternal(std::unique_lock<std::mutex>& ioLock, int inPriority, CookingCommandID inCommandID, PushPosition inPosition)
{
	gAssert(ioLock.mutex() == &mMutex);

	// Find or add the bucket for that cooking priority.
	PrioBucket& bucket = *gEmplaceSorted(mPrioBuckets, inPriority);

	// Add the command.
	if (inPosition == PushPosition::Back)
		bucket.mCommands.PushBack(inCommandID);
	else
		bucket.mCommands.Insert(0, inCommandID);

	mTotalSize++;
}


CookingCommandID CookingQueue::Pop()
{
	std::lock_guard lock(mMutex);

	// Find the first non-empty bucket.
	for (PrioBucket& bucket : mPrioBuckets)
	{
		if (!bucket.mCommands.Empty())
		{
			// Pop a command.
			CookingCommandID id = bucket.mCommands.Back();
			bucket.mCommands.PopBack();
			mTotalSize--;

			return id;
		}
	}

	return CookingCommandID::cInvalid();
}


bool CookingQueue::Remove(CookingCommandID inCommandID, RemoveOption inOption/* = RemoveOption::None*/)
{
	const CookingCommand& command = gCookingSystem.GetCommand(inCommandID);
	int priority = gCookingSystem.GetRule(command.mRuleID).mPriority;

	std::lock_guard lock(mMutex);

	// Find the bucket.
	auto bucket_it = gFindSorted(mPrioBuckets, priority);
	if (bucket_it == mPrioBuckets.end())
	{
		gAssert((inOption & RemoveOption::ExpectFound) == false);
		return false;
	}

	// Find the command in the bucket.
	auto it = gFind(bucket_it->mCommands, inCommandID);
	if (it == bucket_it->mCommands.end())
	{
		gAssert((inOption & RemoveOption::ExpectFound) == false);
		return false;
	}

	// Remove it.
	if (inOption & RemoveOption::KeepOrder)
	{
		bucket_it->mCommands.Erase(bucket_it->mCommands.GetIndex(*it));
	}
	else
	{
		std::swap(*it, bucket_it->mCommands.Back());
		bucket_it->mCommands.PopBack();
	}

	mTotalSize--;
	return true;
}


void CookingQueue::Clear()
{
	std::lock_guard lock(mMutex);

	for (PrioBucket& bucket : mPrioBuckets)
		bucket.mCommands.Clear();

	mTotalSize = 0;
}


size_t CookingQueue::GetSize() const
{
	std::lock_guard lock(mMutex);
	return mTotalSize;
}


void CookingThreadsQueue::Push(CookingCommandID inCommandID, PushPosition inPosition/* = PushPosition::Back*/)
{
	const CookingCommand& command = gCookingSystem.GetCommand(inCommandID);
	int priority = gCookingSystem.GetRule(command.mRuleID).mPriority;

	{
		std::unique_lock lock(mMutex);
		PushInternal(lock, priority, inCommandID, inPosition);

		// Make sure the data array stays in sync (makes pop simpler).
		gEmplaceSorted(mPrioData, priority);
	}

	// Wake up one thread to work on this.
	mBarrier.notify_one();
}


CookingCommandID CookingThreadsQueue::Pop()
{
	std::unique_lock lock(mMutex);
	gAssert(mPrioData.Size() == mPrioBuckets.Size());

	int  non_empty_bucket_index = -1;

	while (true)
	{
		// Break out of the loop if stopping was requested.
		// Note: this needs to be checked before waiting, as this thread will not be awaken again if it tries to wait after stop was requested.
		if (mStopRequested)
			break;

		// Wait for work.
		mBarrier.wait(lock);

		// Find the first bucket containing command that can run.
		for (int prio_index = 0; prio_index < mPrioBuckets.Size(); ++prio_index)
		{
			PrioBucket& bucket = mPrioBuckets[prio_index];
			PrioData&   data   = mPrioData[prio_index];

			// If this bucket is empty but some commands are still being cooked, wait until they're finished before checking the next buckets.
			if (bucket.mCommands.Empty() && data.mCommandsBeingCooked > 0)
			{
				// Go back to waiting and start over if more work is added or these commands are finished.
				break;
			}

			if (!bucket.mCommands.Empty())
			{
				// Found one!
				non_empty_bucket_index = (int)(&bucket - &mPrioBuckets.Front());
				break;
			}
		}

		// If we've found a bucket with commands that can run, exit the outer loop.
		if (non_empty_bucket_index != -1)
			break;
	}
	
	if (non_empty_bucket_index != -1)
	{
		PrioBucket&      bucket = mPrioBuckets[non_empty_bucket_index];
		PrioData&        data   = mPrioData[non_empty_bucket_index];

		// Pop a command.
		CookingCommandID id     = bucket.mCommands.Back();
		bucket.mCommands.PopBack();
		mTotalSize--;

		// Remember there's now one command ongoing.
		data.mCommandsBeingCooked++;

		return id;
	}

	return CookingCommandID::cInvalid();
}


void CookingThreadsQueue::FinishedCooking(CookingCommandID inCommandID)
{
	const CookingCommand& command  = gCookingSystem.GetCommand(inCommandID);
	int                   priority = gCookingSystem.GetRule(command.mRuleID).mPriority;
	bool                  notify   = false;

	{
		std::unique_lock lock(mMutex);

		// Find the bucket.
		auto bucket_it = gFindSorted(mPrioBuckets, priority);
		if (bucket_it == mPrioBuckets.end())
		{
			gAssert(false);
			return;
		}

		// Update the number of commands still cooking.
		int index = mPrioBuckets.GetIndex(*bucket_it);
		PrioData& data  = mPrioData[index];
		data.mCommandsBeingCooked--;

		// If this was the last command being cooked for this prio, notify any waiting thread.
		if (data.mCommandsBeingCooked == 0)
			notify = true;
	}

	// Notify outside of the lock, no reason to wake threads to immediately make them wait for the lock.
	if (notify)
		mBarrier.notify_all();
}


void CookingThreadsQueue::RequestStop()
{
	{
		std::unique_lock lock(mMutex);
		mStopRequested = true;
	}
	mBarrier.notify_all();
}


void CookingSystem::CreateCommandsForFile(FileInfo& ioFile)
{
	// Directories can't have commands.
	if (ioFile.IsDirectory())
		return;

	// Already done?
	if (ioFile.mCommandsCreated)
		return;

	ioFile.mCommandsCreated = true;

	for (const CookingRule& rule : mRules)
	{
		bool pass = false;
		for (auto& filter : rule.mInputFilters)
		{
			if (filter.Pass(ioFile))
			{
				pass = true;
				break;
			}
		}

		if (!pass)
			continue;

		CookingCommandID command_id;

		// Create the command.
		{
			bool   success = true;
			FileID dep_file;

			// Get the dep file (if needed).
			if (rule.UseDepFile())
			{
				dep_file = gGetOrAddFileFromFormat(rule.mDepFilePath, ioFile);
				if (dep_file.IsValid())
					dep_file.GetFile().mIsDepFile = true;
				else
					success = false;
			}

			// Add the main input file.
			// Note: order is important, the main input file is always the first input.
			Vector<FileID> inputs;
			inputs.PushBack(ioFile.mID);

			// Get the additional input files.
			for (StringView path : rule.mInputPaths)
			{
				FileID file = gGetOrAddFileFromFormat(path, ioFile);
				if (!file.IsValid())
				{
					success = false;
					continue;
				}

				gPushBackUnique(inputs, file);
			}

			// If there is an output dep file, add it to the outputs.
			// Note: order is important, the dep file is always the first output.
			Vector<FileID> outputs;
			if (dep_file.IsValid())
				outputs.PushBack(dep_file);

			// Add the ouput files.
			for (StringView path : rule.mOutputPaths)
			{
				FileID file = gGetOrAddFileFromFormat(path, ioFile);
				if (!file.IsValid())
				{
					success = false;
					continue;
				}

				gPushBackUnique(outputs, file);
			}

			// Most problems should be caught during ValidateRules,
			// but if something goes wrong anyway, log an error and ignore this rule.
			if (!success)
			{
				gApp.LogError("Failed to create Rule {} command for {}", rule.mName, ioFile);
				continue;
			}

			// Add the command to the global list.
			{
				auto lock = mCommands.Lock();

				// Build the ID now that we have the mutex.
				command_id              = CookingCommandID{ (uint32)mCommands.SizeRelaxed() };

				// Create the command.
				CookingCommand& command = mCommands.Emplace(lock);
				command.mID             = command_id;
				command.mRuleID         = rule.mID;
				command.mInputs         = gMove(inputs);
				command.mOutputs        = gMove(outputs);
			}

			// Update stats.
			rule.mCommandCount++;
		}

		// Let all the input and ouputs know that they are referenced by this command.
		{
			const CookingCommand& command = GetCommand(command_id);

			for (FileID file_id : command.mInputs)
				gFileSystem.GetFile(file_id).mInputOf.PushBack(command.mID);

			for (FileID file_id : command.mOutputs)
				gFileSystem.GetFile(file_id).mOutputOf.PushBack(command.mID);
		}

		// TODO: add validation
		// - a file cannot be the input/output of the same command
		// - all the inputs of a command can only be outputs of commands with lower prio (ie. that build before)

		// Check if we need to continue to try more rules for this file.
		if (!rule.mMatchMoreRules)
			break;
	}	
}


const CookingRule* CookingSystem::FindRule(StringView inRuleName) const
{
	for (const CookingRule& rule : mRules)
		if (gIsEqual(rule.mName, inRuleName))
			return &rule;

	return nullptr;
}


CookingCommand* CookingSystem::FindCommandByMainInput(CookingRuleID inRule, FileID inFileID)
{
	const FileInfo& file = inFileID.GetFile();
	for (CookingCommandID command_id : file.mInputOf)
	{
		CookingCommand& command = GetCommand(command_id);

		if (command.mRuleID == inRule && command.GetMainInput() == inFileID)
			return &command;
	}

	return nullptr;
}


bool CookingSystem::ValidateRules()
{
	HashSet<StringView> all_names;
	int                 errors = 0;
	Span                rules  = GetRules();

	// TODO: This is a temporary limitation because the current code compares USN numbers from multiple drives (and that doesn't work).
	//		 Solution are to either store one USN per drive involved in commands, etc. (but storing variable number of USN seems annoying/inefficent),
	//		 or hash files and compare hashes (which is more work but looks like the way to go for other reasons, like implementing a shared cache).
	if (gFileSystem.GetDriveCount() > 1)
	{
		gApp.LogError(R"(Having FileRepos on multiple Drives is not supported (yet).)");
		errors++;
	}

	for (const CookingRule& rule : rules)
	{
		// Validate the name.
		if (!rule.mName.Empty())
		{
			auto [_, inserted] = all_names.insert(rule.mName);
			if (!inserted)
			{
				errors++;
				gApp.LogError(R"(Found multiple rules with name "{}")", rule.mName);
			}
		}
		else
		{
			errors++;
			gApp.LogError(R"(Rule[{}] has no name)", rules.GetIndex(rule));
		}

		// Validate the version.
		if (rule.mVersion == CookingRule::cInvalidVersion)
		{
			errors++;
			gApp.LogError(R"(Rule {}, Version {} is a reserved value to indicate an invalid version.)", rule.mName, rule.mVersion);
		}

		// Validate the input filters.
		for (int i = 0; i < rule.mInputFilters.Size(); ++i)
		{
			const InputFilter& filter = rule.mInputFilters[i];

			// Make sure there's at least one way to filter.
			if (filter.mPathPattern.Empty())
			{
				errors++;
				gApp.LogError(R"(Rule {}, InputFilter[{}].PathPattern cannot be empty.)", rule.mName, i);
			}
		}

		// Dummy file info use to test path formatting.
		FileInfo dummy_file(FileID{ 0, 0 }, "dir\\dummy.txt", Hash128{ 0, 0 }, FileType::File, {});

		// Validate the command line.
		if (rule.mCommandType == CommandType::CommandLine && !gFormatCommandString(rule.mCommandLine, dummy_file))
		{
			errors++;
			gApp.LogError(R"(Rule {}: Failed to parse CommandLine "{}")", rule.mName, rule.mCommandLine);
		}

		// Validate the dep file path.
		if (rule.UseDepFile() && !gFormatCommandString(rule.mDepFilePath, dummy_file))
		{
			errors++;
			gApp.LogError(R"(Rule {}: Failed to parse DepFilePath "{}")", rule.mName, rule.mDepFilePath);
		}

		// Validate the input paths.
		for (int i = 0; i < rule.mInputPaths.Size(); ++i)
		{
			if (!gFormatCommandString(rule.mInputPaths[i], dummy_file))
			{
				errors++;
				gApp.LogError(R"(Rule {}: Failed to parse InputPaths[{}] "{}")", rule.mName, i, rule.mInputPaths[i]);
			}
		}

		// Validate the output paths.
		for (int i = 0; i < rule.mOutputPaths.Size(); ++i)
		{
			if (!gFormatCommandString(rule.mOutputPaths[i], dummy_file))
			{
				errors++;
				gApp.LogError(R"(Rule {}: Failed to parse OutputPaths[{}] "{}")", rule.mName, i, rule.mOutputPaths[i]);
			}
		}

		// Validate that there is at least one output.
		if (rule.mOutputPaths.Empty() && (!rule.UseDepFile() || rule.mDepFileFormat == DepFileFormat::Make)) // Make format DepFiles can only add inputs, not outputs.
		{
			errors++;
			gApp.LogError(R"(Rule {}: a rule must have at least one output, or a DepFile that can register outputs.)", rule.mName);
		}
	}

	return errors == 0;
}


static OwnedHandle sCreateJobObject()
{
	// Create a job object.
	OwnedHandle job_object = CreateJobObjectA(nullptr, nullptr);
	if (job_object == nullptr)
		gApp.FatalError("CreateJobObjectA failed - {}", GetLastErrorString());

	// Configure it so that child processes get killed with the parent.
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info = {};
	limit_info.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (SetInformationJobObject(job_object, JobObjectExtendedLimitInformation, &limit_info, sizeof(limit_info)) == FALSE)
		gApp.FatalError("SetInformationJobObject failed - {}", GetLastErrorString());

	return job_object;
}


void CookingSystem::StartCooking()
{
	// Zero/negative means no limit on thread count.
	int thread_count = (mWantedCookingThreadCount <= 0) ? INT_MAX : mWantedCookingThreadCount;

	// Number of threads is at least one, and is capped by number of CPU cores minus one,
	// because we want to leave one core for the file system monitoring thread (and main thread).
	thread_count = gClamp(thread_count, 1, (int)std::thread::hardware_concurrency() - 1);

	// Create the job object that will make sure child processes are killed if this process is killed.
	mJobObject = sCreateJobObject();

	gApp.Log("Starting {} Cooking Threads.", thread_count);

	mCookingThreads.reserve(thread_count);

	// Start the cooking threads.
	for (int i = 0; i < thread_count; ++i)
	{
		auto& thread = mCookingThreads.emplace_back();
		thread.mThread = std::jthread(std::bind_front(&CookingSystem::CookingThreadFunction, this, &thread));
	}

	// Start the thread updating the time outs.
	mTimeOutUpdateThread = std::jthread(std::bind_front(&CookingSystem::TimeOutUpdateThread, this));

	// Initialize cooking paused bool.
	mCookingPaused = mCookingStartPaused;

	// If the cooking isn't paused, queue the dirty commands.
	if (!IsCookingPaused())
		QueueDirtyCommands();
}


void CookingSystem::StopCooking()
{
	for (auto& thread : mCookingThreads)
		thread.mThread.request_stop();

	mCommandsToCook.RequestStop();

	for (auto& thread : mCookingThreads)
		thread.mThread.join();

	mJobObject = {};

	mTimeOutUpdateThread.request_stop();
	mTimeOutAddedSignal.notify_one();
	mTimeOutTimerSignal.release();
	mTimeOutUpdateThread.join();
}


void CookingSystem::SetCookingPaused(bool inPaused)
{
	// If cooking isn't started yet, only change the start paused bool.
	// Setting mCookingPaused to true before starting the cooking would cause dirty commands to be queued twice.
	if (!mTimeOutUpdateThread.joinable())
	{
		mCookingStartPaused = inPaused;
		return;
	}

	if (inPaused == mCookingPaused)
		return;

	if (inPaused)
	{
		mCookingPaused = true;

		// Empty the cooking queue.
		mCommandsToCook.Clear();

	}
	else
	{
		mCookingPaused = false;

		// Queue all the dirty commands that need to cook.
		QueueDirtyCommands();
	}
}


void CookingSystem::QueueDirtyCommands()
{
	std::lock_guard lock(mCommandsDirty.mMutex);

	for (auto& bucket : mCommandsDirty.mPrioBuckets)
	{
		for (CookingCommandID command_id : bucket.mCommands)
		{
			const CookingCommand& command = GetCommand(command_id);
			CookingState          cooking_state = command.GetCookingState();

			if (cooking_state == CookingState::Cooking || cooking_state == CookingState::Waiting)
				continue; // Skip commands already cooking.

			if (cooking_state == CookingState::Error && (command.mDirtyState & (CookingCommand::InputChanged | CookingCommand::VersionMismatch)) == 0)
				continue; // Skip commands that errored if their input hasn't changed since last time (unless the rule version changed).

			mCommandsToCook.Push(command_id);
		}
	}
}


void CookingSystem::QueueErroredCommands()
{
	std::lock_guard lock(mCommandsDirty.mMutex);

	for (auto& bucket : mCommandsDirty.mPrioBuckets)
	{
		for (CookingCommandID command_id : bucket.mCommands)
		{
			const CookingCommand& command = GetCommand(command_id);
			CookingState          cooking_state = command.GetCookingState();

			// If the command is in error state, queue it again.
			if (cooking_state == CookingState::Error)
				mCommandsToCook.Push(command_id);
		}
	}
}


static bool sRunCommandLine(StringView inCommandLine, StringPool::ResizableStringView& ioOutput, HANDLE inJobObject)
{
	ioOutput.AppendFormat("Command Line: {}\n\n", inCommandLine);

	// Create the process for the command line.
	subprocess_s process;
	int options = subprocess_option_no_window | subprocess_option_combined_stdout_stderr | subprocess_option_single_string_command_line | subprocess_option_enable_async;

	// Not strictly needed but some launched processes fail to read files if not used (access rights issues? unclear).
	options |= subprocess_option_inherit_environment;

	const char*  command_line_array[] = { inCommandLine.AsCStr(), nullptr };
	if (subprocess_create(command_line_array, options, &process))
	{
		ioOutput.AppendFormat("[error] Failed to create process - {}\n", GetLastErrorString());
		return false;
	}

	// Assign the job object to the process, to make sure it is killed if the Asset Cooker process ends.
	if (AssignProcessToJobObject(inJobObject, process.hProcess) == FALSE)
		gApp.FatalError("AssignProcessToJobObject failed - {}", GetLastErrorString());

	// Get the output.
	// TODO: optionally skip getting the output?
	// TODO: periodically check if we want to exit, and terminate the process (not possible with subprocess_read_stdout, there's no timeout option)
	{
		char  buffer[1024];
		while (true)
		{
			int bytes_read = (int)subprocess_read_stdout(&process, buffer, sizeof(buffer) - 1);
			buffer[bytes_read] = 0;

			if (bytes_read == 0)
				break;

			ioOutput.Append({ buffer, bytes_read });
		}
	}

	// Wait for the process to finish.
	int exit_code = 0;
	bool got_exit_code = subprocess_join(&process, &exit_code) == 0;

	bool success = true;

	// Log the exit code if we have it.
	if (!got_exit_code)
	{
		ioOutput.AppendFormat("[error] Failed to get exit code - {}\n", GetLastErrorString());
		success = false;
	}
	else
	{
		ioOutput.AppendFormat("\nExit code: {} (0x{:X})\n", exit_code, (uint32)exit_code);
	}

	// Non-zero exit code is considered an error.
	// TODO make that optional in the Rule
	if (exit_code != 0)
		success = false;
	
	// Destruct the process handles (this can't actually fail).
	subprocess_destroy(&process);

	return success;
}


bool sRunCopyFile(const CookingCommand& inCommand, StringPool::ResizableStringView& ioOutput)
{
	// Incompatible with DepFile, otherwise mOutputs[0] is the DepFile. This is checked by the RuleReader.
	gAssert(inCommand.GetRule().UseDepFile() == false);

	ioOutput.AppendFormat("Copying {} to {}\n", inCommand.mInputs[0].GetFile(), inCommand.mOutputs[0].GetFile());

	TempPath input (R"(\\?\{}{})", inCommand.mInputs [0].GetRepo().mRootPath, inCommand.mInputs [0].GetFile().mPath);
	TempPath output(R"(\\?\{}{})", inCommand.mOutputs[0].GetRepo().mRootPath, inCommand.mOutputs[0].GetFile().mPath);
	return CopyFileA(input.AsCStr(), output.AsCStr(), FALSE) != 0;
}


void CookingSystem::CookCommand(CookingCommand& ioCommand, CookingThread& ioThread)
{
	CookingLogEntry& log_entry = AllocateCookingLogEntry(ioCommand.mID);

	// Set the start time.
	log_entry.mTimeStart       = gGetSystemTimeAsFileTime();

	ioThread.mCurrentLogEntry  = log_entry.mID;
	defer { ioThread.mCurrentLogEntry = CookingLogEntryID::cInvalid(); };

	// Set the log entry on the command.
	ioCommand.mLastCookingLog = &log_entry;

	// Allocate a resizable string for the output.
	StringPool::ResizableStringView output_str = ioThread.mStringPool.CreateResizableString();

	const CookingRule& rule    = ioCommand.GetRule();

	// Update the last cook USN (used later know if this command needs to cook again).
	// Note: when there's a DepFile, we don't know the full list of inputs before reading it, so it will be done later, after reading it.
	if (!rule.UseDepFile())
	{
		// Get the max USN of all inputs.
		// TODO this does not work if multiple drives are involved, we can only compare USNs from the same journal
		USN max_input_usn = 0;
		for (FileID input_id : ioCommand.GetAllInputs())
			max_input_usn = gMax(max_input_usn, input_id.GetFile().mLastChangeUSN);
		
		// Set the inputs USN on the command.
		ioCommand.mLastCookUSN = max_input_usn;
	}

	// Update the last cook time.
	ioCommand.mLastCookTime = log_entry.mTimeStart;

	// Update the last cook version.
	ioCommand.mLastCookRuleVersion = rule.mVersion;

	defer
	{
		// If the command ends in error, we need to make sure that:
		// - its last cook USN is updated
		// - its state is updated
		// That normally happens when the outputs (and the dep file) are written, but that might not happen at all if there is an error.
		// This is important to then properly detect when the inputs change again and the command can re-cook.
		if (log_entry.mCookingState == CookingState::Error)
		{
			// If the last cook USN wasn't updated because there's a dep file, do it now with the currently known inputs.
			if (rule.UseDepFile())
			{
				// Get the max USN of all inputs.
				// TODO this does not work if multiple drives are involved, we can only compare USNs from the same journal
				USN max_input_usn = 0;
				for (FileID input_id : ioCommand.GetAllInputs())
					max_input_usn = gMax(max_input_usn, input_id.GetFile().mLastChangeUSN);
				
				// Set the inputs USN on the command.
				ioCommand.mLastCookUSN = max_input_usn;
			}

			// Queue for a dirty state update.
			QueueUpdateDirtyState(ioCommand.mID);
		}
	};

	// Sleep to make things slow (for debugging).
	// Note: use the command main input path as seed to make it consistent accross runs (useful if we want to add loading bars).
	if (mSlowMode)
		Sleep(100 + gRand32((uint32)gHash(ioCommand.GetMainInput().GetFile().mPath)) % 5000);

	// Make sure all inputs exist.
	{
		bool all_inputs_exist = true;
		for (FileID input_id : ioCommand.GetAllInputs())
		{
			const FileInfo& input = gFileSystem.GetFile(input_id);
			if (input.IsDeleted())
			{
				all_inputs_exist = false;
				output_str.AppendFormat("[error] Input missing: {}\n", input);
			}
		}

		if (!all_inputs_exist)
		{
			log_entry.mOutput       = output_str.AsStringView();
			log_entry.mCookingState = CookingState::Error;
			return;
		}
	}

	// Make sure the directories for all the outputs exist.
	{
		bool all_dirs_exist = true;
		for (FileID output_file : ioCommand.mOutputs)
		{
			bool success = gFileSystem.CreateDirectory(output_file);

			if (!success)
			{
				all_dirs_exist = false;
				output_str.AppendFormat("[error] Failed to create directory for {}\n", gFileSystem.GetFile(output_file));
			}
		}

		if (!all_dirs_exist)
		{
			log_entry.mOutput       = output_str.AsStringView();
			log_entry.mCookingState = CookingState::Error;
			return;
		}
	}

	// Fake random failures for debugging.
	if (gDebugFailCookingRandomly && (gRand32() % 5) == 0)
	{
		output_str.Append("Uh oh! This is a fake failure for debug purpose!\n");
		log_entry.mOutput       = output_str.AsStringView();
		log_entry.mCookingState = CookingState::Error;
		return;
	}

	
	// If there is a dep file command line, build it.
	Optional<String> dep_command_line;
	if (!rule.mDepFileCommandLine.Empty())
	{
		dep_command_line = gFormatCommandString(rule.mDepFileCommandLine, gFileSystem.GetFile(ioCommand.GetMainInput()));
		if (!dep_command_line)
		{
			output_str.Append("[error] Failed to format dep file command line.\n");
			log_entry.mOutput       = output_str.AsStringView();
			log_entry.mCookingState = CookingState::Error;
			return;
		}
	}

	bool success = false;
	if (rule.mCommandType == CommandType::CommandLine)
	{
		// Build the command line.
		Optional<String> command_line = gFormatCommandString(rule.mCommandLine, gFileSystem.GetFile(ioCommand.GetMainInput()));
		if (!command_line)
		{
			output_str.Append("[error] Failed to format command line.\n");
			log_entry.mOutput       = output_str.AsStringView();
			log_entry.mCookingState = CookingState::Error;
			return;
		}

		// Run the command line.
		success = sRunCommandLine(*command_line, output_str, mJobObject);
	}
	else
	{
		// Run the built-in command.
		switch (rule.mCommandType)
		{
		case CommandType::CopyFile:
			success = sRunCopyFile(ioCommand, output_str);
			break;
		default:
			gAssert(false);
			success = false;
			break;
		}
	}

	// If there's a dep file command line, run it next.
	if (success && dep_command_line)
	{
		output_str.Append("\nDep File "); // No end line on purpose, we want to prepend the line added inside the sRunCommandLine.
		success = sRunCommandLine(*dep_command_line, output_str, mJobObject);
	}

	// Set the end time and add the duration at the end of the log.
	log_entry.mTimeEnd = gGetSystemTimeAsFileTime();
	output_str.AppendFormat("\nDuration: {:.3f} seconds\n", (double)(log_entry.mTimeEnd - log_entry.mTimeStart) / 1'000'000'000.0);

	// Store the log output.
	log_entry.mOutput = output_str.AsStringView();

	if (!success)
	{
		log_entry.mCookingState = CookingState::Error;
	}
	else
	{

		// Now we wait for confirmation that the outputs were written (and if yes, it's a success).
		log_entry.mCookingState = CookingState::Waiting;

		// Any time an output is detected changed, we will try updating the cooking state.
		// If all outputs were written, cooking is a success.
		// If timeout happens first, we declare it's an error because of outputs not written.
		AddTimeOut(&log_entry);
	}

	// Make sure the file changes are processed as soon as possible (even if there was an error, there might be some files written).
	gFileSystem.KickMonitorDirectoryThread();
}


void CookingSystem::CleanupCommand(CookingCommand& ioCommand, CookingThread& ioThread)
{
	CookingLogEntry& log_entry = AllocateCookingLogEntry(ioCommand.mID);
	log_entry.mIsCleanup       = true;

	// Set the start time.
	log_entry.mTimeStart       = gGetSystemTimeAsFileTime();

	ioThread.mCurrentLogEntry  = log_entry.mID;
	defer { ioThread.mCurrentLogEntry = CookingLogEntryID::cInvalid(); };

	// Set the log entry on the command.
	ioCommand.mLastCookingLog = &log_entry;

	StringPool::ResizableStringView output_str = ioThread.mStringPool.CreateResizableString();

	bool error = false;
	for (FileID output_id : ioCommand.mOutputs)
	{
		if (gFileSystem.DeleteFile(output_id))
		{
			output_str.AppendFormat("Deleted {}\n", output_id.GetFile());
		}
		else
		{
			output_str.AppendFormat("[error] Failed to delete {}{}\n", output_id.GetRepo().mRootPath, output_id.GetFile().mPath);
			error = true;
		}
	}

	log_entry.mOutput       = output_str.AsStringView();
	log_entry.mTimeEnd      = gGetSystemTimeAsFileTime();

	if (error)
	{
		log_entry.mCookingState = CookingState::Error;
	}
	else
	{
		log_entry.mCookingState = CookingState::Waiting;
		AddTimeOut(&log_entry);

		// Make sure the file changes are processed as soon as possible.
		gFileSystem.KickMonitorDirectoryThread();
	}
}


void CookingSystem::AddTimeOut(CookingLogEntry* inLogEntry)
{
	{
		std::lock_guard lock(mTimeOutMutex);
		mTimeOutNextBatch.insert(inLogEntry);
	}

	// Tell the thread there are timeouts to process.
	mTimeOutAddedSignal.notify_one();
}


void CookingSystem::TimeOutUpdateThread(std::stop_token inStopToken)
{
	using namespace std::chrono_literals;

	gSetCurrentThreadName("TimeOut Update Thread");

	// The logic in this loop is a bit weird, but the idea is to wait *at least* this amount of time before declaring a command is in error.
	// Many commands will wait twice as much because they won't be in the first batch to be processed, but that's okay.
	constexpr auto cTimeout = 0.3s;

	while (true)
	{
		{
			std::unique_lock lock(mTimeOutMutex);

			// Wait until there are time outs to update.
			while (mTimeOutNextBatch.empty())
			{
				mTimeOutAddedSignal.wait(lock);

				if (inStopToken.stop_requested())
					return;
			}

			// Swap the buffers.
			std::swap(mTimeOutNextBatch, mTimeOutCurrentBatch);
		}

		do
		{
			// Wait for the time out.
			(void)mTimeOutTimerSignal.try_acquire_for(cTimeout);

			if (inStopToken.stop_requested())
				return;

			// If the file system is still busy, wait more. Otherwise we might incorrectly declare some commands are errors.
			// It can take a long time to process the events, especially if we need to open a lot of new files to get their path.
		} while (!gFileSystem.IsMonitoringIdle());
		
		// Declare all the unfinished commands in the current buffer as errored.
		{
			std::lock_guard lock(mTimeOutMutex);

			for (CookingLogEntry* log_entry : mTimeOutCurrentBatch)
			{
				// At this point if the state is still Waiting, we can consider it a failure: some outputs were not written.
				if (log_entry->mCookingState == CookingState::Waiting)
				{
					log_entry->mCookingState = CookingState::Error;

					// Update the total count of errors.
					mCookingErrors++;

					// Update the dirty state so that it's set to Error.
					QueueUpdateDirtyState(log_entry->mCommandID);
				}
			}

			mTimeOutCurrentBatch.clear();
		}
	}
}


void CookingSystem::QueueUpdateDirtyStates(FileID inFileID)
{
	// We want to queue/defer the update for several reasons:
	// - we don't want to update during the init scan (there's no point, we don't have all the files/all the info yet)
	// - we don't want to update commands while they are still running (again there's no point)
	// - we want a single thread doing it (because it's simpler)

	const FileInfo& file = inFileID.GetFile();

	if (file.mInputOf.Empty() && file.mOutputOf.Empty())
		return; // Early out if we know there will be nothing to do.

	std::lock_guard lock(mCommandsQueuedForUpdateDirtyStateMutex);

	for (CookingCommandID command_id : file.mInputOf)
		mCommandsQueuedForUpdateDirtyState.insert(command_id);
	for (CookingCommandID command_id : file.mOutputOf)
		mCommandsQueuedForUpdateDirtyState.insert(command_id);
}


void CookingSystem::QueueUpdateDirtyState(CookingCommandID inCommandID)
{
	std::lock_guard lock(mCommandsQueuedForUpdateDirtyStateMutex);
	mCommandsQueuedForUpdateDirtyState.insert(inCommandID);
}


bool CookingSystem::ProcessUpdateDirtyStates()
{
	std::lock_guard lock(mCommandsQueuedForUpdateDirtyStateMutex);

	for (auto it = mCommandsQueuedForUpdateDirtyState.begin(); it != mCommandsQueuedForUpdateDirtyState.end();)
	{
		CookingCommand& command = GetCommand(*it);

		if (command.mLastCookingLog && command.mLastCookingLog->mCookingState == CookingState::Cooking)
		{
			++it; // Still cooking, check again later.
		}
		else
		{
			// Update and remove from the list.
			command.UpdateDirtyState();
			it = mCommandsQueuedForUpdateDirtyState.erase(it);
		}
	}

	return !mCommandsQueuedForUpdateDirtyState.empty();
}


void CookingSystem::UpdateAllDirtyStates()
{
	std::lock_guard lock(mCommandsQueuedForUpdateDirtyStateMutex);

	for (CookingCommand& command : mCommands)
		command.UpdateDirtyState();

	mCommandsQueuedForUpdateDirtyState.clear();
}


void CookingSystem::ForceCook(CookingCommandID inCommandID)
{
	auto& command       = GetCommand(inCommandID);
	auto  cooking_state = command.GetCookingState();

	if (cooking_state == CookingState::Cooking || cooking_state == CookingState::Waiting)
		return; // Already cooking, don't do anything.

	// Remove it from the queue (if present) and add it at the front.
	mCommandsToCook.Remove(inCommandID);
	mCommandsToCook.Push(inCommandID, PushPosition::Front);
}


void CookingSystem::CookingThreadFunction(CookingThread* ioThread, std::stop_token inStopToken)
{
	gSetCurrentThreadName("CookingThread");

	while (true)
	{
		CookingCommandID command_id = mCommandsToCook.Pop();

		if (inStopToken.stop_requested())
			return;

		if (command_id.IsValid())
		{
			auto& command = GetCommand(command_id);
			if (command.mDirtyState & CookingCommand::AllStaticInputsMissing)
				CleanupCommand(command, *ioThread);
			else
				CookCommand(command, *ioThread);

			// Update the total count of errors.
			if (command.GetCookingState() == CookingState::Error)
				mCookingErrors++;

			// TODO is this ok or should we actually wait until the command is in success/error state rather than waiting state?
			mCommandsToCook.FinishedCooking(command_id);
		}
	}
}


CookingLogEntry& CookingSystem::AllocateCookingLogEntry(CookingCommandID inCommandID)
{
	auto             lock      = mCookingLog.Lock();

	CookingLogEntry& log_entry = mCookingLog.Emplace(lock);
	log_entry.mID              = { (uint32)mCookingLog.SizeRelaxed() - 1 };
	log_entry.mCommandID       = inCommandID;
	log_entry.mCookingState    = CookingState::Cooking;

	return log_entry;
}


bool CookingSystem::IsIdle() const
{
	// If there are things to cook, we're not idle.
	if (!mCommandsToCook.IsEmpty())
		return false;

	// If any worker is busy, we're not idle.
	for (auto& thread : mCookingThreads)
		if (thread.mCurrentLogEntry.load() != CookingLogEntryID::cInvalid())
			return false;

	// If any command is still waiting for its final status, we're not idle.
	{
		std::lock_guard lock(mTimeOutMutex);

		if (!mTimeOutCurrentBatch.empty())
				return false;
		if (!mTimeOutNextBatch.empty())
				return false;
	}

	// If we're still initializing, we're not idle.
	if (gFileSystem.GetInitState() != FileSystem::InitState::Ready)
		return false;

	// TODO check if log is changed (don't need to redraw if filesystem isn't idle, unless it prints)

	// Guess we're idle.
	return true;
}


void CookingSystem::UpdateNotifications()
{
	// The code below doesn't check if the cooking is finished because the queue is empty or because cooking is paused.
	// To make things simpler, never do notifications when cooking is paused (we don't need them anyway).
	if (IsCookingPaused())
		return;

	size_t cooking_log_size = mCookingLog.SizeRelaxed();

	// If no command was cooked since last time, nothing to do.
	if (mLastNotifCookingLogSize == cooking_log_size)
		return;

	// Wait some time between notifs, we don't want to spam.
	constexpr double cNotifPeriodSeconds = 10.0;
	int64            current_ticks       = gGetTickCount();
	if (mLastNotifTicks != 0 && gTicksToSeconds(current_ticks - mLastNotifTicks) < cNotifPeriodSeconds)
	{
		// Update the log size that we only generate a notif if something new is cooked (and not just time has passed).
		mLastNotifCookingLogSize = cooking_log_size;
		// Also update the number of cooking errors. It does mean that some errors will never get notified, but it
		// subjectively seems better than reporting potentially very old errors after a new command succeeded.
		mLastNotifCookingErrors  = mCookingErrors;
		return;
	}

	// Cooking system being idle is equivalent to cooking being finished.
	bool   cooking_is_finished = IsIdle();
	size_t error_count         = mCookingErrors - mLastNotifCookingErrors;

	if (cooking_is_finished)
	{
		if (error_count == 0)
		{
			size_t command_count = cooking_log_size - mLastNotifCookingLogSize;
			if (gShouldNotify(gApp.mEnableNotifOnCookingFinish))
				gNotifAdd(NotifType::Info, "Cooking finished!", FixedString128("{} {}.", command_count, command_count > 1 ? "commands" : "command"));
		}
		else
		{
			if (gShouldNotify(gApp.mEnableNotifOnCookingFinish) || gShouldNotify(gApp.mEnableNotifOnCookingError))
				gNotifAdd(NotifType::Error, "Cooking finished with errors.", FixedString128("{} {}.", error_count, error_count > 1 ? "errors" : "error"));
		}

		// Update the last notif values even if we didn't actually display a notif,
		// because we want the number of cooked commands to be correct if we re-enable the notifs.
		mLastNotifCookingErrors  = mCookingErrors;
		mLastNotifCookingLogSize = cooking_log_size;
		mLastNotifTicks          = current_ticks;
	}
	else if (error_count > 0)
	{
		if (gShouldNotify(gApp.mEnableNotifOnCookingError))
		{
			gNotifAdd(NotifType::Error, "Oh la la!", FixedString128("{} {}.", error_count, error_count > 1 ? "errors" : "error"));

			// Here however only update the last notif values if we actually display a notif,
			// because otherwise it might cause the next cooking finished notif to be skipped.
			mLastNotifCookingErrors  = mCookingErrors;
			mLastNotifCookingLogSize = cooking_log_size;
			mLastNotifTicks          = current_ticks;
		}
	}
}

