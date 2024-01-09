#include "App.h"
#include "UI.h"

#include "FileSystem.h"
#include "CookingSystem.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "win32/misc.h"

ImGuiStyle gStyle               = {};


bool       gDrawAppLog          = true;
bool       gDrawCookingQueue    = true;

constexpr const char* cAppLogName          = "Main Log";
constexpr const char* cCookingQueueName    = "Cooking Queue";


std::span<const uint8> gGetEmbeddedFont(StringView inName)
{
	HRSRC   resource             = FindResourceA(nullptr, inName.AsCStr(), "FONTFILE");
	DWORD   resource_data_size   = SizeofResource(nullptr, resource);
	HGLOBAL resource_data_handle = LoadResource(nullptr, resource);
	auto    resource_data        = (const uint8*)LockResource(resource_data_handle);

	return { resource_data, resource_data_size };
}

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

		// Fonts are embedded in the exe, they don't need to be released.
		ImFontConfig font_config; 
		font_config.FontDataOwnedByAtlas = false;

		// Reload the fonts at the new scale.
		// The main font.
		{
			auto cousine_ttf = gGetEmbeddedFont("cousine_regular");
			io.Fonts->AddFontFromMemoryTTF((void*)cousine_ttf.data(), (int)cousine_ttf.size(), 14.0f * gUIScale.GetFinalScale(), &font_config);
		}

		// The icons font.
		{
			ImFontConfig icons_config          = font_config;
			icons_config.MergeMode             = true; // Merge inot the default font.
			icons_config.GlyphOffset.y         = 3.f * gUIScale.GetFinalScale();

			static const ImWchar icon_ranges[] = { ICON_MIN_CI, ICON_MAX_CI, 0 };

			auto codicon_ttf = gGetEmbeddedFont("codicon");
			io.Fonts->AddFontFromMemoryTTF((void*)codicon_ttf.data(), (int)codicon_ttf.size(), 14.0f * gUIScale.GetFinalScale(), &icons_config, icon_ranges);
		}

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
				gApp.RequestExit();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem(cAppLogName, nullptr, &gDrawAppLog);
			ImGui::MenuItem(cCookingQueueName, nullptr, &gDrawCookingQueue);

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
	ImGui::Begin("Background", nullptr, 
		ImGuiWindowFlags_NoMove | 
		ImGuiWindowFlags_NoResize | 
		ImGuiWindowFlags_NoTitleBar | 
		ImGuiWindowFlags_NoFocusOnAppearing | 
		ImGuiWindowFlags_NoBringToFrontOnFocus);

	ImGui::End();

	if (gDrawAppLog)
		gApp.mLog.Draw(cAppLogName, &gDrawAppLog);

	if (gDrawCookingQueue)
		gUIDrawCookingQueue();
}


void gDrawFileInfoSpan(StringView inListName, std::span<const FileID> inFileIDs);
void gDrawCookingCommandSpan(StringView inListName, std::span<const CookingCommandID> inCommandIDs);


void gUIDrawFileInfo(const FileInfo& inFile)
{
	ImGui::PushID(TempString32("File {}", inFile.mID.AsUInt()).AsCStr());
	defer { ImGui::PopID(); };

	TempString512 label("{}", inFile);
	bool       clicked = ImGui::Selectable(label.AsCStr(), false, ImGuiSelectableFlags_DontClosePopups);
	bool       open    = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

	if (open)
		ImGui::OpenPopup("Popup");

	if (ImGui::BeginPopup("Popup"))
	{
		ImGui::Text(label);

		ImGui::SmallButton("Open Dir");
		ImGui::SameLine();
		ImGui::SmallButton("Open File");

		ImGui::SeparatorText("Details");

		if (ImGui::BeginTable("File Details", 2))
		{
			ImGui::TableNextRow();
			
			ImGui::TableNextColumn(); ImGui::TextUnformatted("Repo");
			ImGui::TableNextColumn(); ImGui::Text(TempString128("{} ({})", inFile.GetRepo().mName, inFile.GetRepo().mRootPath));

			ImGui::TableNextColumn(); ImGui::TextUnformatted("RefNumber");
			ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mRefNumber));

			ImGui::TableNextColumn(); ImGui::TextUnformatted("LastChange");
			ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mLastChange));

			ImGui::EndTable();
		}

		ImGui::SeparatorText("Related Commands");

		if (!inFile.mInputOf.empty())
			gDrawCookingCommandSpan("Is Input Of", inFile.mInputOf);
		if (!inFile.mOutputOf.empty())
			gDrawCookingCommandSpan("Is Output Of", inFile.mOutputOf);


		ImGui::EndPopup();
	}
}



void gUIDrawCookingCommand(const CookingCommand& inCommand)
{
	ImGui::PushID(TempString32("Command {}", inCommand.mID.mIndex).AsCStr());
	defer { ImGui::PopID(); };

	TempString256 label("[{}] {}", inCommand.GetRule().mName, inCommand.GetMainInput().GetFile().mPath);

	bool       clicked = ImGui::Selectable(label.AsCStr(), false, ImGuiSelectableFlags_DontClosePopups);
	bool       open    = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

	// TODO add tooltip when hovering the selectable? or just over the icon (if there's one)

	if (open)
		ImGui::OpenPopup("Popup");

	if (ImGui::BeginPopup("Popup"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, gStyle.ItemSpacing);

		ImGui::Text(label);

		ImGui::SeparatorText("Related Files");

		gDrawFileInfoSpan("Inputs", inCommand.mInputs);
		gDrawFileInfoSpan("Outputs", inCommand.mOutputs);

		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}
}



void gDrawFileInfoSpan(StringView inListName, std::span<const FileID> inFileIDs)
{
	constexpr int cMaxItemsForOpenByDefault = 10;
	ImGui::SetNextItemOpen(inListName.size() <= cMaxItemsForOpenByDefault, ImGuiCond_Appearing);

	if (ImGui::TreeNode(inListName.data(), TempString64("{} ({} items)", inListName, inFileIDs.size()).AsCStr()))
	{
		for (FileID file_id : inFileIDs)
		{
			gUIDrawFileInfo(file_id.GetFile());
		}
		ImGui::TreePop();
	}
};


void gDrawCookingCommandSpan(StringView inListName, std::span<const CookingCommandID> inCommandIDs)
{
	constexpr int cMaxItemsForOpenByDefault = 10;
	ImGui::SetNextItemOpen(inListName.size() <= cMaxItemsForOpenByDefault, ImGuiCond_Appearing);

	if (ImGui::TreeNode(inListName.data(), TempString64("{} ({} items)", inListName, inCommandIDs.size()).AsCStr()))
	{
		for (CookingCommandID command_id : inCommandIDs)
		{
			gUIDrawCookingCommand(gCookingSystem.GetCommand(command_id));
		}
		ImGui::TreePop();
	}
};


void gUIDrawCookingQueue()
{
	if (!ImGui::Begin("Cooking Queue", &gDrawCookingQueue))
	{
		ImGui::End();
		return;
	}

	bool auto_cook = !gCookingSystem.IsCookingPaused();
	if (ImGui::Checkbox("Auto cook", &auto_cook))
		gCookingSystem.SetCookingPaused(!auto_cook);

	// Lock the queue while we're browsing it.
	std::lock_guard lock(gCookingQueue.mMutex);

	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None))
	{

		for (auto& bucket : gCookingQueue.mPrioBuckets)
		{
			if (bucket.mCommands.empty())
				continue;

			ImGui::SeparatorText(TempString64("Priority {} ({} items)", bucket.mPriority, bucket.mCommands.size()));

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

			ImGuiListClipper clipper;
			clipper.Begin((int)bucket.mCommands.size());
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
				{
					gUIDrawCookingCommand(gCookingSystem.GetCommand(bucket.mCommands[i]));
				}
			}
			clipper.End();

			ImGui::PopStyleVar();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}