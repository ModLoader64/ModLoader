#pragma once

#include <modloader/types.h>
#include <modloader_module.h>

extern "C" {

MODLOADER_IMPORT("log_source") void ModLoader_Host_Log_Source(u32 level, const char* source, u64 source_length, const char* text, u64 length);
MODLOADER_IMPORT("platform_describe") void ModLoader_Host_Platform_Describe(ModLoader_Platform_Description* description, u64 size);
MODLOADER_IMPORT("platform_space_describe") s32 ModLoader_Host_Platform_Space_Describe(u32 index, ModLoader_Module_Space* space, u64 size);
MODLOADER_IMPORT("platform_processor_describe") s32 ModLoader_Host_Platform_Processor_Describe(u32 index, ModLoader_Module_Processor* processor, u64 size);
MODLOADER_IMPORT("platform_query") s64 ModLoader_Host_Platform_Query(u32 query, void* data, u64 capacity);
MODLOADER_IMPORT("symbol_define") void ModLoader_Host_Symbol_Define(const char* name, u64 length, u64 address);
MODLOADER_IMPORT("symbol_address") u64 ModLoader_Host_Symbol_Address(const char* name, u64 length);
MODLOADER_IMPORT("bind_symbols") u32 ModLoader_Host_Bind_Symbols();
MODLOADER_IMPORT("event_subscribe") void ModLoader_Host_Event_Subscribe(u32 event, u32 enabled);
MODLOADER_IMPORT("invalidate_code") void ModLoader_Host_Invalidate_Code(u64 address, u64 size);
MODLOADER_IMPORT("memory_peek") u64 ModLoader_Host_Memory_Peek(u32 processor, u64 address, void* buffer, u64 size);
MODLOADER_IMPORT("memory_poke") u64 ModLoader_Host_Memory_Poke(u32 processor, u64 address, const void* data, u64 size);
MODLOADER_IMPORT("image_resize") s32 ModLoader_Host_Image_Resize(u64 size);
MODLOADER_IMPORT("translate_address") u64 ModLoader_Host_Translate_Address(u32 processor, u64 address);
MODLOADER_IMPORT("hypercall_allocate") u32 ModLoader_Host_Hypercall_Allocate();
MODLOADER_IMPORT("hypercall_register") u32 ModLoader_Host_Hypercall_Register(u32 id, u64 handler);
MODLOADER_IMPORT("lobby_status") void ModLoader_Host_Lobby_Status(ModLoader_Lobby_Status* status, u64 size);
MODLOADER_IMPORT("lobby_member") u32 ModLoader_Host_Lobby_Member(u32 index, ModLoader_Lobby_Member* member, u64 size);
MODLOADER_IMPORT("lobby_send") u32 ModLoader_Host_Lobby_Send(const char* topic, u64 topic_length, const void* data, u64 size, u32 transport);
MODLOADER_IMPORT("lobby_send_server") u32 ModLoader_Host_Lobby_Send_Server(const char* topic, u64 topic_length, const void* data, u64 size, u32 sealed, u32 transport);
MODLOADER_IMPORT("lobby_read") u64 ModLoader_Host_Lobby_Read(u64 offset, void* buffer, u64 size);
MODLOADER_IMPORT("network_configure") u32 ModLoader_Host_Network_Configure(u32 port);
MODLOADER_IMPORT("server_send") u32 ModLoader_Host_Server_Send(u32 lobby, const void* member, const void* except, const char* topic, u64 topic_length, const void* data, u64 size, u32 sealed, u32 transport);
MODLOADER_IMPORT("ui_texture_create") u64 ModLoader_Host_Texture_Create(u32 width, u32 height, const void* pixels);
MODLOADER_IMPORT("ui_texture_update") u32 ModLoader_Host_Texture_Update(u64 texture, const void* pixels);
MODLOADER_IMPORT("ui_texture_destroy") void ModLoader_Host_Texture_Destroy(u64 texture);
MODLOADER_IMPORT("texture_source_create") u32 ModLoader_Host_Texture_Source_Create(const char* folder, u64 folder_length, const char* id, u64 id_length, const char* name, u64 name_length, const char* path, u64 path_length, u32 enabled);
MODLOADER_IMPORT("texture_source_enable") s32 ModLoader_Host_Texture_Source_Enable(u32 source, u32 enabled);
MODLOADER_IMPORT("texture_source_reload") s32 ModLoader_Host_Texture_Source_Reload(u32 source);
MODLOADER_IMPORT("texture_source_destroy") void ModLoader_Host_Texture_Source_Destroy(u32 source);
MODLOADER_IMPORT("texture_source_state") u32 ModLoader_Host_Texture_Source_State(u32 source);
MODLOADER_IMPORT("texture_source_enabled") s32 ModLoader_Host_Texture_Source_Enabled(u32 source);
MODLOADER_IMPORT("ui_window_create") u32 ModLoader_Host_Window_Create(const char* title, u64 length, u32 width, u32 height);
MODLOADER_IMPORT("ui_window_destroy") void ModLoader_Host_Window_Destroy(u32 window);
MODLOADER_IMPORT("ui_window_show") void ModLoader_Host_Window_Show(u32 window, u32 shown);
MODLOADER_IMPORT("ui_window_shown") u32 ModLoader_Host_Window_Shown(u32 window);
MODLOADER_IMPORT("rml_document_load") u32 ModLoader_Host_Rml_Document_Load(u32 window, const char* folder, u64 folder_length, const char* text, u64 length, u32 from_file);
MODLOADER_IMPORT("rml_font_load") u32 ModLoader_Host_Rml_Font_Load(const char* folder, u64 folder_length, const char* path, u64 length, u32 fallback);
MODLOADER_IMPORT("rml_element_find") u32 ModLoader_Host_Rml_Element_Find(u32 root, u32 by_selector, const char* text, u64 length);
MODLOADER_IMPORT("rml_element_find_all") u32 ModLoader_Host_Rml_Element_Find_All(u32 root, const char* selector, u64 length, u32* out, u32 capacity);
MODLOADER_IMPORT("rml_element_relative") u32 ModLoader_Host_Rml_Element_Relative(u32 element, u32 relation, u32 index);
MODLOADER_IMPORT("rml_element_child_count") u32 ModLoader_Host_Rml_Element_Child_Count(u32 element);
MODLOADER_IMPORT("rml_element_append") u32 ModLoader_Host_Rml_Element_Append(u32 parent, const char* tag, u64 length);
MODLOADER_IMPORT("rml_element_remove") void ModLoader_Host_Rml_Element_Remove(u32 element);
MODLOADER_IMPORT("rml_element_set") u32 ModLoader_Host_Rml_Element_Set(u32 element, u32 what, const char* name, u64 name_length, const char* value, u64 value_length);
MODLOADER_IMPORT("rml_element_get") s64 ModLoader_Host_Rml_Element_Get(u32 element, u32 what, const char* name, u64 name_length, char* buffer, u64 capacity);
MODLOADER_IMPORT("rml_element_action") u32 ModLoader_Host_Rml_Element_Action(u32 element, u32 action, u32 argument);
MODLOADER_IMPORT("rml_element_box") u32 ModLoader_Host_Rml_Element_Box(u32 element, ModLoader_Rml_Box* out);
MODLOADER_IMPORT("rml_element_scroll") void ModLoader_Host_Rml_Element_Scroll(u32 element, f32 left, f32 top);
MODLOADER_IMPORT("rml_element_listen") u32 ModLoader_Host_Rml_Element_Listen(u32 element, const char* event, u64 length, u32 capture, u64 listener);
MODLOADER_IMPORT("rml_element_unlisten") void ModLoader_Host_Rml_Element_Unlisten(u32 listener);
MODLOADER_IMPORT("rml_model_create") u32 ModLoader_Host_Rml_Model_Create(u32 window, const char* name, u64 length);
MODLOADER_IMPORT("rml_model_bind") u32 ModLoader_Host_Rml_Model_Bind(u32 model, const char* name, u64 length, u32 type, void* address, u32 size, u32 stride, const u32* count);
MODLOADER_IMPORT("rml_model_bind_event") u32 ModLoader_Host_Rml_Model_Bind_Event(u32 model, const char* name, u64 length, u64 listener);
MODLOADER_IMPORT("rml_model_dirty") void ModLoader_Host_Rml_Model_Dirty(u32 model, const char* name, u64 length);
MODLOADER_IMPORT("savestate_block") u64 ModLoader_Host_Savestate_Block(const char* owner, u64 owner_length, const char* reason, u64 length);
MODLOADER_IMPORT("savestate_unblock") void ModLoader_Host_Savestate_Unblock(u64 token);
MODLOADER_IMPORT("setting_declare") s32 ModLoader_Host_Setting_Declare(const char* folder, u64 length, const ModLoader_Module_Setting* setting);
MODLOADER_IMPORT("setting_get") s64 ModLoader_Host_Setting_Get(const char* folder, u64 length, const char* key, char* buffer, u64 capacity);
MODLOADER_IMPORT("setting_set") s32 ModLoader_Host_Setting_Set(const char* folder, u64 length, const char* key, const char* value);
MODLOADER_IMPORT("emulation_pause") void ModLoader_Host_Emulation_Pause(u32 paused);
MODLOADER_IMPORT("emulation_paused") u32 ModLoader_Host_Emulation_Paused();
MODLOADER_IMPORT("emulation_restart") void ModLoader_Host_Emulation_Restart();
MODLOADER_IMPORT("thread_sleep") void ModLoader_Host_Thread_Sleep(u32 milliseconds);
MODLOADER_IMPORT("thread_is_emulation") u32 ModLoader_Host_Thread_Is_Emulation();
MODLOADER_IMPORT("time_milliseconds") u64 ModLoader_Host_Time_Milliseconds();
MODLOADER_IMPORT("task_submit") u32 ModLoader_Host_Task_Submit(u64 context);
MODLOADER_IMPORT("task_drain") void ModLoader_Host_Task_Drain();
MODLOADER_IMPORT("task_cancelled") u32 ModLoader_Host_Task_Cancelled();
MODLOADER_IMPORT("dispatch_request") void ModLoader_Host_Dispatch_Request();
MODLOADER_IMPORT("clock_now") u64 ModLoader_Host_Clock_Now(u32 clock);
MODLOADER_IMPORT("guest_copy") u32 ModLoader_Host_Guest_Copy(u64 destination, u64 source, u64 size);
MODLOADER_IMPORT("lobby_send_to") u32 ModLoader_Host_Lobby_Send_To(const void* member, const char* topic, u64 topic_length, const void* data, u64 size, u32 transport);
MODLOADER_IMPORT("file_open") u32 ModLoader_Host_File_Open(const char* folder, u64 folder_length, const char* path, u64 length, u32 mode);
MODLOADER_IMPORT("file_read") s64 ModLoader_Host_File_Read(u32 file, void* buffer, u64 size, u64 offset);
MODLOADER_IMPORT("file_write") s64 ModLoader_Host_File_Write(u32 file, const void* data, u64 size, u64 offset);
MODLOADER_IMPORT("file_size") s64 ModLoader_Host_File_Size(u32 file);
MODLOADER_IMPORT("file_close") void ModLoader_Host_File_Close(u32 file);
MODLOADER_IMPORT("file_delete") u32 ModLoader_Host_File_Delete(const char* folder, u64 folder_length, const char* path, u64 length);
MODLOADER_IMPORT("file_exists") u32 ModLoader_Host_File_Exists(const char* folder, u64 folder_length, const char* path, u64 length);
MODLOADER_IMPORT("file_create_directory") u32 ModLoader_Host_File_Create_Directory(const char* folder, u64 folder_length, const char* path, u64 length);
MODLOADER_IMPORT("file_list") s64 ModLoader_Host_File_List(const char* folder, u64 folder_length, const char* path, u64 length, char* buffer, u64 capacity);
MODLOADER_IMPORT("tcp_connect") u32 ModLoader_Host_Tcp_Connect(const char* host, u64 length, u32 port, u32 timeout);
MODLOADER_IMPORT("tcp_listen") u32 ModLoader_Host_Tcp_Listen(const char* address, u64 length, u32 port);
MODLOADER_IMPORT("tcp_accept") u32 ModLoader_Host_Tcp_Accept(u32 listener, u32 timeout);
MODLOADER_IMPORT("socket_port") u32 ModLoader_Host_Socket_Port(u32 socket);
MODLOADER_IMPORT("socket_send") s64 ModLoader_Host_Socket_Send(u32 socket, const void* data, u64 size);
MODLOADER_IMPORT("socket_receive") s64 ModLoader_Host_Socket_Receive(u32 socket, void* buffer, u64 size, u32 timeout);
MODLOADER_IMPORT("udp_open") u32 ModLoader_Host_Udp_Open(u32 port);
MODLOADER_IMPORT("udp_send_to") s64 ModLoader_Host_Udp_Send_To(u32 socket, const char* host, u64 length, u32 port, const void* data, u64 size);
MODLOADER_IMPORT("udp_receive_from") s64 ModLoader_Host_Udp_Receive_From(u32 socket, void* buffer, u64 size, u32* out_from, u32 timeout);
MODLOADER_IMPORT("socket_close") void ModLoader_Host_Socket_Close(u32 socket);
MODLOADER_IMPORT("socket_packets") u32 ModLoader_Host_Socket_Packets(u32 socket);
MODLOADER_IMPORT("socket_packet_send") u32 ModLoader_Host_Socket_Packet_Send(u32 socket, const char* type, u64 length, u32 version, const void* data, u64 size);
MODLOADER_IMPORT("socket_packet_send_to") u32 ModLoader_Host_Socket_Packet_Send_To(u32 socket, const char* type, u64 length, u32 version, const void* data, u64 size, u32 address, u32 port);
MODLOADER_IMPORT("lobby_packet_follow") u32 ModLoader_Host_Lobby_Packet_Follow(u32 event, const char* type, u64 length, u32 enabled);
MODLOADER_IMPORT("asm_assemble")
s64 ModLoader_Host_Asm_Assemble(u64 origin, const char* source, u64 source_length, void* output, u64 capacity, char* error, u64 error_capacity);
MODLOADER_IMPORT("sig_scan") u64 ModLoader_Host_Sig_Scan(u32 space, const char* pattern, u64 length, u64 from, u64 to);
MODLOADER_IMPORT("sig_scan_unique")
u64 ModLoader_Host_Sig_Scan_Unique(u32 space, const char* pattern, u64 length, u64 from, u64 to);
MODLOADER_IMPORT("asm_disassemble")
u32 ModLoader_Host_Asm_Disassemble(u32 instruction, u64 pc, char* out, u64 capacity);
MODLOADER_IMPORT("breakpoint_add") u32 ModLoader_Host_Breakpoint_Add(u32 processor, u64 address, u64 size, u32 access, u64 handler);
MODLOADER_IMPORT("breakpoint_remove") void ModLoader_Host_Breakpoint_Remove(u32 id);
MODLOADER_IMPORT("breakpoint_resume") void ModLoader_Host_Breakpoint_Resume(u32 processor, u32 step, u64 handler);
MODLOADER_IMPORT("breakpoint_paused") u32 ModLoader_Host_Breakpoint_Paused();
MODLOADER_IMPORT("breakpoint_cpu") u32 ModLoader_Host_Breakpoint_Cpu(u32 processor, void* state, u64 size, u32 write);
MODLOADER_IMPORT("log") void ModLoader_Host_Log(u32 level, const char* text, u64 length);
MODLOADER_IMPORT("abort_with") void ModLoader_Host_Abort_With(const char* text, u64 length);
MODLOADER_IMPORT("time_now") u64 ModLoader_Host_Time_Now(u32 clock);
MODLOADER_IMPORT("thread_spawn") u32 ModLoader_Host_Thread_Spawn(u64 context);
MODLOADER_IMPORT("thread_join") void ModLoader_Host_Thread_Join(u32 thread);
MODLOADER_IMPORT("cpu_count") u32 ModLoader_Host_Cpu_Count();
MODLOADER_IMPORT("libc_format_float") s64 ModLoader_Host_Format_Float(char* buffer, u64 capacity, u64 bits, u32 conversion, u32 flags, s32 min_width, s32 precision);
MODLOADER_IMPORT("libc_parse_float") void ModLoader_Host_Parse_Float(const char* text, u32 is_double, void* out);

}
