// SPDX-License-Identifier: Unlicense
#ifndef ASSET_COOKER_API_H
#define ASSET_COOKER_API_H

// https://github.com/jlaumon/AssetCooker

#if defined(__cplusplus)
extern "C" {
#endif

struct AssetCookerInternal;
typedef struct AssetCookerInternal* AssetCookerHandle;


enum AssetCookerOptions
{
	AssetCookerOption_StartMinimized = 0x1,
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