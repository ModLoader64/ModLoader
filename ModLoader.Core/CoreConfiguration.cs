using ModLoader.API;
using System.Reflection;
using System.Text.Json;

namespace ModLoader.Core
{

    public class NetworkConfiguration
    {
        public string nickname { get; set; } = "";
        public string server_ip { get; set; } = "127.0.0.1";
        public int port { get; set; } = 25565;
        public string lobby { get; set; } = RandomLobbyName.GetRandomLobbyName();
        public bool isServer { get; set; } = false;
        public bool isClient { get; set; } = true;
    }

    public class ClientConfiguration
    {
        public string rom { get; set; } = "Legend of Zelda, The - Ocarina of Time (U) (V1.0) [!].z64";
        public string patch { get; set; } = "";
    }

    [Configuration]
    public class CoreConfiguration
    {
        public NetworkConfiguration multiplayer { get; set; } = new NetworkConfiguration();
        public ClientConfiguration client { get; set; } = new ClientConfiguration();
    }

    public class CoreConfigurationHandler
    {

        public static CoreConfiguration config = new CoreConfiguration();

        public static void SetupCoreConfiguration()
        {
            Type type = typeof(CoreConfigurationHandler);
            var fields = type.GetRuntimeFields();
            foreach (var field in fields)
            {
                if (Attribute.GetCustomAttribute(field.FieldType, typeof(ConfigurationAttribute)) != null)
                {
                    Console.WriteLine($"Found [Configuration] in {field}");
                    var configFile = Path.GetFullPath($"./config/ModLoader.Core.json");
                    if (!File.Exists(configFile))
                    {
                        Console.WriteLine("Config file does not exist for core context. Creating...");
                        field.SetValue(null, Activator.CreateInstance(field.FieldType));
                        var options = new JsonSerializerOptions { WriteIndented = true };
                        string json = JsonSerializer.Serialize(field.GetValue(null), options);
                        File.WriteAllText(configFile, json);
                        Console.WriteLine(json);
                    }
                    else
                    {
                        Console.WriteLine("Loading config file for core...");
                        string json = File.ReadAllText(configFile);
                        field.SetValue(null, JsonSerializer.Deserialize(json, field.FieldType));
                    }
                }
            }
        }
    }
}
