#pragma once

#include "Module.h"
#include "Primitive.h"

#include <string>
#include <vector>
#include <algorithm>
#include <imgui.h>

class RenderLog : public Module<RenderLog>
{
public:
	void RenderBegin(const std::string& scene, uint32_t samples)
	{
		Entry entry;
		entry.scene = scene;
		entry.samples = samples;
		m_Logs.push_back(entry);
	}

	// Logs info about building an accelerator structure. Can be called multiple times per render.
	void AccelInfo(AcceleratorType accel, float time, uint32_t nodeCount, uint32_t byteCount)
	{
		m_Logs.back().accelTime += time;
		m_Logs.back().nodeCount += nodeCount;
		m_Logs.back().bytes += byteCount;
		m_Logs.back().accel = accel;
	}

	void RenderEnd(float renderTime)
	{
		m_Logs.back().renderTime = renderTime;
	}

	// Logs info about a mesh. Can be called multiple times per render.
	void MeshInfo(uint32_t verts, uint32_t faces)
	{
		m_Logs.back().verts += verts;
		m_Logs.back().faces += faces;
	}
	
	void Render(bool disabled)
	{
		if (disabled)
			ImGui::BeginDisabled(disabled);
		ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_SortMulti | ImGuiTableFlags_Sortable |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY;
		if (ImGui::BeginTable("##consoleTable", 10, flags))
		{
			ImGui::TableSetupColumn("Scene");
			ImGui::TableSetupColumn("Vertices");
			ImGui::TableSetupColumn("Faces");
			ImGui::TableSetupColumn("Samples");
			ImGui::TableSetupColumn("Accelerator Structure");
			ImGui::TableSetupColumn("Accelerator Build Time");
			ImGui::TableSetupColumn("Node Count");
			ImGui::TableSetupColumn("Accelerator Memory");
			ImGui::TableSetupColumn("Render Time");
			ImGui::TableSetupColumn("Total Time");
			ImGui::TableHeadersRow();

			for (auto& entry : m_Logs)
			{
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text(entry.scene.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.faces);
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.verts);
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.samples);
				ImGui::TableNextColumn();
				const std::vector<const char*> optionsAcc = { "Octtree", "BVH", "KDTree" };
				ImGui::Text(optionsAcc[(uint32_t)entry.accel]);
				ImGui::TableNextColumn();
				ImGui::Text("%f", entry.accelTime);
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.nodeCount);
				ImGui::TableNextColumn();
				ImGui::Text("%d", entry.bytes);
				ImGui::TableNextColumn();
				ImGui::Text("%f", entry.renderTime);
				ImGui::TableNextColumn();
				ImGui::Text("%f", entry.renderTime + entry.accelTime);
				ImGui::TableNextColumn();
			}

			bool needSort = false;
			ImGuiTableSortSpecs* sortSpec = ImGui::TableGetSortSpecs();
			if (sortSpec && sortSpec->SpecsDirty)
				needSort = true;
			if (needSort)
			{
				sortSpec->SpecsDirty = false;
				std::sort(m_Logs.begin(), m_Logs.end(), [sortSpec](const Entry& l, const Entry& r)
					{
						bool ascending = sortSpec->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
						bool ret = false;
						switch (sortSpec->Specs[0].ColumnIndex)
						{
						case 0: ret = l.scene < r.scene; break;
						case 1: ret = l.verts < r.verts; break;
						case 2: ret = l.faces < r.faces; break;
						case 3: ret = l.samples < r.samples; break;
						case 4: ret = l.accel < r.accel; break;
						case 5: ret = l.accelTime < r.accelTime; break;
						case 6: ret = l.nodeCount < r.nodeCount; break;
						case 7: ret = l.bytes < r.bytes; break;
						case 8: ret = l.renderTime < r.renderTime; break;
						case 9: ret = l.renderTime + l.accelTime < r.renderTime + r.accelTime; break;
						}
						return ascending ? ret : !ret;
					});
				
			}

			ImGui::EndTable();
		}
		if (disabled)
			ImGui::EndDisabled();
	}
private:
	struct Entry
	{
		std::string scene;
		float renderTime = 0;
		float accelTime = 0;
		uint32_t nodeCount = 0;
		uint32_t bytes = 0;
		uint32_t samples = 0;
		uint32_t verts = 0;
		uint32_t faces = 0;
		AcceleratorType accel = AcceleratorType::Octtree;
	};

	Entry m_Tmp;
	std::vector<Entry> m_Logs;
};

#define LOG_RENDER_BEGIN(scene, samples) RenderLog::Get().RenderBegin(scene, samples)
#define LOG_MESH_INFO(verts, faces) RenderLog::Get().MeshInfo(verts,faces);
#define LOG_ACCEL_BUILD(accel, time, nodes, bytes) RenderLog::Get().AccelInfo(accel, time, nodes, bytes)
#define LOG_RENDER_END(time) RenderLog::Get().RenderEnd(time)