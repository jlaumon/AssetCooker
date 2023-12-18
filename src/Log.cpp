#include "Log.h"

#include "imgui.h"

void Log::Add(StringView inString, LogType inType)
{
	// TODO: check inString for end of lines and split it, otherwise can't use imgui clipper
	// TODO: add date/time (optionally)

	size_t alloc_size = inString.size();

	if (inType == LogType::Error)
		alloc_size += cErrorTag.size();

	auto line_storage = mStringPool.Allocate(alloc_size);
	auto line_ptr     = line_storage;

	if (inType == LogType::Error)
		line_ptr = gAppend(line_ptr, cErrorTag);

	line_ptr = gAppend(line_ptr, inString);

	gAssert(line_ptr.empty()); // Should have allocated exactly what's needed.

	mLines.push_back(line_storage);
}


void Log::Clear()
{
	mLines.clear();
	mStringPool = {};
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
					ImGui::TextUnformatted(line.data(), line.data() + line.size());
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
					StringView line = mLines[line_no];
					ImGui::TextUnformatted(line.data(), line.data() + line.size());
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