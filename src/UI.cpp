#include "App.h"
#include "UI.h"

#include "FileSystem.h"
#include "CookingSystem.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"

#include "win32/misc.h"

ImGuiStyle             gStyle                           = {};

bool                   gDrawAppLog                      = true;
bool                   gDrawImGuiDemo                   = false;
int                    gSelectedCookingLogEntry         = -1;
bool                   gScrollToSelectedCookingLogEntry = false;

constexpr const char*  cAppLogWindowName                = "App Log";
constexpr const char*  cCommandOutputWindowName         = "Command Output";


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


void gDrawMainMenuBar()
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
			ImGui::MenuItem(cAppLogWindowName, nullptr, &gDrawAppLog);

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

			ImGui::MenuItem("ImGui Demo Window", nullptr, &gDrawImGuiDemo);

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}



void gDrawFileInfoSpan(StringView inListName, std::span<const FileID> inFileIDs);
void gDrawCookingCommandSpan(StringView inListName, std::span<const CookingCommandID> inCommandIDs);


TempString256 gFormat(const CookingCommand& inCommand)
{
	return { "Command: {} {}", inCommand.GetRule().mName, inCommand.GetMainInput().GetFile().mPath };
}


TempString256 gFormat(const CookingLogEntry& inLogEntry)
{
	const CookingCommand& command = gCookingSystem.GetCommand(inLogEntry.mCommandID);
	const CookingRule&    rule    = gCookingSystem.GetRule(command.mRuleID);
	SystemTime            start_time = inLogEntry.mTimeStart.ToLocalTime();
	return { "[#{} {:02}:{:02}:{:02}] {:8} {} - {}",
		rule.mPriority,
		start_time.mHour, start_time.mMinute, start_time.mSecond,
		command.GetRule().mName, command.GetMainInput().GetFile().mPath, gToStringView(inLogEntry.mCookingState) };
}


TempString512 gFormat(const FileInfo& inFile)
{
	return { "{}", inFile };
}


void gDrawFileInfo(const FileInfo& inFile)
{
	ImGui::PushID(TempString32("File {}", inFile.mID.AsUInt()).AsCStr());
	defer { ImGui::PopID(); };

	bool clicked = ImGui::Selectable(gFormat(inFile).AsCStr(), false, ImGuiSelectableFlags_DontClosePopups);
	bool open    = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

	if (open)
		ImGui::OpenPopup("Popup");

	if (ImGui::BeginPopup("Popup"))
	{
		ImGui::Text(gFormat(inFile).AsStringView());

		ImGui::ButtonGrad("Open Dir");
		ImGui::SameLine();
		ImGui::ButtonGrad("Open File");

		ImGui::SeparatorText("Details");

		if (ImGui::BeginTable("File Details", 2))
		{
			ImGui::TableNextRow();
			
			ImGui::TableNextColumn(); ImGui::TextUnformatted("Repo");
			ImGui::TableNextColumn(); ImGui::Text(TempString128("{} ({})", inFile.GetRepo().mName, inFile.GetRepo().mRootPath));

			if (inFile.IsDeleted())
			{
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Deletion Time");
				ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mCreationTime));
			}
			else
			{
				ImGui::TableNextColumn(); ImGui::TextUnformatted("RefNumber");
				ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mRefNumber));
				
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Creation Time");
				ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mCreationTime));

				ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Change Time");
				ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mLastChangeTime));
				
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Change USN");
				ImGui::TableNextColumn(); ImGui::Text(TempString64("{}", inFile.mLastChangeUSN));
			}

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


void gDrawCookingCommandPopup(const CookingCommand& inCommand)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, gStyle.ItemSpacing);

	ImGui::Text(gFormat(inCommand));

	if (ImGui::ButtonGrad("Cook"))
		gCookingSystem.ForceCook(inCommand.mID);

	if (inCommand.mLastCookingLog)
	{
		ImGui::SameLine();
		if (ImGui::ButtonGrad("Select last Log"))
		{
			gSelectedCookingLogEntry         = inCommand.mLastCookingLog->mIndex;
			gScrollToSelectedCookingLogEntry = true;
		}
	}

	ImGui::SeparatorText("Cooking State");

	if (inCommand.IsDirty())
	{
		ImGui::TextUnformatted("Dirty");

		ImGui::Indent();

		if (inCommand.mDirtyState & CookingCommand::InputMissing)
			ImGui::TextUnformatted("Input Missing");
		if (inCommand.mDirtyState & CookingCommand::InputChanged)
			ImGui::TextUnformatted("Input Changed");
		if (inCommand.mDirtyState & CookingCommand::OutputMissing)
			ImGui::TextUnformatted("Output Missing");

		ImGui::Unindent();
	}
	else
	{
		ImGui::TextUnformatted("Up To Date");
	}

	ImGui::SeparatorText("Related Files");

	gDrawFileInfoSpan("Inputs", inCommand.mInputs);
	gDrawFileInfoSpan("Outputs", inCommand.mOutputs);

	ImGui::PopStyleVar();
}


void gDrawCookingCommand(const CookingCommand& inCommand)
{
	ImGui::PushID(TempString32("Command {}", inCommand.mID.mIndex).AsCStr());
	defer { ImGui::PopID(); };

	bool clicked = ImGui::Selectable(gFormat(inCommand).AsCStr(), false, ImGuiSelectableFlags_DontClosePopups);
	bool open    = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

	// TODO add tooltip when hovering the selectable? or just over the icon (if there's one)

	if (open)
		ImGui::OpenPopup("Popup");

	if (ImGui::BeginPopup("Popup"))
	{
		gDrawCookingCommandPopup(inCommand);
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
			gDrawFileInfo(file_id.GetFile());
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
			gDrawCookingCommand(gCookingSystem.GetCommand(command_id));
		}
		ImGui::TreePop();
	}
};


void gDrawCookingQueue()
{
	if (!ImGui::Begin("Cooking Queue"))
	{
		ImGui::End();
		return;
	}

	bool auto_cook = !gCookingSystem.IsCookingPaused();
	if (ImGui::Checkbox("Auto cook", &auto_cook))
		gCookingSystem.SetCookingPaused(!auto_cook);

	// Lock the dirty command list while we're browsing it.
	std::lock_guard lock(gCookingSystem.mCommandsDirty.mMutex);

	if (ImGui::BeginChild("ScrollingRegion"))
	{

		for (auto& bucket : gCookingSystem.mCommandsDirty.mPrioBuckets)
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
					gDrawCookingCommand(gCookingSystem.GetCommand(bucket.mCommands[i]));
				}
			}
			clipper.End();

			ImGui::PopStyleVar();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}


void gDrawCookingLog()
{
	if (!ImGui::Begin("Cooking Log"))
	{
		ImGui::End();
		return;
	}

	// Lock the log while we're browsing it.
	// TODO: would not be necessary with a virtual mem array, just need to read size atomically
	std::lock_guard lock(gCookingSystem.mCookingLogMutex);

	if (ImGui::BeginChild("ScrollingRegion"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

		ImGuiListClipper clipper;
		clipper.Begin((int)gCookingSystem.mCookingLog.size());
		while (clipper.Step())
		{
			if (gScrollToSelectedCookingLogEntry && gSelectedCookingLogEntry != -1)
			{
				// TODO test this
				gScrollToSelectedCookingLogEntry = false;
                ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + gSelectedCookingLogEntry * clipper.ItemsHeight);
			}

			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const CookingLogEntry& log_entry     = gCookingSystem.mCookingLog[i];
				bool                   selected      = (gSelectedCookingLogEntry == i);

				ImGui::PushID(&log_entry);

				// TODO draw spinner and change color based on cooking state
				if (ImGui::Selectable(gFormat(log_entry).AsCStr(), selected))
				{
					gSelectedCookingLogEntry = i;
					ImGui::SetWindowFocus(cCommandOutputWindowName);
				}

				bool open = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
				if (open)
					ImGui::OpenPopup("Popup");

				if (ImGui::BeginPopup("Popup"))
				{
					gDrawCookingCommandPopup(gCookingSystem.GetCommand(log_entry.mCommandID));
					ImGui::EndPopup();
				}

				ImGui::PopID();
			}
		}
		clipper.End();

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	ImGui::End();
	
}


void gDrawSelectedCookingLogEntry()
{
	bool opened = ImGui::Begin(cCommandOutputWindowName);
	defer { ImGui::End(); };

	if (!opened || gSelectedCookingLogEntry == -1)
		return;

	const CookingLogEntry& log_entry = gCookingSystem.mCookingLog[gSelectedCookingLogEntry];

	ImGui::TextUnformatted(gFormat(log_entry));

	if (ImGui::BeginChild("ScrollingRegion", {}, ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_HorizontalScrollbar))
	{
		// If it's finished cooking, it's safe to read the log output.
		if (log_entry.mCookingState > CookingState::Cooking)
		{
			//ImGui::PushTextWrapPos();
			ImGui::TextUnformatted(log_entry.mOutput);
			//ImGui::PopTextWrapPos();
		}
	}
	ImGui::EndChild();
}


void gDrawCommandSearch()
{
	if (!ImGui::Begin("Command Search"))
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginChild("ScrollingRegion"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));

		ImGuiListClipper clipper;
		clipper.Begin((int)gCookingSystem.mCommands.size());
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const CookingCommand& command = gCookingSystem.GetCommand(CookingCommandID{ (uint32)i });
				gDrawCookingCommand(command);
			}
		}

		clipper.End();

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	ImGui::End();
}



void gDrawMain()
{
	ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoWindowMenuButton);

	if (gDrawAppLog)
		gApp.mLog.Draw(cAppLogWindowName, &gDrawAppLog);

	gDrawCookingQueue();

	gDrawCookingLog();
	gDrawCommandSearch();
	gDrawSelectedCookingLogEntry();

	if (gDrawImGuiDemo)
		ImGui::ShowDemoWindow(&gDrawImGuiDemo);
}