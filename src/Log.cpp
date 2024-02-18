#include "Log.h"

#include "FileSystem.h"
#include "imgui.h"
#include "Ticks.h"


StringPool::ResizableStringView Log::StartLine(LogType inType)
{
	auto resizable_str = mStringPool.CreateResizableString();

	if (mAutoAddTime)
	{
		//double time = gTicksToSeconds(gGetTickCount() - gProcessStartTicks);
		//resizable_str.AppendFormat("[{:.3f}] ", time);
		auto time = gGetLocalTime();
		resizable_str.AppendFormat("[{:02}:{:02}:{:02}.{:02}] ", time.mHour, time.mMinute, time.mSecond, time.mMilliseconds / 10);
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
	StringView line = inLine.AsStringView();

	// TODO: check inString for end of lines and split it, otherwise can't use imgui clipper
	gAssert(gIsNullTerminated(line));

	mLines.emplace_back(line.data(), (int)line.size(), inType);
}


StringView Log::Add(LogType inType, StringView inFmt, fmt::format_args inArgs)
{
	std::lock_guard lock(mMutex);

	StringPool::ResizableStringView str = StartLine(inType);

	size_t size_before_format = str.AsStringView().size();
	str.AppendFormatV(inFmt, inArgs);
	size_t size_after_format = str.AsStringView().size();

	FinishLine(inType, str);

	return { str.mData + size_before_format, str.mData + size_after_format };
}


void Log::Clear()
{
	std::lock_guard lock(mMutex);
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
		std::lock_guard lock(mMutex);

		ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
		TempString32 title("{}", inName);
		if (!ImGui::Begin(title.AsCStr(), ioOpen))
		{
			ImGui::End();
			return;
		}
	}


	if (ImGui::ButtonGrad("Clear"))
		Clear();

	std::lock_guard lock(mMutex);

	ImGui::Separator();
	mFilter.Draw(R"(Filter ("incl,-excl") ("error"))", 400);
	ImGui::Separator();

	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysHorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		if (mFilter.IsActive())
		{
			// Can't use clipper with the filter. For very long logs, we should store the filter result instead.
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