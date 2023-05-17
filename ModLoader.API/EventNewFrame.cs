namespace ModLoader.API;

/// <summary>
/// Event for a new frame being rendered.
/// </summary>
public class EventNewFrame : EventArgs
{

    public readonly int frame;

    public EventNewFrame(int frame)
    {
        this.frame = frame;
    }
}
