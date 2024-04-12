#include "RuleReader.h"
#include "CookingSystem.h"
#include "App.h"
#include "TomlReader.h"


void gReadRuleFile(StringView inPath)
{
	gApp.Log(R"(Reading Rule file "{}".)", inPath);

	// Parse the toml file.
	toml::parse_result rules_toml = toml::parse_file(inPath);
	if (!rules_toml)
	{
		gApp.LogError(R"(Failed to parse Rule file "{}".)", inPath);
		gApp.LogError("{}", rules_toml.error());
		gApp.SetInitError(TempString512(R"(Failed to parse Rule file "{}". See log for details.)", inPath).AsStringView());
		return;
	}

	// Initialize a reader on the root table.
	TomlReader reader(rules_toml.table(), &gCookingSystem.GetStringPool());

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

				InputFilter& input_filter = rule.mInputFilters.emplace_back();

				TempString512 repo_name;
				if (reader.Read("Repo", repo_name))
				{
					// Try to find the repo from the name.
					FileRepo* repo = gFileSystem.FindRepo(repo_name.AsStringView());
					if (repo == nullptr)
					{
						gApp.LogError(R"(Repo "{}" not found.)", repo_name.AsStringView());
						reader.mErrorCount++;
					}
					else
					{
						input_filter.mRepoIndex = repo->mIndex;
					}
				}

				reader.TryReadArray("Extensions",			input_filter.mExtensions);
				reader.TryReadArray("DirectoryPrefixes",	input_filter.mDirectoryPrefixes);
				reader.TryReadArray("NamePrefixes",			input_filter.mNamePrefixes);
				reader.TryReadArray("NameSuffixes",			input_filter.mNameSuffixes);
			}
		}

		// Check the type of command (either command line or a built-in command).
		{
			TempString64 cmd_type;
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

			TempString64 format;
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
