#include "Log.h"

#include "imgui.h"

void Log::Add(std::string_view inString, LogType inType)
{
	// TODO: split into lines otherwise can't use imgui clipper
	// TODO: add date/time (optionally)

	size_t alloc_size = inString.size();

	if (inType == LogType::Error)
		alloc_size += cErrorTag.size();

	auto  storage = mStringPool.Allocate(alloc_size);
	char* ptr     = storage.data();

	if (inType == LogType::Error)
	{
		cErrorTag.copy(ptr, cErrorTag.size());
		ptr += cErrorTag.size();
	}

	inString.copy(ptr, inString.size());
	ptr += inString.size();

	// StringPool always allocates one more char for the \0.
	gAssert(ptr == storage.data() + storage.size() && *ptr == 0);

	mLines.push_back({ storage.data(), storage.size() });
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
					std::string_view line = mLines[line_no];
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