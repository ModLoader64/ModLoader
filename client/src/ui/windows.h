#pragma once

#include "module.h"
#include "renderer/renderer.h"

union SDL_Event;
struct SDL_Window;

namespace Rml {
class Context;
} // namespace Rml

void Ui_Windows_Register_Natives();
void Ui_Windows_Start(Renderer& renderer, SDL_Window* main_window);
void Ui_Windows_Stop();
void Ui_Windows_Release_Owner(const Module& owner);
void Ui_Windows_Frame_Begin();
void Ui_Windows_Frame_End();
bool Ui_Windows_Event(const SDL_Event& event);
void Ui_Windows_Main_Event(const SDL_Event& event);
bool Ui_Windows_Wants_Keyboard();
void Ui_Windows_Draw_Overlays(Renderer& renderer);
void Ui_Windows_Present(Renderer& renderer);

Rml::Context* Ui_Windows_Context(const Module& owner, u32 window);
Module* Ui_Windows_Owner(Rml::Context* context);
Module* Ui_Windows_Work_For(Module* module);
