#include "ConfigReader.h"
#include "FileSystem.h"
#include "CookingSystem.h"
#include "App.h"
#include "TomlReader.h"


void gReadConfigFile(StringView inPath)
{
	gApp.Log(R"(Reading Config file "{}.")", inPath);

	// Parse the toml file.
	toml::parse_result config_toml = toml::parse_file(inPath);
	if (!config_toml)
	{
		gApp.LogError("Failed to parse Config file.");
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
	TempString512 rule_file_path;
	if (reader.TryRead("RuleFile", rule_file_path))
		gApp.mRuleFilePath = rule_file_path.AsStringView();

	// Start paused, or cook immediately?
	bool start_paused = gCookingSystem.IsCookingPaused();
	if (reader.TryRead("StartPaused", start_paused))
		gCookingSystem.SetCookingPaused(start_paused);

	// Number of Cooking Threads.
	int num_cooking_threads = 0;
	if (reader.TryRead("NumCookingThreads", num_cooking_threads))
		gCookingSystem.SetCookingThreadCount(num_cooking_threads);
}
