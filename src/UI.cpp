#include "App.h"
#include "UI.h"

#include "FileSystem.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"

ImGuiStyle gStyle               = {};

struct UIScale
{
	static constexpr float cMin = 0.4f;
	static constexpr float cMax = 3.0f;

	float mFromDPI      = 1.0f;
	float mFromSettings = 1.0f;
	bool  mNeedUpdate   = true;

	float GetFinalScale() const { return mFromDPI * mFromSettings; }
};

UIScale gUIScale;


// Set the DPI scale.
void gUISetDPIScale(float inDPIScale)
{
	if (inDPIScale == gUIScale.mFromDPI)
		return;

	gUIScale.mFromDPI = inDPIScale;
	gUIScale.mNeedUpdate = true;
}


// Set the user setting scale.
void gUISetScale(float inScale)
{
	float scale = gClamp(inScale, UIScale::cMin, UIScale::cMax);
	if (scale == gUIScale.mFromSettings)
		return;

	gUIScale.mFromSettings = scale;
	gUIScale.mNeedUpdate   = true;
}


void gUIUpdate()
{
	if (gUIScale.mNeedUpdate)
	{
		gUIScale.mNeedUpdate = false;

		ImGui::GetStyle() = gStyle;
		ImGui::GetStyle().ScaleAllSizes(gUIScale.GetFinalScale());

		auto& io = ImGui::GetIO();

		// Remove all the font data.
		io.Fonts->Clear();
		// Release the DX11 objects (font textures, but also everything else... might not be the most efficient).
		ImGui_ImplDX11_InvalidateDeviceObjects();

		// Reload the font at the new scale.
		io.Fonts->AddFontFromFileTTF("thirdparty/imgui/misc/fonts/Cousine-Regular.ttf", 14.0f * gUIScale.GetFinalScale());
		// Re-create the DX11 objects.
		ImGui_ImplDX11_CreateDeviceObjects();
	}
}


void gUIDrawMainMenuBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit", "Alt + F4"))
			{
				
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Settings"))
		{
			float ui_scale = gUIScale.mFromSettings;
			if (ImGui::DragFloat("UI Scale", &ui_scale, 0.01f, UIScale::cMin, UIScale::cMax, "%.1f"))
				gUISetScale(ui_scale);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Debug"))
		{
			if (ImGui::BeginMenu("Log FileSystem Activity"))
			{
				const char* log_levels[] = { "None", "Basic", "Verbose" };
				static_assert(gElemCount(log_levels) == (size_t)LogLevel::Verbose + 1);

				int current_log_level = (int)gApp.mLogFSActivity;

				if (ImGui::ListBox("##Verbosity", &current_log_level, log_levels, gElemCount(log_levels)))
					gApp.mLogFSActivity = (LogLevel)current_log_level;

				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}


void gUIDrawMain()
{
	// Make a fullscreen window.
	ImGui::SetNextWindowPos({ 0, 0 });
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin(cAppName, nullptr, 
		ImGuiWindowFlags_NoMove | 
		ImGuiWindowFlags_NoResize | 
		ImGuiWindowFlags_NoTitleBar | 
		ImGuiWindowFlags_NoFocusOnAppearing | 
		ImGuiWindowFlags_NoBringToFrontOnFocus);

	ImGui::End();

	gApp.DrawLog();
}
