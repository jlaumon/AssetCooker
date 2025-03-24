/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "ConfigReader.h"
#include "App.h"
#include "CookingSystem.h"
#include "FileSystem.h"
#include "TomlReader.h"

void gReadConfigFile(StringView inPath)
{
	TempString configPath = gGetAbsolutePath(inPath);
	gAppLog(R"(Reading Config file "%s".)", configPath.AsCStr());

	// Parse the toml file.
	TomlReader reader;
	if (!reader.Init(configPath, &gCookingSystem.GetStringPool()))
	{
		gApp.SetInitError(gTempFormat(R"(Failed to parse Config file "%s". See log for details.)", configPath.AsCStr()));
		return;
	}

	defer
	{
		// At the end if there were any error, tell the app to not start.
		if (reader.mErrorCount)
			gApp.SetInitError("Failed to parse Config file. See log for details.");
	};

	// Look for the repos.
	if (reader.OpenArray("Repo"))
	{
		defer { reader.CloseArray(); };

		while (reader.NextArrayElement())
		{
			// Each repo should be a table.
			if (!reader.OpenTable(""))
				continue;

			defer { reader.CloseTable(); };

			TempString name;
			reader.Read("Name", name);

			TempString path;
			reader.Read("Path", path);

			if (!name.Empty() && !path.Empty())
				gFileSystem.AddRepo(name, path);
		}
	}

	// Read the Rule File path.
	{
		TempString rule_file_path;
		if (reader.TryRead("RuleFile", rule_file_path))
			gApp.mRuleFilePath = rule_file_path;
		gApp.mRuleFilePath = gGetAbsolutePath(gApp.mRuleFilePath);
	}

	// Log directory path
	{
		TempString log_dir;
		if (reader.TryRead("LogDirectory", log_dir))
		{
			// Normalize the path.
			gNormalizePath(log_dir);

			// If there's a trailing slash, remove it.
			if (log_dir.EndsWith("\\"))
				log_dir.RemoveSuffix(1);

			gApp.mLogDirectory = log_dir;
		}
		gApp.mLogDirectory = gGetAbsolutePath(gApp.mLogDirectory);
	}

	// Cache directory path
	{
		TempString cache_dir;
		if (reader.TryRead("CacheDirectory", cache_dir))
		{
			// Normalize the path.
			gNormalizePath(cache_dir);

			// If there's a trailing slash, remove it.
			if (cache_dir.EndsWith("\\"))
				cache_dir.RemoveSuffix(1);

			gApp.mCacheDirectory = cache_dir;
		}
		gApp.mCacheDirectory = gGetAbsolutePath(gApp.mCacheDirectory);
	}

	// Read the window title.
	reader.TryRead("WindowTitle", gApp.mMainWindowTitle);
}
