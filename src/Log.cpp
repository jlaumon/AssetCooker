/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Log.h"

#include "FileSystem.h"
#include "imgui.h"


StringPool::ResizableStringView Log::StartLine(LogType inType)
{
	auto resizable_str = mStringPool.CreateResizableString();

	if (mAutoAddTime)
	{
		//double time = gTicksToSeconds(gGetTickCount() - gProcessStartTicks);
		//gAppendFormat(resizable_str, "[%.3f] ", time);
		auto time = gGetLocalTime();
		gAppendFormat(resizable_str, "[%02u:%02u:%02u.%02u] ", time.mHour, time.mMinute, time.mSecond, time.mMilliseconds / 10);
	}

	if (mAutoAddErrorTag && inType == LogType::Error)
	{
		resizable_str.Append(cErrorTag);
		resizable_str.Append(" ");
	}

	return resizable_str;
}


void Log::FinishLine(LogType inType, StringPool::ResizableStringView& inLine)
{
	inLine.Append("\n");

	StringView line = inLine.AsStringView();

	// TODO: check inLine for extra end of lines and split it (?), otherwise can't use imgui clipper
	gAssert(gIsNullTerminated(inLine.AsStringView()));

	mLines.emplace_back(line.Data(), line.Size(), inType);
}


StringView Log::Add(LogType inType, StringView inFormat, va_list inArgs)
{
	LockGuard lock(mMutex);

	StringPool::ResizableStringView str = StartLine(inType);

	gAppendFormatV(str, inFormat.AsCStr(), inArgs);

	FinishLine(inType, str);

	return str.AsStringView();
}


void Log::Clear()
{
	LockGuard lock(mMutex);
	mLines.clear();
	mStringPool.Clear();
}


static void sDrawLine(const Log::Line& inLine)
{
	ImVec4 color;
    bool has_color = false;
	if (inLine.mType == LogType::Error)
	{
		color     = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		has_color = true;
	}

    if (has_color)
        ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(inLine.AsStringView());
    if (has_color)
        ImGui::PopStyleColor();
}


void Log::Draw(StringView inName, bool* ioOpen)
{
	{
		LockGuard lock(mMutex);

		ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin(TempString(inName), ioOpen))
		{
			ImGui::End();
			return;
		}
	}


	if (ImGui::Button("Clear"))
		Clear();

	LockGuard lock(mMutex);

	ImGui::Separator();
	mFilter.Draw(R"(Filter ("incl,-excl") ("error"))", 400);
	ImGui::Separator();

	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysHorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		if (mFilter.IsActive())
		{
			// TODO: Can't use clipper with the filter. For very long logs, we should store the filter result instead.
			for (auto line : mLines)
			{
				if (mFilter.PassFilter({ line.mData, line.mData + line.mSize }))
					sDrawLine(line);
			}
		}
		else
		{
			ImGuiListClipper clipper;
			clipper.Begin((int)mLines.size());
			while (clipper.Step())
			{
				for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
				{
					sDrawLine(mLines[line_no]);
				}
			}
			clipper.End();
		}

		// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
		// Using a scrollbar or mouse-wheel will take away from the bottom edge.
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	ImGui::End();
}