#pragma once

#include <imgui.h>

// Pass to DockBuilderAddNode for a dockspace root
constexpr ImGuiDockNodeFlags gImguiDockNodeDockSpace = 1 << 10;

namespace ImGui {

bool DockBuilderHasNode(ImGuiID node_id);
ImGuiID DockBuilderAddNode(ImGuiID node_id = 0, ImGuiDockNodeFlags flags = 0);
void DockBuilderRemoveNode(ImGuiID node_id);
void DockBuilderSetNodeSize(ImGuiID node_id, ImVec2 size);
ImGuiID DockBuilderSplitNode(ImGuiID node_id, ImGuiDir split_dir, float size_ratio_for_node_at_dir, ImGuiID* out_id_at_dir, ImGuiID* out_id_at_opposite_dir);
void DockBuilderDockWindow(const char* window_name, ImGuiID node_id);
void DockBuilderFinish(ImGuiID node_id);

} // namespace ImGui
