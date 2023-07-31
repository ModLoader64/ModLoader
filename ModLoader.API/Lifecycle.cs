namespace ModLoader.API;

[AttributeUsage(AttributeTargets.Method)]
public class OnFrameAttribute : Attribute, IEvent
{

    public string Id { get; set; } = "EventNewFrame";

    public OnFrameAttribute() { }
}

[AttributeUsage(AttributeTargets.Method)]
public class OnViUpdateAttribute : Attribute, IEvent
{
    public string Id { get; set; } = "EventNewVi";

    public OnViUpdateAttribute() { }
}

[AttributeUsage(AttributeTargets.Method)]
public class OnEmulatorStartAttribute : Attribute, IEvent
{
    public string Id { get; set; } = "OnEmulatorStart";

    public OnEmulatorStartAttribute() { }
}