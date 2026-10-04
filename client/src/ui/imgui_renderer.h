#pragma once

#include "renderer/renderer.h"

struct ImDrawData;

void Imgui_Renderer_Init(Renderer& renderer);
void Imgui_Renderer_Shutdown();
void Imgui_Renderer_Update_Textures();
void Imgui_Renderer_Draw(ImDrawData* data);
