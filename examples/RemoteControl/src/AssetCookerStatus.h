// SPDX-License-Identifier: Unlicense
#pragma once

#include <AssetCookerAPI.h>
#include <imgui.h>

// Small widget used to display the status of Asset Cooker
// and control it from a separate process.
// This is meant to be an example usage of the API, not a fully featured tool.
struct AssetCookerStatus
{
	// Fill these variables before use.
	char		 mExecutablePath[260]			= {};	 // The path to the Asset Cooker executable.
	char		 mConfigFilePath[260]			= {};	 // The path to the config file (config.toml).
	ImTextureRef mIcon							= {};	 // The icon this widget will use. Load asset-cooker.png and put it in here.
	bool		 mStartMinimized				= false; // If true, Asset Cooker will start with its window minimized (or hidden, depending on user settings).

	AssetCookerStatus()							= default;
	AssetCookerStatus(const AssetCookerStatus&) = delete;
	~AssetCookerStatus();

	void Launch();							   // Launch Asset Cooker.
	void Draw(ImVec2 inSize = { 64.f, 64.f }); // Draw this widget.
private:
	AssetCookerHandle mHandle		 = nullptr;
	float			  mIconAngle	 = 0.f;
	float			  mIconIdleTimer = 0.f;
};
