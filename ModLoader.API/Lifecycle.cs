namespace ModLoader.API;

public class OnTickAttribute : Attribute
{

    public string Id { get; set; } = "OnTick";

    public OnTickAttribute() { }
}

public class OnViUpdateAttribute : Attribute
{
    public string Id { get; set; } = "OnViUpdate";

    public OnViUpdateAttribute() { }
}