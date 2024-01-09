#include "CookingSystem.h"
#include "App.h"
#include "Debug.h"
#include "subprocess/subprocess.h"


// Return a StringView on the text part of {text}.
static StringView sParseArgument(const char*& ioPtr, const char* inPtrEnd)
{
	gAssert(*ioPtr == '{');
	ioPtr++;
	const char* arg_begin = ioPtr;
	const char* arg_end   = ioPtr;

	while (ioPtr != inPtrEnd)
	{
		if (*ioPtr == '}')
		{
			arg_end = ioPtr;
			ioPtr++;
			break;
		}

		ioPtr++;
	}

	return { arg_begin, arg_end };
}


template<class taFormatter>
static std::optional<String> sParseCommandVariables(StringView inFormatStr, const taFormatter& inFormatter)
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

				if (!inFormatter(CommandVariables::Repo, repo_name, str))
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
						if (!inFormatter(var, "", str))
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


std::optional<String> gFormatCommandString(StringView inFormatStr, const FileInfo& inFile)
{
	if (inFormatStr.empty())
		return {}; // Consider empty format string is an error.

	return sParseCommandVariables(inFormatStr, [&inFile](CommandVariables inVar, StringView inRepoName, String& outStr) 
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


std::optional<RepoAndFilePath> gFormatFilePath(StringView inFormatStr, const FileInfo& inFile)
{
	FileRepo*             repo = nullptr;
	std::optional<String> path = sParseCommandVariables(inFormatStr, [&inFile, &repo](CommandVariables inVar, StringView inRepoName, String& outStr) 
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
	std::optional repo_and_path = gFormatFilePath(inFormatStr, inFile);
	if (!repo_and_path)
		return FileID::cInvalid();

	auto& [repo, path] = *repo_and_path;
	return repo.GetOrAddFile(path, FileType::File, {}).mID;
}



bool InputFilter::Pass(const FileInfo& inFile) const
{
	if (mRepoIndex != inFile.mID.mRepoIndex)
		return false;

	if (!mExtension.empty() && !gIsEqualNoCase(inFile.GetExtension(), mExtension))
		return false;

	if (!mDirectoryPrefix.empty() && !gStartsWithNoCase(inFile.GetDirectory(), mDirectoryPrefix))
		return false;

	if (!mNamePrefix.empty() && !gStartsWithNoCase(inFile.GetNameNoExt(), mNamePrefix))
		return false;

	if (!mNameSuffix.empty() && !gEndsWithNoCase(inFile.GetNameNoExt(), mNameSuffix))
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
	DirtyState dirty_state = NotDirty;

	for (FileID file_id : mInputs)
	{
		const FileInfo& file = gFileSystem.GetFile(file_id);

		if (file.IsDeleted())
			dirty_state |= InputMissing;
		else if (file.mLastChange > mLastCook)
			dirty_state |= InputChanged;
	}

	for (FileID file_id : mOutputs)
	{
		const FileInfo& file = gFileSystem.GetFile(file_id);

		if (file.IsDeleted())
			dirty_state |= OutputMissing;
	}

	mDirtyState = dirty_state;

	// If dirty, add it to the cooking queue.
	// Note: Add it even if there are missing inputs. They might be created by earlier priority commands, and if not that's an error and we want to know about it.
	if (IsDirty())
		gCookingQueue.Push(mID);
}


void CookingCommand::ReadDepFile()
{
	// TODO
}


void CookingQueue::Push(CookingCommandID inCommandID)
{
	const CookingCommand& command = gCookingSystem.GetCommand(inCommandID);
	int priority = gCookingSystem.GetRule(command.mRuleID).mPriority;

	// TODO: check if the command is already in the queue/already cooking?

	{
		std::lock_guard lock(mMutex);

		// Find or add the bucket for that cooking priority.
		PrioBucket& bucket = *gEmplaceSorted(mPrioBuckets, priority);

		// Add the command.
		bucket.mCommands.push_back(inCommandID);
		mTotalSize++;
	}

	gCookingSystem.KickOneCookingThread();
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
				std::lock_guard lock(mCommandsMutex);

				// Build the ID now that we have the mutex.
				command_id              = CookingCommandID{ (uint32)mCommands.size() };

				// Create the command.
				CookingCommand& command = mCommands.emplace_back();
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
			if (filter.mExtension.empty() && 
				filter.mDirectoryPrefix.empty() && 
				filter.mNamePrefix.empty() && 
				filter.mNameSuffix.empty())
			{
				errors++;
				gApp.LogError(R"(Rule {}, InputFilter[{}] needs at least one way of filtering the inputs (by extension, etc.).)", rule.mName, i, filter.mExtension);
			}

			if (!filter.mExtension.empty() && !gStartsWith(filter.mExtension, "."))
			{
				errors++;
				gApp.LogError(R"(Rule {}, InputFilter[{}]: Extension "{}" must start with a ".")", rule.mName, i, filter.mExtension);
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
	// TODO: add more threads
	int thread_count = 1;

	gApp.Log("Starting {} Cooking Threads.", thread_count);

	mCookingThreads.resize(thread_count);

	// Start the cooking thread.
	for (auto& thread : mCookingThreads)
		thread.mThread = std::jthread(std::bind_front(&CookingSystem::CookingThreadFunction, this, &thread));
}


void CookingSystem::StopCooking()
{
	for (auto& thread : mCookingThreads)
		thread.mThread.request_stop();

	mCookingQueueSemaphore.release(mCookingThreads.size());

	for (auto& thread : mCookingThreads)
		thread.mThread.join();
}


void CookingSystem::SetCookingPaused(bool inPaused)
{
	if (inPaused == mCookingPaused)
		return;

	if (inPaused)
	{
		mCookingPaused = true;
	}
	else
	{
		// Unpause and wake all the threads.
		mCookingPaused = false;
		mCookingResumeSemaphore.release(mCookingThreads.size());
	}
}


void CookingSystem::KickOneCookingThread()
{
	mCookingQueueSemaphore.release();
}


void CookingSystem::CookCommand(CookingCommand& ioCommand, StringPool& ioStringPool)
{
	CookingLogEntry& log_entry = AllocateCookingLogEntry(ioCommand.mID);

	// Set the log entry on the command.
	ioCommand.mLastCookingLog       = &log_entry;

	// Build the command line.
	const CookingRule& rule         = gCookingSystem.GetRule(ioCommand.mRuleID);
	std::optional      command_line = gFormatCommandString(rule.mCommandLine, gFileSystem.GetFile(ioCommand.GetMainInput()));
	if (!command_line)
	{
		log_entry.mOutput       = ioStringPool.AllocateCopy("[error] Failed to format command line.\n");
		log_entry.mCookingState = CookingState::Error;
		ioCommand.mCookingState = CookingState::Error;
		return;
	}

	StringPool::ResizableStringView output_str     = ioStringPool.CreateResizableString();
	output_str.AppendFormat("Command Line: {}\n", StringView(*command_line));

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
			log_entry.mCookingState = CookingState::Error;
			ioCommand.mCookingState = CookingState::Error;
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
			log_entry.mCookingState = CookingState::Error;
			ioCommand.mCookingState = CookingState::Error;
			return;
		}
	}

	// Get the max USN of all inputs (to later know if this command needs to cook again).
	USN max_input_usn = 0;
	for (FileID input_id : ioCommand.mInputs)
		max_input_usn = gMax(max_input_usn, gFileSystem.GetFile(input_id).mLastChange);

	// Subprocess wants each arg in a different string to then concatenate them... a bit silly but whatevs, let's split it.
	std::vector<const char*> command_line_array;
	{
		const char* arg_begin = command_line->data();
		for (char& c : *command_line)
		{
			if (c == ' ' || c == '\t' || c == 'v')
				c = 0;

			command_line_array.push_back(arg_begin);
			arg_begin = &c + 1;
		}

		if (arg_begin != gEndPtr(*command_line))
			command_line_array.push_back(arg_begin);

		command_line_array.push_back(nullptr);
	}

	// Create the process for the command line.
	subprocess_s process;
	if (subprocess_create(command_line_array.data(), subprocess_option_no_window | subprocess_option_combined_stdout_stderr, &process))
	{
		output_str.AppendFormat("[error] Failed to create process - {}\n", GetLastErrorString());
		log_entry.mCookingState = CookingState::Error;
		ioCommand.mCookingState = CookingState::Error;
		return;
	}

	// Wait for the process to finish.
	int exit_code = 0;
	if (subprocess_join(&process, &exit_code))
	{
		output_str.AppendFormat("[error] Failed to get exit code - {}\n", GetLastErrorString());
		log_entry.mCookingState = CookingState::Error;
		ioCommand.mCookingState = CookingState::Error;
	}

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
	
	// Destruct the process handles (this can't actually fail).
	subprocess_destroy(&process);

	// Set the inputs USN on the command.
	ioCommand.mLastCook = max_input_usn;

	// Now we wait for confirmation that the outputs were written (and if yes, it's a success).
	log_entry.mCookingState = CookingState::Waiting;
	ioCommand.mCookingState = CookingState::Waiting;

	// Any time an output is detected changed, we will try updating the cooking state.
	// If all outputs were written, cooking is a success.
	// If timeout happens first, we declare it's an error because of outputs not written.
	AddToWaitList(ioCommand.mID);
}


void CookingSystem::AddToWaitList(CookingCommandID inCommandID)
{
	std::lock_guard lock(mWaitingCommandsMutex);

	int next_index = (mWaitingCommandCurrentIndex + 1) % (int)mWaitingCommands.size();
	mWaitingCommands[next_index].insert(inCommandID);
}


// TODO add something that calls this
// TODO also add the call that sets the cooking state to success on the file event side
//#error need to draw cooking log first to debug?
void CookingSystem::ProcessWaitList()
{
	std::lock_guard lock(mWaitingCommandsMutex);

	HashSet<CookingCommandID>& waiting_commands = mWaitingCommands[mWaitingCommandCurrentIndex];

	for (CookingCommandID command_id : waiting_commands)
	{
		CookingCommand& command = GetCommand(command_id);

		// At this point if the state is still Waiting, we can consider it a failure: some outputs were not written.
		if (command.mCookingState != CookingState::Waiting)
		{
			command.mCookingState                  = CookingState::Error;
			command.mLastCookingLog->mCookingState = CookingState::Error;
		}
	}

	waiting_commands.clear();
	mWaitingCommandCurrentIndex = (mWaitingCommandCurrentIndex + 1) % (int)mWaitingCommands.size();
}



void CookingSystem::CookingThreadFunction(CookingThread* ioThread, std::stop_token inStopToken)
{
	while (true)
	{
		mCookingQueueSemaphore.acquire();

		if (inStopToken.stop_requested())
			return;

		// If cooking is paused, wait on the resume semaphore.
		if (mCookingPaused)
		{
			mCookingResumeSemaphore.acquire();

			if (inStopToken.stop_requested())
				return;
		}

		CookingCommandID command_id = gCookingQueue.Pop();
		if (command_id.IsValid())
		{
			CookCommand(GetCommand(command_id), ioThread->mStringPool);
		}
	}
}


CookingLogEntry& CookingSystem::AllocateCookingLogEntry(CookingCommandID inCommandID)
{
	std::lock_guard lock(mCookingLogMutex);

	CookingLogEntry& log_entry = mCookingLog.emplace_back();
	log_entry.mCommandID       = inCommandID;
	log_entry.mCookingState    = CookingState::Cooking;

	return log_entry;
}
