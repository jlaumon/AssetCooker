#include "ConfigReader.h"
#include "FileSystem.h"
#include "CookingSystem.h"
#include "App.h"
#include "TomlReader.h"
#include "Paths.h"


void gReadConfigFile(StringView inPath)
{
	gApp.Log(R"(Reading Config file "{}".)", inPath);

	// Parse the toml file.
	toml::parse_result config_toml = toml::parse_file(inPath);
	if (!config_toml)
	{
		gApp.LogError(R"(Failed to parse Config file "{}".)", inPath);
		gApp.LogError("{}", config_toml.error());
		gApp.SetInitError(TempString512(R"(Failed to parse Config file "{}". See log for details.)", inPath).AsStringView());
		return;
	}

	// Initialize a reader on the root table.
	TomlReader reader(config_toml.table(), gCookingSystem.GetRuleStringPool());

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

	// Start paused, or cook immediately?
	// TODO: move this to a user preferences config file
	{
		bool start_paused = gCookingSystem.IsCookingPaused();
		if (reader.TryRead("StartPaused", start_paused))
			gCookingSystem.SetCookingPaused(start_paused);
	}

	// Number of Cooking Threads.
	// TODO: move this to a user preferences config file
	{
		int num_cooking_threads = 0;
		if (reader.TryRead("NumCookingThreads", num_cooking_threads))
			gCookingSystem.SetCookingThreadCount(num_cooking_threads);
	}

	// Filesystem log verbosity.
	{
		TempString64 log_level;
		if (reader.TryRead("LogFSActivity", log_level))
		{
			StringView log_level_sv = log_level.AsStringView();
			if (gIsEqualNoCase(log_level_sv, "None"))
				gApp.mLogFSActivity = LogLevel::None;
			else if (gIsEqualNoCase(log_level_sv, "Normal"))
				gApp.mLogFSActivity = LogLevel::Normal;
			else if (gIsEqualNoCase(log_level_sv, "Verbose"))
				gApp.mLogFSActivity = LogLevel::Verbose;
		}
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

			gApp.mLogDirectory = cache_dir.AsStringView();
		}
	}

	// Read the window title.
	reader.TryRead("WindowTitle", gApp.mMainWindowTitle);
}
