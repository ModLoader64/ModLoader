namespace ModLoader.API;

public class OnFrameAttribute : Attribute
{

    public string Id { get; set; } = "OnFrame";

    public OnFrameAttribute() { }
}

public class OnViUpdateAttribute : Attribute
{
    public string Id { get; set; } = "OnViUpdate";

    public OnViUpdateAttribute() { }
}