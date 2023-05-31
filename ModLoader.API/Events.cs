namespace ModLoader.API;

public class EventNewFrame : IEvent
{
    public string Id { get; set; } = "OnFrame";
    public int frame { get; set; }

    public EventNewFrame(int frame)
    {
        this.frame = frame;
    }
}

public class EventNewVi : IEvent
{
    public string Id { get; set; } = "OnViUpdate";

    public EventNewVi()
    {
    }
}


public class EventEmulatorStart : IEvent
{
    public string Id { get; set; } = "OnEmulatorStart";

    public EventEmulatorStart() { }
}