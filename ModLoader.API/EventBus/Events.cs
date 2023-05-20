﻿namespace ModLoader.API.EventBus;

public class EventNewFrame : IEvent
{
    public string Id { get; set; } = "OnTick";
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