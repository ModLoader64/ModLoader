#include "ui/textures.h"
#include "../textures.h"
#include "modloader_texture.h"
#include "imgui.h"

namespace {

bool sOpen;

const char* State_Name(u32 state) {
    switch (state) {
    case MODLOADER_TEXTURE_DISABLED:
        return "Disabled";
    case MODLOADER_TEXTURE_PENDING:
        return "Pending";
    case MODLOADER_TEXTURE_READY:
        return "Ready";
    case MODLOADER_TEXTURE_UNSUPPORTED:
        return "Unsupported";
    case MODLOADER_TEXTURE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

void Draw_Source(Texture_Manager& manager, const Texture_Source_View& source, usize index, usize count) {
    bool enabled = source.enabled;

    ImGui::PushID(static_cast<s32>(source.handle));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    if (ImGui::Checkbox("##enabled", &enabled)) {
        manager.Enable(source.handle, enabled);
    }

    ImGui::TableNextColumn();
    ImGui::BeginDisabled(index == 0);

    if (ImGui::ArrowButton("up", ImGuiDir_Up)) {
        manager.Move(source.handle, -1);
    }

    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 == count);

    if (ImGui::ArrowButton("down", ImGuiDir_Down)) {
        manager.Move(source.handle, 1);
    }

    ImGui::EndDisabled();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(source.name.c_str());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s / %s\n%s", source.owner.c_str(), source.id.c_str(), source.path.c_str());
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(State_Name(source.state));

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        if (source.state == MODLOADER_TEXTURE_ERROR) {
            ImGui::SetTooltip("error");
        }
        else if (source.state == MODLOADER_TEXTURE_UNSUPPORTED) {
            ImGui::SetTooltip("The current renderer does not support texture replacements.");
        }
    }

    ImGui::TableNextColumn();
    ImGui::BeginDisabled(!enabled || source.state == MODLOADER_TEXTURE_UNSUPPORTED);

    if (ImGui::Button("Reload")) {
        manager.Reload(source.handle);
    }

    ImGui::EndDisabled();
    ImGui::PopID();
}

} // namespace

void Textures_Menu() {
    ImGui::MenuItem("Texture replacements", nullptr, &sOpen);
}

void Textures_Build(Texture_Manager& manager) {
    if (!sOpen) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 42.0f, ImGui::GetFontSize() * 22.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture replacements", &sOpen)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Higher sources override matching textures from lower sources");
    ImGui::Spacing();
    std::vector<Texture_Source_View> sources = manager.Snapshot();

    if (sources.empty()) {
        ImGui::TextDisabled("No texture sources");
    }

    else if (ImGui::BeginTable("sources", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Enabled");
        ImGui::TableSetupColumn("Priority");
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("##reload");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (usize index = 0; index < sources.size(); index++) {
            Draw_Source(manager, sources[index], index, sources.size());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
