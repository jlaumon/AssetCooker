// SPDX-License-Identifier: Unlicense
#ifndef ASSET_COOKER_API_H
#define ASSET_COOKER_API_H

// This API can be used to control Asset Cooker from an external process.
// For example, your game can use it to launch Asset Cooker on start-up and wait for
// all assets to be cooked.
// More info about Asset Cooker on github: https://github.com/jlaumon/AssetCooker
// API Version 1.0.0
//
// Changelog:
// - 1.0.0: Initial version!

#if defined(__cplusplus)
extern "C" {
#endif

struct AssetCookerInternal;
typedef struct AssetCookerInternal* AssetCookerHandle; ///< Handle to an Asset Cooker instance.
#define AssetCookerHandle_Invalid NULL

enum AssetCookerOptions
{
	AssetCookerOption_StartMinimized	= 1 << 0,	///< Start with the window minimized (or hidden depending on Asset Cooker's settings).
	AssetCookerOption_StartPaused		= 1 << 1,	///< Start with cooking paused.
	AssetCookerOption_StartUnpaused		= 1 << 2,	///< Start with cooking unpaused.
};

/// @brief Launch an instance of Asset Cooker and create a handle to communicate with it.
/// If an Asset Cooker instance already exists, attach to it instead.
int AssetCooker_Launch(const char* inExePath, const char* inConfigFilePath, int inOptions, AssetCookerHandle* ouHandle);

/// @brief Detach from the Asset Cooker instance without killing it, and destroy the handle.
/// The handle is set to AssetCookerHandle_Invalid.
/// @param ioHandlePtr A pointer to the Asset Cooker instance handle.
int AssetCooker_Detach(AssetCookerHandle* ioHandlePtr);

/// @brief Kill the Asset Cooker instance, and destroy the handle.
/// The handle is set to AssetCookerHandle_Invalid.
/// @param ioHandlePtr A pointer to the Asset Cooker instance handle.
int AssetCooker_Kill(AssetCookerHandle* ioHandlePtr);

/// @brief Pause or unpause cooking.
/// @param inHandle The Asset Cooker instance handle.
/// @param inPause Pass 1 to pause, 0 to unpause.
/// @return Zero on success.
int AssetCooker_Pause(AssetCookerHandle inHandle, int inPause);

/// @brief Open/show the window of the Asset Cooker instance.
/// @param inHandle The Asset Cooker instance handle.
/// @return Zero on success.
int AssetCooker_ShowWindow(AssetCookerHandle inHandle);

/// @brief Check if the Asset Cooker instance is alive.
/// @param inHandle The Asset Cooker instance handle.
/// @return Non-zero if alive.
/// @note To re-create an instance, first call AssetCooker_Detach to destroy the current handle,
/// then call AssetCooker_Lauch to create a new instance and new handle.
int AssetCooker_IsAlive(AssetCookerHandle inHandle);

/// @brief Check if the Asset Cooker instance is idle (ie. no cooking is happening).
/// @param inHandle The Asset Cooker instance handle.
/// @return Non-zero if idle.
int AssetCooker_IsIdle(AssetCookerHandle inHandle);

/// @brief Check if the Asset Cooker instance is paused.
/// @param inHandle The Asset Cooker instance handle.
/// @return Non-zero if paused.
int AssetCooker_IsPaused(AssetCookerHandle inHandle);

/// @brief Check if the Asset Cooker instance encountered cooking errors.
/// @param inHandle The Asset Cooker instance handle.
/// @return Non-zero if has errors.
int AssetCooker_HasErrors(AssetCookerHandle inHandle);

/// @brief Wait for the Asset Cooker instance to become idle.
/// @param inHandle The Asset Cooker instance handle.
/// @return Zero on success.
int AssetCooker_WaitForIdle(AssetCookerHandle inHandle);


#if defined(__cplusplus)
} // extern "C"
#endif


#endif // ASSET_COOKER_API_H