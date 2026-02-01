// SPDX-License-Identifier: Unlicense

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include "AssetCookerStatus.h"

#include <imgui_internal.h>
#include <math.h>


AssetCookerStatus::~AssetCookerStatus()
{
	AssetCooker_Detach(&mHandle);
}

void AssetCookerStatus::Launch()
{
	// If there already was a handle, destroy it.
	AssetCooker_Detach(&mHandle);

	int options = AssetCookerOption_StartUnpaused;
	if (mStartMinimized)
		options |= AssetCookerOption_StartMinimized;

	// Launch Asset Cooker
	AssetCooker_Launch(mExecutablePath, mConfigFilePath, options, &mHandle);

	// Reset internal values.
	mIconAngle	   = 0.f;
	mIconIdleTimer = 0.f;
}

void AssetCookerStatus::Draw(ImVec2 inSize)
{
	mIconIdleTimer += ImGui::GetIO().DeltaTime;

	ImGui::Begin("Asset Cooker Status Bar", nullptr,
				 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking);

	bool is_alive	= AssetCooker_IsAlive(mHandle);
	bool is_idle	= AssetCooker_IsIdle(mHandle);
	bool has_errors = AssetCooker_HasErrors(mHandle);
	bool is_paused	= AssetCooker_IsPaused(mHandle);

	bool want_spin	= is_alive && !is_idle;
	if (want_spin)
		mIconIdleTimer = 0;

	// Update the rotation of the icon.
	if (want_spin || mIconAngle != 0.f) // If we're already spinning, always continue until we reach angle zero.
	{
		constexpr float pi				= 3.14159265358979323846f;

		float			base_rot_speed	= 2.5f * pi;
		float			extra_rot_speed = (cosf(mIconAngle * 0.5f + pi) + 1.0f) * 1.2f * pi;

		float			angle_before	= mIconAngle;

		mIconAngle += (base_rot_speed + extra_rot_speed) * ImGui::GetIO().DeltaTime;
		mIconAngle = fmodf(mIconAngle, 4.0f * pi);

		// Keep spinning until we've made a full cycle, then stop.
		if (!want_spin && mIconAngle < angle_before)
			mIconAngle = 0.f;
	}

	ImU32 color_alive  = IM_COL32(255, 255, 255, 255);
	ImU32 color_dead   = IM_COL32(100, 100, 100, 255);
	ImU32 color_errors = IM_COL32(255, 100, 100, 255);

	// Choose a color depending on status.
	ImU32 color = 0;
	if (!is_alive)
		color = color_dead;
	else if (has_errors)
		color = color_errors;
	else
		color = color_alive;

	// If the icon was idle for long enough, fade it out.
	constexpr float fadeout_start	  = 1.0f;
	constexpr float fadeout_duration  = 0.5f;
	constexpr float fadeout_min_alpha = 0.5f;
	if (mIconIdleTimer > fadeout_start)
	{
		float fadeout_strength = (mIconIdleTimer - fadeout_start) / fadeout_duration;
		if (fadeout_strength > 1.0f)
			fadeout_strength = 1.0f;

		ImColor color4f(color);
		color4f.Value.w = 1.0f - (1.0f - fadeout_min_alpha) * fadeout_strength;

		color			= color4f;
	}

	// Draw the icon.
	{
		ImVec2 p1	  = ImGui::GetCursorScreenPos();
		ImVec2 p2	  = { p1.x + inSize.x, p1.y };
		ImVec2 p3	  = { p1.x + inSize.x, p1.y + inSize.y };
		ImVec2 p4	  = { p1.x, p1.y + inSize.y };

		ImVec2 center = (p1 + p3) / 2.0f;

		float  cos_a  = cosf(mIconAngle);
		float  sin_a  = sinf(mIconAngle);

		p1			  = ImRotate(p1 - center, cos_a, sin_a) + center;
		p2			  = ImRotate(p2 - center, cos_a, sin_a) + center;
		p3			  = ImRotate(p3 - center, cos_a, sin_a) + center;
		p4			  = ImRotate(p4 - center, cos_a, sin_a) + center;

		ImGui::GetWindowDrawList()->AddImageQuad(mIcon, p1, p2, p3, p4, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, color);
	}

	// Add an invisible button where the icon is.
	bool clicked = ImGui::InvisibleButton("AssetCookerIcon", inSize);

	// Add a popup menu if right-clicking the icon.
	if (ImGui::BeginPopupContextItem())
	{
		mIconIdleTimer = 0;

		if (is_alive)
		{
			if (ImGui::MenuItem("Show Window"))
				AssetCooker_ShowWindow(mHandle);

			if (is_paused)
			{
				if (ImGui::MenuItem("Unpause"))
					AssetCooker_Pause(mHandle, false);
			}
			else
			{
				if (ImGui::MenuItem("Pause"))
					AssetCooker_Pause(mHandle, true);
			}
		}
		else
		{
			if (ImGui::MenuItem("Launch"))
				Launch();

			ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
			ImGui::MenuItem("Start Minimized", {}, &mStartMinimized);
			ImGui::PopItemFlag();
		}

		ImGui::EndPopup();
	}

	// Add a tooltip when hovering the icon.
	if (ImGui::IsItemHovered())
	{
		mIconIdleTimer = 0;

		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0))
		{
			ImGui::StartMouseMovingWindow(ImGui::GetCurrentWindow());
		}

		// Choose a tootip depending on status.
		const char* tooltip = "";
		if (!is_alive)
			tooltip = "Not running";
		else if (has_errors)
		{
			if (is_idle)
				tooltip = "Cooking errors!";
			else
				tooltip = "Cooking errors! But still cooking...";
		}
		else if (is_paused)
			tooltip = "Cooking paused";
		else if (!is_idle)
			tooltip = "Cooking...";
		else
			tooltip = "All caught up!";

		ImGui::SetTooltip(tooltip);
	}

	if (clicked)
	{
		mIconIdleTimer = 0;

		if (is_alive)
			AssetCooker_ShowWindow(mHandle);
	}

	ImGui::End();
}
