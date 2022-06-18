#pragma once

#include "FileSystem.h"

#include <vector>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

static void ShiftCursor(float x, float y)
{
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y);
}

static inline ImRect GetItemRect() { return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }

static inline ImRect RectExpanded(const ImRect& rect, float x, float y)
{
	ImRect result = rect;
	result.Min.x -= x;
	result.Min.y -= y;
	result.Max.x += x;
	result.Max.y += y;
	return result;
}

static inline ImRect RectOffset(const ImRect& rect, float x, float y)
{
	ImRect result = rect;
	result.Min.x += x;
	result.Min.y += y;
	result.Max.x += x;
	result.Max.y += y;
	return result;
}

static void ShiftCursorX(float x) { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x); }

static void ShiftCursorY(float y) { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y); }

static bool IsItemDisabled() { return ImGui::GetItemFlags() & ImGuiItemFlags_Disabled; }

static void DrawItemActivityOutline(float rounding = 0.0f, bool drawWhenInactive = false,
	ImColor colourWhenActive = ImColor(80, 80, 80))
{
	auto* drawList = ImGui::GetWindowDrawList();
	const ImRect rect = RectExpanded(GetItemRect(), 1.0f, 1.0f);
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
	{
		drawList->AddRect(rect.Min, rect.Max, ImColor(60, 60, 60), rounding, 0, 1.5f);
	}
	if (ImGui::IsItemActive())
	{
		drawList->AddRect(rect.Min, rect.Max, colourWhenActive, rounding, 0, 1.0f);
	}
	else if (!ImGui::IsItemHovered() && drawWhenInactive)
	{
		drawList->AddRect(rect.Min, rect.Max, ImColor(50, 50, 50), rounding, 0, 1.0f);
	}
}

static void Underline(bool fullWidth = false, float offsetX = 0.0f, float offsetY = -1.0f)
{
	if (fullWidth)
	{
		if (ImGui::GetCurrentWindow()->DC.CurrentColumns != nullptr)
			ImGui::PushColumnsBackground();
		else if (ImGui::GetCurrentTable() != nullptr)
			ImGui::TablePushBackgroundChannel();
	}

	const float width = fullWidth ? ImGui::GetWindowWidth() : ImGui::GetContentRegionAvail().x;
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddLine(ImVec2(cursor.x + offsetX, cursor.y + offsetY),
		ImVec2(cursor.x + width, cursor.y + offsetY), IM_COL32(26, 26, 26, 255),
		1.0f);

	if (fullWidth)
	{
		if (ImGui::GetCurrentWindow()->DC.CurrentColumns != nullptr)
			ImGui::PopColumnsBackground();
		else if (ImGui::GetCurrentTable() != nullptr)
			ImGui::TablePopBackgroundChannel();
	}
}

static void Pre(const char* label)
{
	ShiftCursor(10.0f, 9.0f);
	ImGui::Text(label);
	ImGui::NextColumn();
	ShiftCursorY(4.0f);
	ImGui::PushItemWidth(-1);

	if (IsItemDisabled())
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
}

static void Post()
{
	if (IsItemDisabled())
		ImGui::PopStyleVar();

	if (!IsItemDisabled())
		DrawItemActivityOutline(2.0f, true, IM_COL32(236, 158, 36, 255));

	ImGui::PopItemWidth();
	ImGui::NextColumn();
	Underline();
}

static void BeginPropertyGrid()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
	ImGui::Columns(2);
}

static void EndPropertyGrid()
{
	ImGui::Columns(1);
	Underline();
	ImGui::PopStyleVar(2); // ItemSpacing, FramePadding
	ShiftCursorY(18.0f);
}

template <typename TEnum, typename TUnderlying = int32_t>
static bool PropertyDropdown(const char* label, const std::vector<const char*>& options, TEnum& selected)
{
	TUnderlying selectedIndex = (TUnderlying)selected;

	const char* current = options[selectedIndex];
	Pre(label);
	bool modified = false;
	if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
		current = "---";

	const std::string id = "##" + std::string(label);
	if (ImGui::BeginCombo(id.c_str(), current))
	{
		for (int i = 0; i < options.size(); i++)
		{
			const bool is_selected = (current == options[i]);
			if (ImGui::Selectable(options[i], is_selected))
			{
				current = options[i];
				selected = (TEnum)i;
				modified = true;
			}
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	Post();

	return modified;
}

static bool DragUInt32(const char* label, uint32_t* v, float v_speed = 1.0f, uint32_t v_min = 0, uint32_t v_max = 0,
	const char* format = "%d", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragScalar(label, ImGuiDataType_U32, v, v_speed, &v_min, &v_max, format, flags);
}

static bool Property(const char* label, uint32_t& value, uint32_t minValue = 0, uint32_t maxValue = 0)
{
	Pre(label);
	std::string lbl = "##" + std::string(label);
	bool modified = DragUInt32(lbl.c_str(), &value, 1.0f, minValue, maxValue);
	Post();

	return modified;
}

static bool PropertyFilepath(const char* label, std::string& value)
{
	ShiftCursor(10.0f, 9.0f);
	ImGui::Text(label);
	ImGui::NextColumn();
	ShiftCursorY(4.0f);
	const auto& style = ImGui::GetStyle();
	ImGui::PushItemWidth(-34.0f);
	if (IsItemDisabled())
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

	std::string lbl = "##" + std::string(label);
	bool modified = ImGui::InputText(lbl.c_str(), &value);
	ImGui::SameLine();
	const ImColor c_ButtonTint = IM_COL32(192, 192, 192, 255);
	if (ImGui::Button("..."))
	{
		std::vector<Path> outPaths;
		if (FileSystem::OpenFileDialog(outPaths) && outPaths.size() > 0)
		{
			value = outPaths[0].string();
			modified = true;
		}
	}

	Post();

	return modified;
}