namespace ModLoader.API;
public interface PluginInterface
{

    void Init();

    void Destroy();

    void OnTick();

}
