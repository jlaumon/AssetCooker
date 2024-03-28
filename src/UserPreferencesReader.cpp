#include "ConfigReader.h"
#include "FileSystem.h"
#include "CookingSystem.h"
#include "App.h"
#include "TomlReader.h"
#include "Paths.h"
#include "UI.h"

#include <win32/file.h>

static bool sFileExists(StringView inPath)
{
	DWORD attributes = GetFileAttributesA(inPath.AsCStr());
	return attributes != INVALID_FILE_ATTRIBUTES;
}


template <typename taEnumType>
bool gStringViewToEnum(StringView inStrValue, taEnumType& outValue)
{
	for (int i = 0; i < (int)taEnumType::_Count; ++i)
	{
		taEnumType value = (taEnumType)i;
		if (gIsEqualNoCase(inStrValue, gToStringView(value)))
		{
			outValue = value;
			return true;
		}
	}

	return false;
}


void gReadUserPreferencesFile(StringView inPath)
{
	if (!sFileExists(inPath))
		return; // It's fine if that file doesn't exist, it's optional.

	gApp.Log(R"(Reading User Preferences file "{}".)", inPath);

	// Parse the toml file.
	toml::parse_result prefs_toml = toml::parse_file(inPath);
	if (!prefs_toml)
	{
		gApp.LogError(R"(Failed to parse User Preferences file "{}".)", inPath);
		gApp.LogError("{}", prefs_toml.error());
		gApp.SetInitError(TempString512(R"(Failed to parse User Preferences file "{}". See log for details.)", inPath).AsStringView());
		return;
	}

	// Initialize a reader on the root table.
	TomlReader reader(prefs_toml.table(), nullptr);

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

	// Number of Cooking Threads.
	{
		int num_cooking_threads = 0;
		if (reader.TryRead("NumCookingThreads", num_cooking_threads))
			gCookingSystem.SetCookingThreadCount(num_cooking_threads);
	}

	// Filesystem log verbosity.
	{
		TempString64 log_level_str;
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
		TempString64 enable_str;
		if (reader.TryRead("EnableNotifOnHideWindow", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifOnHideWindow);
	}
	{
		TempString64 enable_str;
		if (reader.TryRead("EnableNotifOnCookingError", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifOnCookingError);
	}
	{
		TempString64 enable_str;
		if (reader.TryRead("EnableNotifOnCookingFinish", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifOnCookingFinish);
	}
	{
		TempString64 enable_str;
		if (reader.TryRead("EnableNotifSound", enable_str))
			gStringViewToEnum(enable_str, gApp.mEnableNotifSound);
	}
}


void gWriteUserPreferencesFile(StringView inPath)
{
	FILE* prefs_file = fopen(inPath.AsCStr(), "wt");
	if (prefs_file == nullptr)
	{
		gApp.LogError(R"(Failed to save User Preferences file ("{}") - {} (0x{:X}))", inPath, strerror(errno), errno);
		return;
	}

	toml::table prefs_toml;

	prefs_toml.insert("StartPaused", gCookingSystem.IsCookingPaused());
	prefs_toml.insert("NumCookingThreads", gCookingSystem.GetCookingThreadCount());
	prefs_toml.insert("LogFSActivity", std::string_view(gToStringView(gApp.mLogFSActivity)));
	prefs_toml.insert("UIScale", gUIGetUserScale());

	prefs_toml.insert("HideWindowOnMinimize", gApp.mHideWindowOnMinimize);
	prefs_toml.insert("EnableNotifOnHideWindow", std::string_view(gToStringView(gApp.mEnableNotifOnHideWindow)));
	prefs_toml.insert("EnableNotifOnCookingError", std::string_view(gToStringView(gApp.mEnableNotifOnCookingError)));
	prefs_toml.insert("EnableNotifOnCookingFinish", std::string_view(gToStringView(gApp.mEnableNotifOnCookingFinish)));
	prefs_toml.insert("EnableNotifSound", std::string_view(gToStringView(gApp.mEnableNotifSound)));

	std::stringstream sstream;
	sstream << prefs_toml;
	std::string str = sstream.str();

	size_t written_size = fwrite(str.c_str(), 1, str.size(), prefs_file);
	if (written_size != str.size())
		gApp.LogError(R"(Failed to save User Preferences file ("{}") - {} (0x{:X}))", inPath, strerror(errno), errno);

	fclose(prefs_file);
}