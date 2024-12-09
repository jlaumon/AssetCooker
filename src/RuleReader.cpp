/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "RuleReader.h"
#include "CookingSystem.h"
#include "App.h"
#include "TomlReader.h"
#include "LuaReader.h"


template <typename taReaderType>
static void sReadRuleFile(StringView inPath)
{
	gApp.Log(R"(Reading Rule file "{}".)", inPath);

	// Parse the file.
	taReaderType reader;
	if (!reader.Init(inPath, &gCookingSystem.GetStringPool()))
	{
		gApp.SetInitError(gTempFormat(R"(Failed to parse Rule file "%s". See log for details.)", inPath.AsCStr()));
		return;
	}

	defer
	{
		// At the end if there were any error, tell the app to not start.
		if (reader.mErrorCount)
			gApp.SetInitError("Failed to parse Rule file. See log for details.");
	};

	// Look for the rules.
	if (!reader.OpenArray("Rule"))
		return;

	while (reader.NextArrayElement())
	{
		// Each rule should be a table.
		if (!reader.OpenTable(""))
			continue;

		defer { reader.CloseTable(); };

		CookingRule& rule = gCookingSystem.AddRule();

		reader.Read("Name", rule.mName);

		if (reader.OpenArray("InputFilters"))
		{
			defer { reader.CloseArray(); };

			while (reader.NextArrayElement())
			{
				if (!reader.OpenTable(""))
					continue;
				defer { reader.CloseTable(); };

				InputFilter& input_filter = rule.mInputFilters.EmplaceBack();

				TempString repo_name;
				if (reader.Read("Repo", repo_name))
				{
					// Try to find the repo from the name.
					FileRepo* repo = gFileSystem.FindRepo(repo_name);
					if (repo == nullptr)
					{
						gApp.LogError(R"(Repo "{}" not found.)", repo_name.AsCStr());
						reader.mErrorCount++;
					}
					else
					{
						input_filter.mRepoIndex = repo->mIndex;
					}
				}

				TempString path_pattern;
				if (reader.Read("PathPattern", path_pattern))
				{
					// Normalize the path to get rid of any forward slash.
					gNormalizePath(path_pattern);

					input_filter.mPathPattern = reader.mStringPool->AllocateCopy(path_pattern);
				}
			}
		}

		// Check the type of command (either command line or a built-in command).
		{
			TempString cmd_type;
			reader.TryRead("CommandType", cmd_type);
			gStringViewToEnum(cmd_type,			rule.mCommandType);
		}

		if (rule.mCommandType == CommandType::CommandLine)
		{
			reader.Read    ("CommandLine",		rule.mCommandLine);
		}
		else
		{
			reader.NotAllowed("CommandLine", "because CommandType isn't CommandLine");
			reader.NotAllowed("DepFile",	 "because CommandType isn't CommandLine");
		}

		reader.TryRead     ("Priority",			rule.mPriority);
		reader.TryRead     ("Version",			rule.mVersion);
		reader.TryRead     ("MatchMoreRules",	rule.mMatchMoreRules);
		reader.TryReadArray("InputPaths",		rule.mInputPaths);
		reader.TryReadArray("OutputPaths",		rule.mOutputPaths);

		if (reader.TryOpenTable("DepFile"))
		{
			reader.Read    ("Path",				rule.mDepFilePath);

			TempString format;
			reader.Read("Format", format);
			gStringViewToEnum(format,			rule.mDepFileFormat);
			
			reader.CloseTable();

			// Only read the dep file command line if there is a dep file.
			reader.TryRead("DepFileCommandLine", rule.mDepFileCommandLine);
		}
		else
		{
			reader.NotAllowed("DepFileCommandLine", "because DepFile isn't provided");
		}
	}

	// Validate the rules.
	if (!gCookingSystem.ValidateRules())
		gApp.SetInitError("Rules validation failed. See log for details.");
}


void gReadRuleFile(StringView inPath)
{
	if (gEndsWithNoCase(inPath, ".toml"))
		sReadRuleFile<TomlReader>(inPath);
	else if (gEndsWithNoCase(inPath, ".lua"))
		sReadRuleFile<LuaReader>(inPath);
	else
		gApp.SetInitError("Rule file is an unknown format (recognized extensions are .yaml, .yml, .toml and .lua).");
}