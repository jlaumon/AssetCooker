#include "CookingSystem.h"
#include "App.h"
#include "Debug.h"
#include "Notifications.h"
#include "Ticks.h"
#include "subprocess/subprocess.h"
#include "win32/misc.h"

// Debug toggle to fake cooking failures, to test error handling.
bool gDebugFailCookingRandomly = false;


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
	const char* p_begin = inFormatStr.data();
	const char* p_end   = gEndPtr(inFormatStr);
	const char* p       = p_begin;

	while (p != p_end)
	{
		if (*p == '{')
		{
			str.append({ p_begin, p });

			StringView arg = sParseArgument(p, p_end);

			// sParseArgument made p point after the argument itself.
			p_begin = p;

			if (gStartsWith(arg, gToStringView(CommandVariables::Repo)))
			{
				arg.remove_prefix(gToStringView(CommandVariables::Repo).size());

				if (arg.size() < 2 || arg[0] != ':')
					return {}; // Failed to get the repo name part.
			
				StringView repo_name = arg.substr(1);

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

	str.append({ p_begin, p });

	return str;
}


Optional<String> gFormatCommandString(StringView inFormatStr, const FileInfo& inFile)
{
	if (inFormatStr.empty())
		return {}; // Consider empty format string is an error.

	return sParseCommandVariables(inFormatStr, [&inFile](CommandVariables inVar, StringView inRepoName, StringView inRemainingFormatStr, String& outStr) 
	{
		switch (inVar)
		{
		case CommandVariables::Ext:
			outStr.append(inFile.GetExtension());
			break;
		case CommandVariables::File:
			outStr.append(inFile.GetNameNoExt());
			break;
		case CommandVariables::Dir:
			if (!inFile.GetDirectory().empty())
			{
				outStr.append(inFile.GetDirectory());

				// If the following character is a quote, the backslash at the end of the dir will escape it and the command line won't work.
				// Add a second backslash to avoid that.
				if (!inRemainingFormatStr.empty() && inRemainingFormatStr[0] == '"')
					outStr.append("\\");
			}
			break;
		case CommandVariables::Dir_NoTrailingSlash:
			if (!inFile.GetDirectory().empty())
				outStr.append(inFile.GetDirectory().substr(0, inFile.GetDirectory().size() - 1));
			break;
		case CommandVariables::FullPath:
			outStr.append(inFile.mPath);
			break;
		case CommandVariables::Repo:
			{
				FileRepo* repo = gFileSystem.FindRepo(inRepoName);

				// Invalid repo name.
				if (repo == nullptr)
					return false;

				outStr.append(repo->mRootPath);
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
			outStr.append(inFile.GetExtension());
			break;
		case CommandVariables::File:
			outStr.append(inFile.GetNameNoExt());
			break;
		case CommandVariables::Dir:
			outStr.append(inFile.GetDirectory());
			break;
		case CommandVariables::Dir_NoTrailingSlash:
			if (!inFile.GetDirectory().empty())
				outStr.append(inFile.GetDirectory().substr(0, inFile.GetDirectory().size() - 1));
			break;
		case CommandVariables::FullPath:
			outStr.append(inFile.mPath);
			break;
		case CommandVariables::Repo:
			// There can only be 1 Repo arg and it should be at the very beginning of the path.
			if (repo != nullptr || !outStr.empty())
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


template <typename taContainer, typename taPredicate>
bool gAnyOf(taContainer& inContainer, taPredicate inPredicate)
{
	for (auto& element : inContainer)
		if (inPredicate(element))
			return true;
	return false;
}


template <typename taContainer, typename taPredicate>
bool gNoneOf(taContainer& inContainer, taPredicate inPredicate)
{
	for (auto& element : inContainer)
		if (inPredicate(element))
			return false;
	return true;
}



bool InputFilter::Pass(const FileInfo& inFile) const
{
	if (mRepoIndex != inFile.mID.mRepoIndex)
		return false;

	if (!mExtensions.empty() && gNoneOf(mExtensions, [&](StringView inExtension) { return gIsEqualNoCase(inFile.GetExtension(), inExtension); }))
		return false;

	if (!mDirectoryPrefixes.empty() && gNoneOf(mDirectoryPrefixes, [&](StringView inDirectoryPrefix) { return gStartsWithNoCase(inFile.GetDirectory(), inDirectoryPrefix); }))
		return false;

	if (!mNamePrefixes.empty() && gNoneOf(mNamePrefixes, [&](StringView inNamePrefix) { return gStartsWithNoCase(inFile.GetNameNoExt(), inNamePrefix); }))
		return false;

	if (!mNameSuffixes.empty() && gNoneOf(mNameSuffixes, [&](StringView inNameSuffix) { return gEndsWithNoCase(inFile.GetNameNoExt(), inNameSuffix); }))
		return false;

	return true;
}


FileID CookingCommand::GetDepFile() const
{
	if (gCookingSystem.GetRule(mRuleID).UseDepFile())
		return mOutputs[0];
	else
		return FileID::cInvalid();
}


void CookingCommand::UpdateDirtyState()
{
	// Dirty state should not be updated while still cooking!
	gAssert(!mLastCookingLog || mLastCookingLog->mCookingState > CookingState::Cooking);

	DirtyState dirty_state = NotDirty;

	USN last_cook = mLastCook;

	// If we don't have a last cook USN, estimate one.
	// Normally this should be stored in the cooking database and read on start up,
	// but when doing an initial scan we use the oldest output (ie. min USN) as the
	// probable last point when the command was cooked.
	if (last_cook == 0 && !mOutputs.empty())
	{
		last_cook = cMaxUSN;
		for (auto& file_id : mOutputs)
			last_cook = gMin(last_cook, file_id.GetFile().mLastChangeUSN);
	}

	bool all_input_missing = true;
	for (FileID file_id : mInputs)
	{
		const FileInfo& file = file_id.GetFile();

		if (file.IsDeleted())
		{
			dirty_state |= InputMissing;
		}
		else
		{
			all_input_missing = false;

			if (file.mLastChangeUSN > last_cook)
				dirty_state |= InputChanged;
		}
	}
	
	if (all_input_missing)
		dirty_state |= AllInputsMissing;

	bool all_output_written = true;
	bool all_output_missing = true;
	for (FileID file_id : mOutputs)
	{
		const FileInfo& file = file_id.GetFile();

		if (file.IsDeleted())
			dirty_state |= OutputMissing;
		else
			all_output_missing = false;

		if (file.mLastChangeUSN < mLastCook)
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



void CookingCommand::ReadDepFile()
{
	// TODO
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
		bucket.mCommands.push_back(inCommandID);
	else
		bucket.mCommands.insert(bucket.mCommands.begin(), inCommandID);

	mTotalSize++;
}


CookingCommandID CookingQueue::Pop()
{
	std::lock_guard lock(mMutex);

	// Find the first non-empty bucket.
	for (PrioBucket& bucket : mPrioBuckets)
	{
		if (!bucket.mCommands.empty())
		{
			// Pop a command.
			CookingCommandID id = bucket.mCommands.back();
			bucket.mCommands.pop_back();
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
		bucket_it->mCommands.erase(it);
	}
	else
	{
		std::swap(*it, bucket_it->mCommands.back());
		bucket_it->mCommands.pop_back();
	}

	mTotalSize--;
	return true;
}


void CookingQueue::Clear()
{
	std::lock_guard lock(mMutex);

	for (PrioBucket& bucket : mPrioBuckets)
		bucket.mCommands.clear();

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
	gAssert(mPrioData.size() == mPrioBuckets.size());

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
		for (int prio_index = 0; prio_index < (int)mPrioBuckets.size(); ++prio_index)
		{
			PrioBucket& bucket = mPrioBuckets[prio_index];
			PrioData&   data   = mPrioData[prio_index];

			// If this bucket is empty but some commands are still being cooked, wait until they're finished before checking the next buckets.
			if (bucket.mCommands.empty() && data.mCommandsBeingCooked > 0)
			{
				// Go back to waiting and start over if more work is added or these commands are finished.
				break;
			}

			if (!bucket.mCommands.empty())
			{
				// Found one!
				non_empty_bucket_index = (int)(&bucket - &mPrioBuckets.front());
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
		CookingCommandID id     = bucket.mCommands.back();
		bucket.mCommands.pop_back();
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
		int index = (int)(&*bucket_it - &mPrioBuckets.front());
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
				if (!dep_file.IsValid())
					success = false;
			}

			// Add the main input file.
			std::vector<FileID> inputs;
			inputs.push_back(ioFile.mID);

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

			// If there is a dep file, consider it an output.
			std::vector<FileID> outputs;
			if (rule.UseDepFile())
				outputs.push_back(dep_file);

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
				command.mInputs         = std::move(inputs);
				command.mOutputs        = std::move(outputs);
			}
		}

		// Let all the input and ouputs know that they are referenced by this command.
		{
			const CookingCommand& command = GetCommand(command_id);

			for (FileID file_id : command.mInputs)
				gFileSystem.GetFile(file_id).mInputOf.push_back(command.mID);

			for (FileID file_id : command.mOutputs)
				gFileSystem.GetFile(file_id).mOutputOf.push_back(command.mID);
		}

		// TODO: add validation
		// - a file cannot be the input/output of the same command
		// - all the inputs of a command can only be outputs of commands with lower prio (ie. that build before)

		// Check if we need to continue to try more rules for this file.
		if (!rule.mMatchMoreRules)
			break;
	}	
}


bool CookingSystem::ValidateRules()
{
	HashSet<StringView> all_names;
	int                 errors = 0;

	for (size_t rule_index = 0; rule_index < mRules.size(); ++rule_index)
	{
		const CookingRule& rule = mRules[rule_index];

		// Validate the name.
		if (!rule.mName.empty())
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
			gApp.LogError(R"(Rule[{}] has no name)", rule_index);
		}

		// Validate the input filters.
		for (size_t i = 0; i < rule.mInputFilters.size(); ++i)
		{
			const InputFilter& filter = rule.mInputFilters[i];

			// Make sure there's at least one way to filter.
			if (filter.mExtensions.empty() && 
				filter.mDirectoryPrefixes.empty() && 
				filter.mNamePrefixes.empty() && 
				filter.mNameSuffixes.empty())
			{
				errors++;
				gApp.LogError(R"(Rule {}, InputFilter[{}] needs at least one way of filtering the inputs (by extension, etc.).)", rule.mName, i);
			}

			if (!filter.mExtensions.empty())
			{
				for (StringView extension : filter.mExtensions)
					if (!gStartsWith(extension, "."))
					{
						errors++;
						gApp.LogError(R"(Rule {}, InputFilter[{}]: Extension "{}" must start with a ".")", rule.mName, i, extension);
					}
			}
		}

		// Dummy file info use to test path formatting.
		FileInfo dummy_file(FileID{ 0, 0 }, "dir\\dummy.txt", Hash128{ 0, 0 }, FileType::File, {});

		// Validate the command line.
		if (!gFormatCommandString(rule.mCommandLine, dummy_file))
		{
			errors++;
			gApp.LogError(R"(Rule {}: Failed to parse CommandLine "{}")", rule.mName, rule.mCommandLine);
		}

		// Validate the dep file path.
		if (rule.UseDepFile())
		{
			if (!gFormatCommandString(rule.mDepFilePath, dummy_file))
			{
				errors++;
				gApp.LogError(R"(Rule {}: Failed to parse DepFilePath "{}")", rule.mName, rule.mDepFilePath);
			}
		}

		// Validate the input paths.
		for (size_t i = 0; i < rule.mInputPaths.size(); ++i)
		{
			if (!gFormatCommandString(rule.mInputPaths[i], dummy_file))
			{
				errors++;
				gApp.LogError(R"(Rule {}: Failed to parse InputPaths[{}] "{}")", rule.mName, i, rule.mInputPaths[i]);
			}
		}

		// Validate the output paths.
		for (size_t i = 0; i < rule.mOutputPaths.size(); ++i)
		{
			if (!gFormatCommandString(rule.mOutputPaths[i], dummy_file))
			{
				errors++;
				gApp.LogError(R"(Rule {}: Failed to parse OutputPaths[{}] "{}")", rule.mName, i, rule.mOutputPaths[i]);
			}
		}
	}

	return errors == 0;
}


void CookingSystem::StartCooking()
{
	// Zero/negative means no limit on thread count.
	int thread_count = (mWantedCookingThreadCount <= 0) ? INT_MAX : mWantedCookingThreadCount;

	// Number of threads is at least one, and is capped by number of CPU cores minus one,
	// because we want to leave one core for the file system monitoring thread (and main thread).
	thread_count = gClamp(thread_count, 1, (int)std::thread::hardware_concurrency() - 1);

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

	mTimeOutUpdateThread.request_stop();
	mTimeOutAddedSignal.notify_one();
	mTimeOutTimerSignal.release();
	mTimeOutUpdateThread.join();
}


void CookingSystem::SetCookingPaused(bool inPaused)
{
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

			if (cooking_state == CookingState::Error && (command.mDirtyState & CookingCommand::InputChanged) == 0)
				continue; // Skip commands that errored if their input hasn't changed since last time.

			mCommandsToCook.Push(command_id);
		}
	}
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

	// Get the max USN of all inputs (to later know if this command needs to cook again).
	USN max_input_usn = 0;
	for (FileID input_id : ioCommand.mInputs)
		max_input_usn = gMax(max_input_usn, gFileSystem.GetFile(input_id).mLastChangeUSN);
	
	// Set the inputs USN on the command.
	ioCommand.mLastCook = max_input_usn;

	defer
	{
		// If the command ends in error, make sure its state is updated (that normally happens when outputs are written, but that might not happen depending on the error).
		// This is important to then properly detect when the inputs change again and the command can re-cook.
		if (log_entry.mCookingState == CookingState::Error)
			QueueUpdateDirtyState(ioCommand.mID);
	};

	// Sleep to make things slow (for debugging).
	// Note: use the command main input path as seed to make it consistent accross runs (useful if we want to add loading bars).
	if (mSlowMode)
		Sleep(100 + gRand32((uint32)gHash(ioCommand.GetMainInput().GetFile().mPath)) % 5000);

	// Build the command line.
	const CookingRule& rule         = gCookingSystem.GetRule(ioCommand.mRuleID);
	Optional<String>   command_line = gFormatCommandString(rule.mCommandLine, gFileSystem.GetFile(ioCommand.GetMainInput()));
	if (!command_line)
	{
		log_entry.mOutput       = ioThread.mStringPool.AllocateCopy("[error] Failed to format command line.\n");
		log_entry.mCookingState = CookingState::Error;
		return;
	}

	StringPool::ResizableStringView output_str     = ioThread.mStringPool.CreateResizableString();
	output_str.AppendFormat("Command Line: {}\n\n", StringView(*command_line));

	// Make sure all inputs exist.
	{
		bool all_inputs_exist = true;
		for (FileID input_id : ioCommand.mInputs)
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
		output_str.Append("Uh oh! This is a fake failure for debug purpose!");
		log_entry.mOutput       = output_str.AsStringView();
		log_entry.mCookingState = CookingState::Error;
		return;
	}

	// Create the process for the command line.
	subprocess_s process;
	int options = subprocess_option_no_window | subprocess_option_combined_stdout_stderr | subprocess_option_single_string_command_line;

	// Not strictly needed but some launched processes fail to read files if not used (access rights issues? unclear).
	options |= subprocess_option_inherit_environment;

	const char*  command_line_array[] = { command_line->data(), nullptr };
	if (subprocess_create(command_line_array, options, &process))
	{
		output_str.AppendFormat("[error] Failed to create process - {}\n", GetLastErrorString());

		log_entry.mOutput       = output_str.AsStringView();
		log_entry.mCookingState = CookingState::Error;
		return;
	}

	// Wait for the process to finish.
	int exit_code = 0;
	bool got_exit_code = subprocess_join(&process, &exit_code) == 0;
	

	// Get the output.
	// TODO: use the async API to get the output before the process is finished? but may need an extra thread to read stderr
	{
		FILE* p_stdout = subprocess_stdout(&process);
		char  buffer[16384];
		while (true)
		{
			size_t bytes_read  = fread(buffer, 1, sizeof(buffer) - 1, p_stdout);
			buffer[bytes_read] = 0;

			if (bytes_read == 0)
				break;

			output_str.Append({ buffer, bytes_read });
		}
	}

	// Log the exit code if we have it.
	if (!got_exit_code)
	{
		output_str.AppendFormat("[error] Failed to get exit code - {}\n", GetLastErrorString());

		log_entry.mCookingState = CookingState::Error;
	}
	else
	{
		output_str.AppendFormat("\nExit code: {} (0x{:X})\n", exit_code, (uint32)exit_code);
	}

	// Non-zero exit code is considered an error.
	// TODO make that optional in the Rule
	if (exit_code != 0)
		log_entry.mCookingState = CookingState::Error;
	
	// Destruct the process handles (this can't actually fail).
	subprocess_destroy(&process);

	// Set the end time and add the duration at the end of the log.
	log_entry.mTimeEnd = gGetSystemTimeAsFileTime();
	output_str.AppendFormat("\nDuration: {:.3f} seconds\n", (double)(log_entry.mTimeEnd - log_entry.mTimeStart) / 1'000'000'000.0);

	// Store the log output.
	log_entry.mOutput = output_str.AsStringView();

	if (log_entry.mCookingState != CookingState::Error)
	{
		// Now we wait for confirmation that the outputs were written (and if yes, it's a success).
		log_entry.mCookingState = CookingState::Waiting;

		// Any time an output is detected changed, we will try updating the cooking state.
		// If all outputs were written, cooking is a success.
		// If timeout happens first, we declare it's an error because of outputs not written.
		AddTimeOut(&log_entry);
	}

	// Make sure the file changes are processed as soon as possible.
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
			output_str.AppendFormat("Deleted {}{}", output_id.GetRepo().mRootPath, output_id.GetFile().mPath);
		}
		else
		{
			output_str.AppendFormat("[error] Failed to delete {}{}", output_id.GetRepo().mRootPath, output_id.GetFile().mPath);
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

	gSetCurrentThreadName(L"TimeOut Update Thread");

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

	std::lock_guard lock(mCommandsQueuedForUpdateDirtyStateMutex);

	for (auto command_id : file.mInputOf)
		mCommandsQueuedForUpdateDirtyState.insert(command_id);
	for (auto command_id : file.mOutputOf)
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


void CookingSystem::UpdateDirtyStates()
{
	std::lock_guard lock(mCommandsQueuedForUpdateDirtyStateMutex);

	for (CookingCommand& command : mCommands)
		command.UpdateDirtyState();
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
	gSetCurrentThreadName(L"CookingThread");

	while (true)
	{
		CookingCommandID command_id = mCommandsToCook.Pop();

		if (inStopToken.stop_requested())
			return;

		if (command_id.IsValid())
		{
			auto& command = GetCommand(command_id);
			if (command.mDirtyState & CookingCommand::AllInputsMissing)
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
				gNotifAdd(NotifType::Info, "Cooking finished!", TempString128("{} {}.", command_count, command_count > 1 ? "commands" : "command"));
		}
		else
		{
			if (gShouldNotify(gApp.mEnableNotifOnCookingFinish) || gShouldNotify(gApp.mEnableNotifOnCookingError))
				gNotifAdd(NotifType::Error, "Cooking finished with errors.", TempString128("{} {}.", error_count, error_count > 1 ? "errors" : "error"));
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
			gNotifAdd(NotifType::Error, "Oh la la!", TempString128("{} {}.", error_count, error_count > 1 ? "errors" : "error"));

			// Here however only update the last notif values if we actually display a notif,
			// because otherwise it might cause the next cooking finished notif to be skipped.
			mLastNotifCookingErrors  = mCookingErrors;
			mLastNotifCookingLogSize = cooking_log_size;
			mLastNotifTicks          = current_ticks;
		}
	}
}
