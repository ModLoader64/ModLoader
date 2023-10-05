namespace ModLoader.API;

public class EventNewFrame : IEvent
{
    public string Id { get; set; } = "EventNewFrame";
    public int frame { get; set; }

    public EventNewFrame(int frame)
    {
        this.frame = frame;
    }
}

public class EventNewVi : IEvent
{
    public string Id { get; set; } = "EventNewVi";

    public EventNewVi()
    {
    }
}

public class EventEmulatorStart : IEvent
{
    public string Id { get; set; } = "OnEmulatorStart";

    public EventEmulatorStart() { }
}

public class EventPluginsLoaded : IEvent
{
    public string Id { get; set; } = "OnPluginInit";

    public EventPluginsLoaded() { }
}

public class EventRomLoaded : IEvent
{
    public string Id { get; set; } = "EventRomLoaded";
    public readonly byte[] rom;

    public EventRomLoaded(byte[] rom) {
        this.rom = rom;
    }
}