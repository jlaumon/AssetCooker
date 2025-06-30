// SPDX-License-Identifier: Unlicense
#ifndef ASSET_COOKER_API_H
#define ASSET_COOKER_API_H

// https://github.com/jlaumon/AssetCooker

#if defined(__cplusplus)
extern "C" {
#endif

struct asset_cooker_s;
typedef struct asset_cooker_s* asset_cooker_handle;


enum assetcooker_option_e
{
	assetcooker_option_start_minimized = 0x1,
};


int asset_cooker_launch(const char* exe_path, const char* config_file_path, int options, asset_cooker_handle* out_handle);
int asset_cooker_detach(asset_cooker_handle* handle_ptr);
int asset_cooker_kill(asset_cooker_handle* handle_ptr);
int asset_cooker_pause(asset_cooker_handle handle, int pause);
int asset_cooker_show_window(asset_cooker_handle handle);
int asset_cooker_is_alive(asset_cooker_handle handle);
int asset_cooker_is_idle(asset_cooker_handle handle);
int asset_cooker_is_paused(asset_cooker_handle handle);
int asset_cooker_has_errors(asset_cooker_handle handle);
int asset_cooker_wait_for_idle(asset_cooker_handle handle);


#if defined(__cplusplus)
} // extern "C"
#endif


#endif // ASSET_COOKER_API_H