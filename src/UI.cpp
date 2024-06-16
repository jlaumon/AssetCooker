#include "App.h"
#include "UI.h"


#include "FileSystem.h"
#include "CookingSystem.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "Ticks.h"
#include "Tests.h"

#include "win32/misc.h"
#include "win32/window.h"

extern "C" __declspec(dllimport) HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);


// TODO add open path button even if file doesn't exist (just open the folder, and maybe even create it?)
// TODO add copy command line button to command popup

static ImGuiStyle      sDarkTheme();
ImGuiStyle             gStyle = sDarkTheme();

bool                   gOpenImGuiDemo                   = false;
bool                   gOpenDebugWindow                 = false;
bool                   gOpenCookingThreadsWindow        = false;
CookingLogEntryID      gSelectedCookingLogEntry         = {};
bool                   gScrollToSelectedCookingLogEntry = false;
int                    gFirstCookingLogEntryIndex       = 0;

constexpr const char*  cWindowNameAppLog                = "App Log";
constexpr const char*  cWindowNameCommandOutput         = "Command Output";
constexpr const char*  cWindowNameCommandSearch         = "Command Search";
constexpr const char*  cWindowNameFileSearch            = "File Search";
constexpr const char*  cWindowNameWorkerThreads         = "Worker Threads";
constexpr const char*  cWindowNameCookingQueue          = "Cooking Queue";
constexpr const char*  cWindowNameCookingLog            = "Cooking Log";

int64                  gCurrentTimeInTicks              = 0;

// TODO these colors are terrible
constexpr uint32       cColorTextError                  = IM_COL32(255, 100, 100, 255);
constexpr uint32       cColorTextSuccess                = IM_COL32( 98, 214,  86, 255);
constexpr uint32       cColorFrameBgError               = IM_COL32(150,  60,  60, 255);

constexpr uint32       cColorTextFileDeleted            = IM_COL32(170, 170, 170, 255);
constexpr uint32       cColorTextInputModified          = IM_COL32( 65, 171, 240, 255);
constexpr uint32       cColorTextOuputOutdated          = IM_COL32(255, 100, 100, 255);



StringView gGetAnimatedHourglass()
{
	// TODO probably should redraw as long as this is used, because it's the sign that something still needs to update (but could also perhaps lead to accidentally always redrawing?)
	// TODO an example is the commands waiting for timeout: once they time out we need 1 redraw to replace this icon with a cross but everything is detected as 'idle' already
	constexpr StringView hourglass[] = { ICON_FK_HOURGLASS_START, ICON_FK_HOURGLASS_HALF, ICON_FK_HOURGLASS_END };
	return hourglass[(int)(gTicksToSeconds(gCurrentTimeInTicks) * 4.0) % gElemCount(hourglass)];
}


Span<const uint8> gGetEmbeddedFont(StringView inName)
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
void gUISetUserScale(float inScale)
{
	float scale = gClamp(inScale, UIScale::cMin, UIScale::cMax);
	if (scale == gUIScale.mFromSettings)
		return;

	gUIScale.mFromSettings = scale;
	gUIScale.mNeedUpdate   = true;
}


float gUIGetUserScale()
{
	return gUIScale.mFromSettings;
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
			icons_config.MergeMode             = true; // Merge into the default font.
			icons_config.GlyphOffset.y         = 0;

			static const ImWchar icon_ranges[] = { ICON_MIN_FK, ICON_MAX_FK, 0 };

			auto ttf_data = gGetEmbeddedFont("forkawesome");
			io.Fonts->AddFontFromMemoryTTF((void*)ttf_data.data(), (int)ttf_data.size(), 14.0f * gUIScale.GetFinalScale(), &icons_config, icon_ranges);
		}

		// Re-create the DX11 objects.
		ImGui_ImplDX11_CreateDeviceObjects();
	}
}


template <typename taEnumType>
static void sMenuEnum(StringView inLabel, taEnumType& ioValue)
{
	if (ImGui::BeginMenu(inLabel))
	{
		int current_index = (int)ioValue;

		if (ImGui::ListBox("##List", &current_index, [](void*, int inIndex) -> ImStrv { return gToStringView((taEnumType)inIndex); }, nullptr, (int)taEnumType::_Count))
			ioValue = (taEnumType)current_index;

		ImGui::EndMenu();
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
			ImGui::MenuItem("Cooking Threads", nullptr, &gOpenCookingThreadsWindow);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Settings"))
		{
			ImGui::MenuItem("Hide Window On Minimize", nullptr, &gApp.mHideWindowOnMinimize);
			sMenuEnum("Enable Notif On Cooking Finish", gApp.mEnableNotifOnCookingFinish);
			sMenuEnum("Enable Notif On Cooking Error", gApp.mEnableNotifOnCookingError);
			sMenuEnum("Enable Notif Sound", gApp.mEnableNotifSound);

			float ui_scale = gUIScale.mFromSettings;
			if (ImGui::DragFloat("UI Scale", &ui_scale, 0.01f, UIScale::cMin, UIScale::cMax, "%.1f"))
				gUISetUserScale(ui_scale);


			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Debug"))
		{
			sMenuEnum("Log FileSystem Activity", gApp.mLogFSActivity);

			ImGui::MenuItem("ImGui Demo Window", nullptr, &gOpenImGuiDemo);
			ImGui::MenuItem("Debug Window", nullptr, &gOpenDebugWindow);

			ImGui::MenuItem("Make Cooking Slower", nullptr, &gCookingSystem.mSlowMode);

#ifdef TESTS_ENABLED
			if (ImGui::MenuItem("Run Tests", nullptr, nullptr))
			{
				gRunTests();
			}
#endif

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

struct FileContext;

void gDrawFileInfoSpan(StringView inListName, Span<const FileID> inFileIDs, FileContext inContext);
void gDrawCookingCommandSpan(StringView inListName, Span<const CookingCommandID> inCommandIDs);


TempPath gFormat(const CookingCommand& inCommand)
{
	return { "{}{} {}",
		inCommand.GetRule().mName,
		inCommand.NeedsCleanup() ? " (Cleanup)" : "",
		inCommand.GetMainInput().GetFile() };
}


TempPath gFormat(const CookingLogEntry& inLogEntry)
{
	const CookingCommand& command    = gCookingSystem.GetCommand(inLogEntry.mCommandID);
	const CookingRule&    rule       = gCookingSystem.GetRule(command.mRuleID);
	LocalTime             start_time = inLogEntry.mTimeStart.ToLocalTime();
	return { "[#{} {:02}:{:02}:{:02}] {}{} {} - {}",
		rule.mPriority,
		start_time.mHour, start_time.mMinute, start_time.mSecond,
		command.GetRule().mName,
		inLogEntry.mIsCleanup ? " (Cleanup)" : "",
		command.GetMainInput().GetFile().mPath, gToStringView(inLogEntry.mCookingState) };
}


TempPath gFormat(const FileInfo& inFile)
{
	return { "{}", inFile };
}


enum class DependencyType
{
	Unknown,
	Input,
	Output
};


struct FileContext
{
	DependencyType mDepType  = DependencyType::Unknown;
	USN            mLastCook = 0;
};


void gDrawFileInfo(const FileInfo& inFile, FileContext inContext = {})
{
	ImGui::PushID(TempString32("File {}", inFile.mID.AsUInt()).AsCStr());
	defer { ImGui::PopID(); };

	enum
	{
		None = -1,
		Deleted = 0,
		Modified,
		Outdated
	} file_state = None;

	constexpr struct
	{
		uint32 mColor;
		StringView mTooltip;
	} cFileStateData[] = 
	{
		{ cColorTextFileDeleted, "Deleted" },
		{ cColorTextInputModified, "Modified" },
		{ cColorTextOuputOutdated, "Outdated" },
	};

	int pushed_colors = 0;
	if (inFile.IsDeleted())
	{
		file_state = Deleted;
	}
	else if (inContext.mLastCook != 0) // Zero means we have no info.
	{
		switch (inContext.mDepType)
		{
		case DependencyType::Unknown:
			break;

		case DependencyType::Input:
			if (inFile.mLastChangeUSN > inContext.mLastCook)
				file_state = Modified;
			break;

		case DependencyType::Output:
			if (inFile.mLastChangeUSN < inContext.mLastCook)
				file_state = Outdated;
			break;
		}
	}

	if (file_state != None)
		ImGui::PushStyleColor(ImGuiCol_Text, cFileStateData[file_state].mColor);

	bool clicked = ImGui::Selectable(gFormat(inFile).AsCStr(), false, ImGuiSelectableFlags_DontClosePopups);
	bool open    = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

	if (file_state != None && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip(cFileStateData[file_state].mTooltip.AsCStr());

	if (file_state != None)
		ImGui::PopStyleColor();

	if (open)
		ImGui::OpenPopup("Popup");

	if (ImGui::IsPopupOpen("Popup") && 
		ImGui::BeginPopupWithTitle("Popup", TempString128("{} {}: ...\\{}", 
			inFile.IsDirectory() ? ICON_FK_FOLDER_OPEN_O : ICON_FK_FILE_O,
			inFile.GetRepo().mName, inFile.GetName()).AsCStr()))
	{
		defer { ImGui::EndPopup(); };

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, gStyle.ItemSpacing);
		defer { ImGui::PopStyleVar(1); };

		ImGui::Spacing();
		// TODO auto wrapping is kind of incompatible with window auto resizing, need to provide a wrap position, or maybe make sure it isn't the first item drawn?
		// https://github.com/ocornut/imgui/issues/778#issuecomment-239696811
		//ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(gFormat(inFile));
		//ImGui::PopTextWrapPos();
		ImGui::Spacing();

		// TODO make it clearer when we're looking at a deleted file

		if (ImGui::Button("Show in Explorer"))
		{
			if (inFile.IsDeleted())
				// Open the parent dir.
				ShellExecuteA(nullptr, "explore", TempString512("{}{}", inFile.GetRepo().mRootPath, inFile.GetDirectory()).AsCStr(), nullptr, nullptr, SW_SHOWDEFAULT);
			else
				// Open the parent dir with the file selected.
				ShellExecuteA(nullptr, nullptr, "explorer", TempString512("/select, {}{}", inFile.GetRepo().mRootPath, inFile.mPath).AsCStr(), nullptr, SW_SHOWDEFAULT);
		}

		ImGui::SameLine();
		if (ImGui::Button("Copy Path"))
		{
			ImGui::LogToClipboard();
			ImGui::LogText(inFile.GetRepo().mRootPath.AsCStr());
			ImGui::LogText(inFile.mPath.AsCStr());
			ImGui::LogFinish();
		}

		ImGui::SeparatorText("Details");

		if (ImGui::BeginTable("File Details", 2))
		{
			ImGui::TableNextRow();
			
			ImGui::TableNextColumn(); ImGui::TextUnformatted("Repo");
			ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString128("{} ({})", inFile.GetRepo().mName, inFile.GetRepo().mRootPath));

			if (inFile.IsDeleted())
			{
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Deletion Time");
				ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inFile.mCreationTime));
			}
			else
			{
				ImGui::TableNextColumn(); ImGui::TextUnformatted("RefNumber");
				ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inFile.mRefNumber));
				
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Creation Time");
				ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inFile.mCreationTime));

				ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Change Time");
				ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inFile.mLastChangeTime));
				
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Change USN");
				ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inFile.mLastChangeUSN));
			}

			ImGui::EndTable();
		}

		ImGui::SeparatorText("Related Commands");

		if (!inFile.mInputOf.empty())
			gDrawCookingCommandSpan("Is Input Of", inFile.mInputOf);
		if (!inFile.mOutputOf.empty())
			gDrawCookingCommandSpan("Is Output Of", inFile.mOutputOf);
	}
}


void gSelectCookingLogEntry(CookingLogEntryID inLogEntryID, bool inScrollLog)
{
	gSelectedCookingLogEntry         = inLogEntryID;
	gScrollToSelectedCookingLogEntry = inScrollLog;
	ImGui::SetWindowFocus(cWindowNameCommandOutput);
}


void gDrawCookingCommandPopup(const CookingCommand& inCommand)
{
	if (!ImGui::IsPopupOpen("Popup"))
		return;

	if (!ImGui::BeginPopupWithTitle("Popup", TempString512(ICON_FK_CUTLERY " {}{} ...\\{}",
		inCommand.GetRule().mName,
		inCommand.NeedsCleanup() ? " (Cleanup)" : "",
		inCommand.GetMainInput().GetFile().GetName()).AsStringView()))
		return;

	defer { ImGui::EndPopup(); };

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, gStyle.ItemSpacing);

	if (!inCommand.IsCleanedUp() && ImGui::Button("Cook Now"))
		gCookingSystem.ForceCook(inCommand.mID);

	// TODO add a Cleanup button

	if (inCommand.mLastCookingLog)
	{
		ImGui::SameLine();
		if (ImGui::Button("Select last Log"))
		{
			gSelectCookingLogEntry(inCommand.mLastCookingLog->mID, true);
		}
	}

	ImGui::SeparatorText("Details");

	if (ImGui::BeginTable("Details", 2))
	{
		ImGui::TableNextRow();

		ImGui::TableNextColumn(); ImGui::TextUnformatted("Cooking State");
		ImGui::TableNextColumn();
		{
			if (inCommand.IsDirty())
			{
				TempString256 dirty_details("Dirty (");
				if (inCommand.mDirtyState & CookingCommand::Error)
					dirty_details.Append("Error|");
				if (inCommand.mDirtyState & CookingCommand::VersionMismatch)
					dirty_details.Append("Version Mismatch|");
				if (inCommand.mDirtyState & CookingCommand::InputMissing)
					dirty_details.Append("Input Missing|");
				if (inCommand.mDirtyState & CookingCommand::InputChanged)
					dirty_details.Append("Input Changed|");
				if (inCommand.mDirtyState & CookingCommand::OutputMissing)
					dirty_details.Append("Output Missing|");

				// Replace the last | with )
				dirty_details.mBuffer[dirty_details.mSize - 1] = ')'; 
				
				ImGui::TextUnformatted(dirty_details);
			}
			else if (inCommand.IsCleanedUp())
				ImGui::TextUnformatted("Cleaned Up");
			else
				ImGui::TextUnformatted("Up To Date");
		}

		ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Cook");
		ImGui::TableNextColumn();
		{
			StringView last_cook_status = "Unknown";
			if (inCommand.mDirtyState & CookingCommand::Error)
				last_cook_status = "Error";
			else if (inCommand.mLastCookingLog != nullptr)
				last_cook_status = "Success";

			ImGui::TextUnformatted(last_cook_status);
			if (inCommand.mLastCookingLog)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Select in Log"))
					gSelectCookingLogEntry(inCommand.mLastCookingLog->mID, true);
			}
		}


		ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Cook Time");
		ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inCommand.mLastCookTime));

		ImGui::TableNextColumn(); ImGui::TextUnformatted("Last Cook USN");
		ImGui::TableNextColumn(); ImGui::TextUnformatted(TempString64("{}", inCommand.mLastCookUSN));
		
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Related Files");

	gDrawFileInfoSpan("Inputs", inCommand.mInputs, { DependencyType::Input, inCommand.mLastCookUSN });
	
	if (!inCommand.mDepFileInputs.empty())
		gDrawFileInfoSpan("DepFile Inputs", inCommand.mDepFileInputs, { DependencyType::Input, inCommand.mLastCookUSN });

	gDrawFileInfoSpan("Outputs", inCommand.mOutputs, { DependencyType::Output, inCommand.mLastCookUSN });

	if (!inCommand.mDepFileOutputs.empty())
		gDrawFileInfoSpan("DepFile Outputs", inCommand.mDepFileOutputs, { DependencyType::Output, inCommand.mLastCookUSN });

	ImGui::PopStyleVar();
}


void gDrawCookingCommand(const CookingCommand& inCommand)
{
	ImGui::PushID(TempString32("Command {}", inCommand.mID.mIndex).AsCStr());
	defer { ImGui::PopID(); };
	
	int pop_color = 0;

	if (inCommand.GetCookingState() == CookingState::Error)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, cColorTextError);
		pop_color++;
	}

	bool clicked = ImGui::Selectable(gFormat(inCommand).AsCStr(), false, ImGuiSelectableFlags_DontClosePopups);
	bool open    = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

	if (clicked && inCommand.mLastCookingLog)
		gSelectCookingLogEntry(inCommand.mLastCookingLog->mID, true);

	if (pop_color)
		ImGui::PopStyleColor(pop_color);

	if (open)
		ImGui::OpenPopup("Popup");

	gDrawCookingCommandPopup(inCommand);
}



void gDrawFileInfoSpan(StringView inListName, Span<const FileID> inFileIDs, FileContext inContext)
{
	constexpr int cMaxItemsForOpenByDefault = 10;
	ImGui::SetNextItemOpen(inFileIDs.size() <= cMaxItemsForOpenByDefault, ImGuiCond_Appearing);

	if (ImGui::TreeNode(inListName.data(), TempString64("{} ({} items)", inListName, inFileIDs.size()).AsCStr()))
	{
		for (FileID file_id : inFileIDs)
		{
			gDrawFileInfo(file_id.GetFile(), inContext);
		}
		ImGui::TreePop();
	}
};


void gDrawCookingCommandSpan(StringView inListName, Span<const CookingCommandID> inCommandIDs)
{
	constexpr int cMaxItemsForOpenByDefault = 10;
	ImGui::SetNextItemOpen(inCommandIDs.size() <= cMaxItemsForOpenByDefault, ImGuiCond_Appearing);

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
	if (!ImGui::Begin(cWindowNameCookingQueue))
	{
		ImGui::End();
		return;
	}

	bool paused = gCookingSystem.IsCookingPaused();
	if (ImGui::Button(paused ? ICON_FK_PLAY " Start Cooking" : ICON_FK_STOP " Stop Cooking"))
		gCookingSystem.SetCookingPaused(!paused);

	ImGui::SameLine();
	if (ImGui::Button(ICON_FK_REPEAT " Cook Errored"))
		gCookingSystem.QueueErroredCommands();

	// Lock the dirty command list while we're browsing it.
	std::lock_guard lock(gCookingSystem.mCommandsDirty.mMutex);

	if (ImGui::BeginChild("ScrollingRegion"))
	{
		bool all_empty = true;
		for (auto& bucket : gCookingSystem.mCommandsDirty.mPrioBuckets)
		{
			if (bucket.mCommands.empty())
				continue;

			all_empty = false;

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

		if (all_empty && gFileSystem.GetInitState() == FileSystem::InitState::Ready)
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("All caught up! " ICON_FK_HAND_PEACE_O);
		}
	}
	ImGui::EndChild();

	ImGui::End();
}


void gDrawCookingLog()
{
	bool visible = ImGui::Begin(cWindowNameCookingLog);
	defer { ImGui::End(); };

	if (!visible)
		return;

	// Clear doesn't actually clear anything, it just doesn't display old logs.
	if (ImGui::Button("Clear"))
		gFirstCookingLogEntryIndex = gCookingSystem.mCookingLog.Size();

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(TempString128("{} items", gCookingSystem.mCookingLog.Size() - gFirstCookingLogEntryIndex));


	if (!ImGui::BeginTable("CookingLog", 4, ImGuiTableFlags_ScrollY))
		return;
	defer { ImGui::EndTable(); };

	ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);

	ImGui::TableNextRow();

	bool scroll_target_changed = false;

	ImGuiListClipper clipper;
	clipper.Begin((int)gCookingSystem.mCookingLog.Size() - gFirstCookingLogEntryIndex);
	defer { clipper.End(); };
	while (clipper.Step())
	{
		if (clipper.ItemsHeight != -1.f && gScrollToSelectedCookingLogEntry && gSelectedCookingLogEntry.IsValid())
		{
			gScrollToSelectedCookingLogEntry = false;
            ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + (float)gSelectedCookingLogEntry.mIndex * clipper.ItemsHeight);
			scroll_target_changed = true;
		}

		for (int i = (gFirstCookingLogEntryIndex + clipper.DisplayStart); i < (gFirstCookingLogEntryIndex + clipper.DisplayEnd); i++)
		{
			const CookingLogEntry& log_entry = gCookingSystem.mCookingLog[i];
			const CookingCommand&  command   = gCookingSystem.GetCommand(log_entry.mCommandID);
			const CookingRule&     rule      = gCookingSystem.GetRule(command.mRuleID);
			bool                   selected  = (gSelectedCookingLogEntry.mIndex == (uint32)i);

			ImGui::PushID(&log_entry);
			defer { ImGui::PopID(); };

			ImGui::TableNextColumn();
			{
				LocalTime start_time = log_entry.mTimeStart.ToLocalTime();
				if (ImGui::Selectable(TempString32("[#{} {:02}:{:02}:{:02}]", rule.mPriority, start_time.mHour, start_time.mMinute, start_time.mSecond).AsCStr(), selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					gSelectCookingLogEntry({ (uint32)i }, false);
				}

				bool open = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
				if (open)
					ImGui::OpenPopup("Popup");

			}

			ImGui::TableNextColumn();
			{
				ImGui::TextUnformatted(command.GetRule().mName);
			}

			ImGui::TableNextColumn();
			{
				ImGui::TextUnformatted(TempString512("{}{}", log_entry.mIsCleanup ? "(Cleanup) " : "" ,command.GetMainInput().GetFile()));
			}

			ImGui::TableNextColumn();
			{
				constexpr StringView cIcons[]
				{
					ICON_FK_QUESTION,
					ICON_FK_HOURGLASS,
					ICON_FK_HOURGLASS,
					ICON_FK_TIMES,
					ICON_FK_CHECK,
				};
				static_assert(gElemCount(cIcons) == (size_t)CookingState::_Count);

				auto cooking_state = log_entry.mCookingState.load();
				auto icon          = cIcons[(int)cooking_state];

				if (cooking_state == CookingState::Cooking || cooking_state == CookingState::Waiting)
					icon = gGetAnimatedHourglass();

				int pop_color = 0;

				if (cooking_state == CookingState::Success)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, cColorTextSuccess);
					pop_color++;
				}

				if (cooking_state == CookingState::Error)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, cColorTextError);
					pop_color++;
				}

				ImGui::TextUnformatted(TempString32(" {} ", icon));

				if (pop_color)
					ImGui::PopStyleColor(pop_color);

				ImGui::SetItemTooltip(gToStringView(cooking_state).AsCStr());
			}

			gDrawCookingCommandPopup(gCookingSystem.GetCommand(log_entry.mCommandID));
		}
	}

	// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
	// Using a scrollbar or mouse-wheel will take away from the bottom edge.
	if (!scroll_target_changed && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);
}


void gDrawSelectedCookingLogEntry()
{
	bool opened = ImGui::Begin(cWindowNameCommandOutput);
	defer { ImGui::End(); };

	if (!opened || !gSelectedCookingLogEntry.IsValid())
		return;

	const CookingLogEntry& log_entry = gCookingSystem.GetLogEntry(gSelectedCookingLogEntry);

	ImGui::PushTextWrapPos();
	ImGui::TextUnformatted(gFormat(log_entry));
	ImGui::PopTextWrapPos();
	if (ImGui::Button("Copy Command Line"))
	{
		const CookingCommand& command      = gCookingSystem.GetCommand(log_entry.mCommandID);
		const CookingRule&    rule         = command.GetRule();
		Optional<String>      command_line = gFormatCommandString(rule.mCommandLine, gFileSystem.GetFile(command.GetMainInput()));
		if (command_line)
		{
			ImGui::LogToClipboard();
			ImGui::LogText(command_line->c_str());
			ImGui::LogFinish();
		}
	}

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
	if (!ImGui::Begin(cWindowNameCommandSearch))
	{
		ImGui::End();
		return;
	}

	static ImGuiTextFilter                   filter;
	static SegmentedVector<CookingCommandID> filtered_list;

	if (filter.Draw(R"(Filter ("incl,-excl") ("texture"))", 400))
	{
		// Rebuild the filtered list.
		filtered_list.clear();
		for (const CookingCommand& command : gCookingSystem.mCommands)
		{
			auto command_str = gFormat(command);
			if (filter.PassFilter(command_str))
				filtered_list.emplace_back(command.mID);
		}
	}

	if (ImGui::Button("Cook All"))
	{
		if (filter.IsActive())
		{
			for (CookingCommandID command_id : filtered_list)
				gCookingSystem.ForceCook(command_id);
		}
		else
		{
			for (const CookingCommand& command : gCookingSystem.mCommands)
				gCookingSystem.ForceCook(command.mID);
		}
	}

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(TempString128("{} items", filter.IsActive() ? filtered_list.size() : gCookingSystem.mCommands.Size()));

	if (ImGui::BeginChild("ScrollingRegion"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
		ImGuiListClipper clipper;

		if (filter.IsActive())
		{
			// Draw the filtered list.
			clipper.Begin((int)filtered_list.size());
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
					gDrawCookingCommand(gCookingSystem.GetCommand(filtered_list[i]));
			}
			clipper.End();
		}
		else
		{
			// Draw the full list.
			clipper.Begin((int)gCookingSystem.mCommands.Size());
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
					gDrawCookingCommand(gCookingSystem.GetCommand(CookingCommandID{ (uint32)i }));
			}
			clipper.End();
		}

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	ImGui::End();
}


void gDrawFileSearch()
{
	if (!ImGui::Begin(cWindowNameFileSearch))
	{
		ImGui::End();
		return;
	}

	static ImGuiTextFilter         filter;
	static SegmentedVector<FileID> filtered_list;

	if (filter.Draw(R"(Filter ("incl,-excl") ("texture"))", 400))
	{
		// Rebuild the filtered list.
		filtered_list.clear();
		for (const FileRepo& repo : gFileSystem.mRepos)
			for (const FileInfo& file : repo.mFiles)
			{
				auto file_str = gFormat(file);
				if (filter.PassFilter(file_str))
					filtered_list.emplace_back(file.mID);
			}
	}

	ImGui::TextUnformatted(TempString128("{} items", filter.IsActive() ? filtered_list.size() : gFileSystem.GetFileCount()));

	if (ImGui::BeginChild("ScrollingRegion"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
		ImGuiListClipper clipper;

		if (filter.IsActive())
		{
			// Draw the filtered list.
			clipper.Begin((int)filtered_list.size());
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
					gDrawFileInfo(filtered_list[i].GetFile());
			}
			clipper.End();
		}
		else
		{
			// Draw the full list.
			for (const FileRepo& repo : gFileSystem.mRepos)
			{
				clipper.Begin((int)repo.mFiles.Size());
				while (clipper.Step())
				{
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
						gDrawFileInfo(repo.mFiles[i]);
				}
				clipper.End();
			}
				
		}

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	ImGui::End();
}


void gDrawCookingThreads()
{
	if (!gOpenCookingThreadsWindow)
		return;

	if (!ImGui::Begin(cWindowNameWorkerThreads, &gOpenCookingThreadsWindow, ImGuiWindowFlags_NoScrollbar))
	{
		ImGui::End();
		return;
	}

	int thread_count = (int)gCookingSystem.mCookingThreads.size();
	int columns      = sqrt(thread_count); // TODO need a max number of columns instead, they're useless if too short

	if (thread_count && ImGui::BeginTable("Threads", columns, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextRow();

		for (auto& thread : gCookingSystem.mCookingThreads)
		{
			ImGui::TableNextColumn();
			ImGui::BeginChild(ImGui::GetID(&thread), ImVec2(0, ImGui::GetFrameHeight()), ImGuiChildFlags_FrameStyle);
			
			CookingLogEntryID entry_id = thread.mCurrentLogEntry.load();
			if (entry_id.IsValid())
			{
				const CookingLogEntry& entry_log = gCookingSystem.GetLogEntry(entry_id);
				const CookingCommand&  command   = gCookingSystem.GetCommand(entry_log.mCommandID);

				ImGui::TextUnformatted(TempString128("{} {}", command.GetRule().mName, command.GetMainInput().GetFile().mPath));

				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
					gSelectCookingLogEntry(entry_id, true);
			}
			else
			{
				ImGui::TextUnformatted("Idle");
			}

			ImGui::EndChild();
		}

		ImGui::EndTable();
	}

	ImGui::End();
}

extern bool gDebugFailCookingRandomly;
extern bool gDebugFailOpenFileRandomly;

void gDrawDebugWindow()
{
	if (!ImGui::Begin("Debug", &gOpenDebugWindow, ImGuiWindowFlags_NoDocking))
	{
		ImGui::End();
		return;
	}

	if (ImGui::Button("Cook 100"))
	{
		int num_commands  = gCookingSystem.mCommands.Size();
		int first_command = gRand32() % num_commands;
		for (int i = 0; i<gMin(100, (int)num_commands); ++i)
		{
			int command_index = (first_command + i) % num_commands;
			gCookingSystem.ForceCook(gCookingSystem.mCommands[command_index].mID);
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Cook 1000"))
	{
		int num_commands  = gCookingSystem.mCommands.Size();
		int first_command = gRand32() % num_commands;
		for (int i = 0; i<gMin(1000, (int)num_commands); ++i)
		{
			int command_index = (first_command + i) % num_commands;
			gCookingSystem.ForceCook(gCookingSystem.mCommands[i].mID);
		}
	}

	ImGui::Checkbox("Slow mode", &gCookingSystem.mSlowMode);
	ImGui::Checkbox("Cause random Cooking errors", &gDebugFailCookingRandomly);
	ImGui::Checkbox("Cause random FileSystem errors", &gDebugFailOpenFileRandomly);

	Span rules = gCookingSystem.GetRules();
	if (ImGui::CollapsingHeader(TempString64("Rules ({})##Rules", rules.size()).AsCStr()))
	{
		ImGuiListClipper clipper;
		clipper.Begin((int)rules.size());
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const CookingRule& rule = rules[i];
				ImGui::TextUnformatted(rule.mName);
				// TODO add a gDrawCookingRule
			}
		}
		clipper.End();
	}

	if (ImGui::CollapsingHeader(TempString64("Commands ({})##Commands", gCookingSystem.mCommands.Size()).AsCStr()))
	{
		ImGuiListClipper clipper;
		clipper.Begin((int)gCookingSystem.mCommands.Size());
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
				gDrawCookingCommand(gCookingSystem.mCommands[i]);
		}
		clipper.End();
	}

	for (FileRepo& repo : gFileSystem.mRepos)
	{
		ImGui::PushID(&repo);
		if (ImGui::CollapsingHeader(TempString128("{} ({}) - {} Files##Repo", repo.mName, repo.mRootPath, repo.mFiles.Size()).AsCStr()))
		{
			ImGuiListClipper clipper;
			clipper.Begin((int)repo.mFiles.Size());
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
					gDrawFileInfo(repo.mFiles[i]);
			}
			clipper.End();
		}
		ImGui::PopID();
	}

	ImGui::End();
}


void gDrawStatusBar()
{
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	float            height       = ImGui::GetFrameHeight();

	bool             is_error     = gApp.HasInitError();
	int              pop_color    = 0;

	// Pop any style that was pushed.
	defer
	{
		if (pop_color)
			ImGui::PopStyleColor(pop_color);
	};

	if (is_error)
	{
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, cColorFrameBgError);
		pop_color++;
	}

	if (!ImGui::BeginViewportSideBar("##MainStatusBar", nullptr, ImGuiDir_Down, height, window_flags))
		return;

	defer { ImGui::End(); };

	if (!ImGui::BeginMenuBar())
		return;
	defer { ImGui::EndMenuBar(); };

	if (gApp.HasInitError())
	{
		ImGui::TextUnformatted(StringView(gApp.mInitError));
		return;
	}

	auto init_state = gFileSystem.GetInitState();
	if (init_state < FileSystem::InitState::Ready)
	{
		switch (init_state)
		{
		default:
		case FileSystem::InitState::NotInitialized:
		{
			ImGui::TextUnformatted("Bonjour.");
			break;
		}
		case FileSystem::InitState::LoadingCache: 
		{
			ImGui::TextUnformatted(TempString128("{} Loading cache... {:5} files found.", gGetAnimatedHourglass(), gFileSystem.GetFileCount()));
			break;
		}
		case FileSystem::InitState::Scanning: 
		{
			ImGui::TextUnformatted(TempString128("{} Scanning... {:5} files found.", gGetAnimatedHourglass(), gFileSystem.GetFileCount()));
			break;
		}
		case FileSystem::InitState::ReadingUSNJournal: 
		{
			ImGui::TextUnformatted(TempString32("{} Reading USN journal...", gGetAnimatedHourglass()));
			break;
		}
		case FileSystem::InitState::ReadingIndividualUSNs: 
		{
			ImGui::TextUnformatted(TempString128("{} Reading individual USNs... {:5}/{}", gGetAnimatedHourglass(),
				gFileSystem.mInitStats.mIndividualUSNFetched.load(), gFileSystem.mInitStats.mIndividualUSNToFetch
			));
			break;
		}
		case FileSystem::InitState::PreparingCommands: 
		{
			ImGui::TextUnformatted(TempString128("{} Preparing commands...", gGetAnimatedHourglass()));
			break;
		}
		}

		return;
	}
 
	double seconds_since_ready = gTicksToSeconds(gGetTickCount() - gFileSystem.mInitStats.mReadyTicks);
	if (seconds_since_ready < 8.0)
	{
		ImGui::TextUnformatted(TempString128(ICON_FK_THUMBS_O_UP " Init complete in {:.2f} seconds. ",	gTicksToSeconds(gFileSystem.mInitStats.mReadyTicks - gProcessStartTicks)));
	}
	else
	{
		constexpr StringView messages[] = {
			ICON_FK_CUTLERY" It's a great day to cook.",
			ICON_FK_CUTLERY" It's not impossible, it just needs to cook a bit longer.",
			ICON_FK_CUTLERY" What's a shader anyway.",
			ICON_FK_CUTLERY" Let's cook! LET'S COOK!",
			ICON_FK_CUTLERY" A fort wasn't cooked in a day. It took 0.4 seconds.",
			ICON_FK_CUTLERY" Are you going to change a file today?",
			ICON_FK_CUTLERY" Too many cooks? Too many Cooks!",
		};
		static int message_choice = gRand32() % gElemCount(messages);
		ImGui::TextUnformatted(messages[message_choice]);

		// Show a different message on click (the clamp is there to make sure we don't get the same message again).
		if (ImGui::IsItemClicked())
			message_choice = (message_choice + gClamp<int>(gRand32(), 1, gElemCount(messages) - 1)) % gElemCount(messages);
	}

	// Display some stats on the right side of the status bar.
	{
		TempString128 cooking_stats("{} Files, {} Repos, {} Commands | ", gFileSystem.GetFileCount(), gFileSystem.GetRepoCount(), gCookingSystem.GetCommandCount());
		float stats_text_size = ImGui::CalcTextSize(cooking_stats).x;

		StringView ui_stats(ICON_FK_TACHOMETER " UI");
		stats_text_size += ImGui::CalcTextSize(ui_stats).x;

		float available_size  = ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().ItemSpacing.x;
		ImGui::SameLine(available_size - stats_text_size);
		ImGui::TextUnformatted(cooking_stats);
		ImGui::SameLine(0, 0);
		ImGui::TextUnformatted(ui_stats);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(TempString64("UI CPU:{:4.2f}ms\nUI GPU:{:4.2f}ms", gUILastFrameStats.mCPUMilliseconds, gUILastFrameStats.mGPUMilliseconds).AsCStr());
	}
}


void gDrawMain()
{
	gCurrentTimeInTicks = gGetTickCount() - gProcessStartTicks;

	ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoWindowMenuButton);
	do_once
	{
		ImGui::DockBuilderRemoveNode(dockspace_id); // Clear out existing layout
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode); // Add empty node
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

		ImGuiID dock_main_id   = dockspace_id; // This variable will track the document node, however we are not using it here as we aren't docking anything into it.
		ImGuiID dock_id_up     = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up,    0.2f, nullptr, &dock_main_id);
		ImGuiID dock_id_left   = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left,  0.5f, nullptr, &dock_main_id);
		ImGuiID dock_id_right_up, dock_id_right_bottom;
		ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.5f, &dock_id_right_up, &dock_id_right_bottom);

		ImGui::DockBuilderDockWindow(cWindowNameWorkerThreads, dock_id_up);
		ImGui::DockBuilderDockWindow(cWindowNameCommandSearch, dock_id_left);
		ImGui::DockBuilderDockWindow(cWindowNameFileSearch,    dock_id_left);
		ImGui::DockBuilderDockWindow(cWindowNameCookingQueue,  dock_id_left);
		ImGui::DockBuilderDockWindow(cWindowNameCookingLog,    dock_id_right_up);
		ImGui::DockBuilderDockWindow(cWindowNameAppLog,        dock_id_right_bottom);
		ImGui::DockBuilderDockWindow(cWindowNameCommandOutput, dock_id_right_bottom);
		ImGui::DockBuilderFinish(dockspace_id);
	};


	gApp.mLog.Draw(cWindowNameAppLog);

	gDrawCookingQueue();
	gDrawCookingThreads();
	gDrawCookingLog();
	gDrawCommandSearch();
	gDrawFileSearch();
	gDrawSelectedCookingLogEntry();
	gDrawStatusBar();

	if (gOpenDebugWindow)
		gDrawDebugWindow();

	if (gOpenImGuiDemo)
		ImGui::ShowDemoWindow(&gOpenImGuiDemo);

	// If the App failed to Init, set the focus on the Log because it will contain the errors details.
	// Doesn't work until the windows have been drawn once (not sure why), so do it at the end.
	if (gApp.HasInitError())
	{
		// Do it just once though, otherwise we can't select other windows.
		do_once { ImGui::SetWindowFocus(cWindowNameAppLog); };
	}
}


// ImGui Dark Theme from Omar Cornut's stash.
ImGuiStyle sDarkTheme()
{
	ImGuiStyle style;
	ImVec4* colors                        = style.Colors;

	colors[ImGuiCol_Text]                 = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg]             = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_ChildBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_Border]               = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
	colors[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_TitleBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgActive]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg]            = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark]            = ImVec4(0.99f, 1.00f, 0.98f, 1.00f);
	colors[ImGuiCol_SliderGrab]           = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
	colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Button]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_ButtonHovered]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
	colors[ImGuiCol_ButtonActive]         = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
	colors[ImGuiCol_Header]               = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
	colors[ImGuiCol_HeaderHovered]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
	colors[ImGuiCol_HeaderActive]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator]            = colors[ImGuiCol_Border];
	colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	colors[ImGuiCol_SeparatorActive]      = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_ResizeGrip]           = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	colors[ImGuiCol_Tab]                  = ImVec4(0.16f, 0.16f, 0.16f, 0.00f);
	colors[ImGuiCol_TabHovered]           = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
	colors[ImGuiCol_TabActive]            = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
	colors[ImGuiCol_TabUnfocused]         = ImVec4(0.24f, 0.24f, 0.24f, 0.00f);
	colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.39f, 0.39f, 0.39f, 0.22f);
#ifdef IMGUI_HAS_DOCK
	colors[ImGuiCol_DockingPreview] = colors[ImGuiCol_HeaderActive];
	colors[ImGuiCol_DockingPreview].w *= 0.4f;
	colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
#endif
	colors[ImGuiCol_PlotLines]             = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram]         = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight]          = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);

	return style;
}