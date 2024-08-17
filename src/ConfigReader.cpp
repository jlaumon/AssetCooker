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
	gApp.Log(R"(Reading Config file "{}".)", inPath);

	// Parse the toml file.
	TomlReader reader;
	if (!reader.Init(inPath, &gCookingSystem.GetStringPool()))
	{
		gApp.SetInitError(TempString512(R"(Failed to parse Config file "{}". See log for details.)", inPath).AsStringView());
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

			TempString512 name;
			reader.Read("Name", name);

			TempString512 path;
			reader.Read("Path", path);

			if (name.mSize && path.mSize)
				gFileSystem.AddRepo(name.AsStringView(), path.AsStringView());
		}
	}

	// Read the Rule File path.
	{
		TempString512 rule_file_path;
		if (reader.TryRead("RuleFile", rule_file_path))
			gApp.mRuleFilePath = rule_file_path.AsStringView();
	}

	// Log directory path
	{
		TempString512 log_dir;
		if (reader.TryRead("LogDirectory", log_dir))
		{
			// Normalize the path.
			gNormalizePath({ log_dir.mBuffer, log_dir.mSize });

			// If there's a trailing slash, remove it.
			if (gEndsWith(log_dir, "\\"))
			{
				log_dir.mSize--;
				log_dir.mBuffer[log_dir.mSize] = 0;
			}

			gApp.mLogDirectory = log_dir.AsStringView();
		}
	}

	// Cache directory path
	{
		TempString512 cache_dir;
		if (reader.TryRead("CacheDirectory", cache_dir))
		{
			// Normalize the path.
			gNormalizePath({ cache_dir.mBuffer, cache_dir.mSize });

			// If there's a trailing slash, remove it.
			if (gEndsWith(cache_dir, "\\"))
			{
				cache_dir.mSize--;
				cache_dir.mBuffer[cache_dir.mSize] = 0;
			}

			gApp.mCacheDirectory = cache_dir.AsStringView();
		}
	}

	// Read the window title.
	reader.TryRead("WindowTitle", gApp.mMainWindowTitle);
}
