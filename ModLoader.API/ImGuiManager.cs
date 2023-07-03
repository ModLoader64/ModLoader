using ImGuiNET;
using System.Runtime.InteropServices;

namespace ModLoader.API;

public static class ImGuiManager {
    public static IntPtr Context = IntPtr.Zero;
    public static ImGuiIOPtr IO = null;

    private static IntPtr GLVersionString = IntPtr.Zero;

    public static void Initialize() {
        Context = ImGui.CreateContext();
        ImGui.SetCurrentContext(Context);
        IO = ImGui.GetIO();
        IO.ConfigFlags |= ImGuiConfigFlags.NavEnableKeyboard | ImGuiConfigFlags.NavEnableGamepad;
        ImGui.StyleColorsDark();

        GLVersionString = Marshal.StringToHGlobalAnsi("#version 330");
        ImGui_ImplOpenGL3_Init(GLVersionString);
    }

    public static void NewFrame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui.NewFrame();

        ImGui.ShowDemoWindow();
    }

    public static void EndFrame() {
        ImGui.Render();
        unsafe {
            ImGui_ImplOpenGL3_RenderDrawData((IntPtr)ImGui.GetDrawData().NativePtr);
        }
    }

    public static void Dispose() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui.DestroyContext();

        Marshal.FreeHGlobal(GLVersionString);
    }

    /// Backend impl stuff
    // opengl3
    [DllImport("cimgui.dll")]
    private static extern bool ImGui_ImplOpenGL3_Init(IntPtr glsl_version_string);

    [DllImport("cimgui.dll")]
    private static extern void ImGui_ImplOpenGL3_Shutdown();

    [DllImport("cimgui.dll")]
    private static extern void ImGui_ImplOpenGL3_NewFrame();

    [DllImport("cimgui.dll")]
    private static extern void ImGui_ImplOpenGL3_RenderDrawData(IntPtr draw_data);

    [DllImport("cimgui.dll")]
    private static extern bool ImGui_ImplOpenGL3_CreateFontsTexture();

    [DllImport("cimgui.dll")]
    private static extern void ImGui_ImplOpenGL3_DestroyFontsTexture();

    [DllImport("cimgui.dll")]
    private static extern bool ImGui_ImplOpenGL3_CreateDeviceObjects();

    [DllImport("cimgui.dll")]
    private static extern void ImGui_ImplOpenGL3_DestroyDeviceObjects();
}

