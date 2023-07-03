using ImGuiNET;
using OpenTK;
using OpenTK.Windowing.Desktop;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Graphics.OpenGL;

namespace ModLoader.API;

public class ImGuiManager : GameWindow {
    private readonly Color4 ClearColor = new Color4(0, 0, 0, 0);

    private ImGuiController? Controller = null;

    public ImGuiManager() : base(GameWindowSettings.Default, new NativeWindowSettings() { Size = new Vector2i(640, 480), APIVersion = new Version(3, 3)}) {
        WindowBorder = WindowBorder.Hidden;
    }

    protected override void OnLoad() {
        base.OnLoad();
        Title = "";
        Controller = new ImGuiController(ClientSize.X, ClientSize.Y);
    }

    protected override void OnResize(ResizeEventArgs e) {
        base.OnResize(e);
        GL.Viewport(0, 0, ClientSize.X, ClientSize.Y);
        if (Controller != null) {
            Controller.WindowResized(ClientSize.X, ClientSize.Y);
        }
    }

    protected override void OnRenderFrame(FrameEventArgs args) {
        base.OnRenderFrame(args);

        if (Controller != null) {
            Controller.Update(this, (float)args.Time);
        }

        GL.ClearColor(ClearColor);
        GL.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit | ClearBufferMask.StencilBufferBit);

        ImGui.ShowDemoWindow();

        if (Controller != null) {
            Controller.Render();
        }

        SwapBuffers();
    }

    protected override void OnTextInput(TextInputEventArgs e) {
        base.OnTextInput(e);
        if (Controller != null) {
            Controller.PressChar((char)e.Unicode);
        }
    }

    protected override void OnMouseWheel(MouseWheelEventArgs e) {
        base.OnMouseWheel(e);
        if (Controller != null) {
            Controller.MouseScroll(e.Offset);
        }
    }
}

