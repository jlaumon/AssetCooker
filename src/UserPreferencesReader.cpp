/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "FileSystem.h"
#include "CookingSystem.h"
#include "App.h"
#include "TomlReader.h"
#include "UI.h"


void gReadUserPreferencesFile(StringView inPath)
{
	if (!gFileExists(inPath))
		return; // It's fine if that file doesn't exist, it's optional.

	gAppLog(R"(Reading User Preferences file "%s".)", inPath.AsCStr());

	// Parse the toml file.
	TomlReader reader;
	if (!reader.Init(inPath, &gCookingSystem.GetStringPool()))
	{
		gApp.SetInitError(gTempFormat(R"(Failed to parse User Preferences file "%s". See log for details.)", inPath.AsCStr()));
		return;
	}

	defer
	{
		// At the end if there were any error, tell the app to not start.
		if (reader.mErrorCount)
			gApp.SetInitError("Failed to parse User Preferences file. See log for details.");
	};

	// Start paused, or cook immediately?
	{
		bool start_paused = gCookingSystem.IsCookingPaused();
		if (reader.TryRead("StartPaused", start_paused))
			gCookingSystem.SetCookingPaused(start_paused);
	}

	// Start minimized (or hidden to tray icon)
	{
		bool start_minimized = gApp.mStartMinimized;
		if (reader.TryRead("StartMinimized", start_minimized))
			gApp.mStartMinimized = start_minimized;
	}

	// Number of Cooking Threads.
	{
		int num_cooking_threads = 0;
		if (reader.TryRead("NumCookingThreads", num_cooking_threads))
			gCookingSystem.SetCookingThreadCount(num_cooking_threads);
	}

	// Filesystem log verbosity.
	{
		TempString log_level_str;
		if (reader.TryRead("LogFSActivity", log_level_str))
			gStringViewToEnum(log_level_str, gApp.mLogFSActivity);
	}

	// UI scale.
	{
		float ui_scale = 1.f;
		if (reader.TryRead("UIScale", ui_scale))
			gUISetUserScale(ui_scale);
	}

	reader.TryRead("HideWindowOnMinimize", gApp.mHideWindowOnMinimize);

	// Notifications.
	{
		TempString enable_str;
		if (reader.TryRead("EnableNotifOnHideWindow", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifOnHideWindow);
	}
	{
		TempString enable_str;
		if (reader.TryRead("EnableNotifOnCookingError", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifOnCookingError);
	}
	{
		TempString enable_str;
		if (reader.TryRead("EnableNotifOnCookingFinish", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifOnCookingFinish);
	}
	{
		TempString enable_str;
		if (reader.TryRead("EnableNotifSound", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifSound);
	}
}


void gWriteUserPreferencesFile(StringView inPath)
{
	FILE* prefs_file = fopen(inPath.AsCStr(), "wt");
	if (prefs_file == nullptr)
	{
		gAppLogError(R"(Failed to save User Preferences file ("%s") - %s (0x%X))", 
			inPath.AsCStr(), strerror(errno), errno);
		return;
	}

	toml::table prefs_toml;

	prefs_toml.insert("StartPaused", gCookingSystem.IsCookingPaused());
	prefs_toml.insert("StartMinimized", gApp.mStartMinimized);
	prefs_toml.insert("NumCookingThreads", gCookingSystem.GetCookingThreadCount());
	prefs_toml.insert("LogFSActivity", std::string_view(gToStringView(gApp.mLogFSActivity).AsCStr()));
	prefs_toml.insert("UIScale", gUIGetUserScale());

	prefs_toml.insert("HideWindowOnMinimize", gApp.mHideWindowOnMinimize);
	prefs_toml.insert("EnableNotifOnHideWindow", std::string_view(gToStringView(gApp.mEnableNotifOnHideWindow).AsCStr()));
	prefs_toml.insert("EnableNotifOnCookingError", std::string_view(gToStringView(gApp.mEnableNotifOnCookingError).AsCStr()));
	prefs_toml.insert("EnableNotifOnCookingFinish", std::string_view(gToStringView(gApp.mEnableNotifOnCookingFinish).AsCStr()));
	prefs_toml.insert("EnableNotifSound", std::string_view(gToStringView(gApp.mEnableNotifSound).AsCStr()));

	std::stringstream sstream;
	sstream << prefs_toml;
	std::string str = sstream.str();

	size_t written_size = fwrite(str.c_str(), 1, str.size(), prefs_file);
	if (written_size != str.size())
		gAppLogError(R"(Failed to save User Preferences file ("%s") - %s (0x%X))", inPath.AsCStr(), strerror(errno), errno);

	fclose(prefs_file);
}