// SPDX-License-Identifier: Unlicense
#ifndef ASSET_COOKER_API_H
#define ASSET_COOKER_API_H

// https://github.com/jlaumon/AssetCooker

#if defined(__cplusplus)
extern "C" {
#endif

struct AssetCookerInternal;
typedef struct AssetCookerInternal* AssetCookerHandle;
#define AssetCookerHandle_Invalid NULL

enum AssetCookerOptions
{
	AssetCookerOption_StartMinimized	= 1 << 0,	// Start with the window minimized (or hidden depending on Asset Cooker's settings).
	AssetCookerOption_StartPaused		= 1 << 1,	// Start with cooking paused.
	AssetCookerOption_StartUnpaused		= 1 << 2,	// Start with cooking unpaused.
};

int AssetCooker_Launch(const char* inExePath, const char* inConfigFilePath, int inOptions, AssetCookerHandle* ouHandle);
int AssetCooker_Detach(AssetCookerHandle* ioHandlePtr);
int AssetCooker_Kill(AssetCookerHandle* ioHandlePtr);
int AssetCooker_Pause(AssetCookerHandle inHandle, int inPause);
int AssetCooker_ShowWindow(AssetCookerHandle inHandle);
int AssetCooker_IsAlive(AssetCookerHandle inHandle);
int AssetCooker_IsIdle(AssetCookerHandle inHandle);
int AssetCooker_IsPaused(AssetCookerHandle inHandle);
int AssetCooker_HasErrors(AssetCookerHandle inHandle);
int AssetCooker_WaitForIdle(AssetCookerHandle inHandle);


#if defined(__cplusplus)
} // extern "C"
#endif


#endif // ASSET_COOKER_API_H