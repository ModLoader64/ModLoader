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

[AttributeUsage(AttributeTargets.Method)]
public class OnInitAttribute : Attribute, IEvent
{
    public string Id { get; set; } = "OnPluginInit";

    public OnInitAttribute() { }
}

[AttributeUsage(AttributeTargets.Class)]
public class BootstrapFilterAttribute: Attribute
{
    public BootstrapFilterAttribute() {}
}

public interface IBootstrapFilter
{
    public static abstract bool DoesLoad();
}