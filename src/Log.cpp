#include "Log.h"

#include "imgui.h"

void Log::Add(StringView inString, LogType inType)
{
	// TODO: check inString for end of lines and split it, otherwise can't use imgui clipper
	// TODO: add date/time (optionally)

	size_t alloc_size = inString.size();

	if (inType == LogType::Error)
		alloc_size += cErrorTag.size() + 1;

	auto line_storage = mStringPool.Allocate(alloc_size);
	auto line_ptr     = line_storage;

	if (inType == LogType::Error)
	{
		line_ptr = gAppend(line_ptr, cErrorTag);
		line_ptr = gAppend(line_ptr, " ");
	}

	line_ptr = gAppend(line_ptr, inString);

	gAssert(line_ptr.size() == 1 && line_ptr[0] == 0); // Should have allocated exactly what's needed, only the null terminator is left.

	mLines.push_back(line_storage);
}


void Log::Clear()
{
	mLines.clear();
	mStringPool = {};
}


static void sDrawLine(StringView inLine)
{
	ImVec4 color;
    bool has_color = false;
	if (inLine.starts_with(Log::cErrorTag))
	{
		color     = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		has_color = true;
	}

    if (has_color)
        ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(inLine.data(), inLine.data() + inLine.size());
    if (has_color)
        ImGui::PopStyleColor();
}


void Log::Draw()
{
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Log"))
	{
		ImGui::End();
		return;
	}

	if (ImGui::SmallButton("Clear"))
		Clear();

	ImGui::Separator();
	mFilter.Draw(R"(Filter ("incl,-excl") ("error"))", 400);
	ImGui::Separator();

	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		if (mFilter.IsActive())
		{
			// Can't use clipper with the filter. For very long logs, we should store the filter result instead.
			for (auto line : mLines)
			{
				if (mFilter.PassFilter(line.data(), line.data() + line.size()))
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