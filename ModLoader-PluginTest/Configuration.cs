using ModLoader_API;
using System.Text.Json.Serialization;

namespace ModLoader_PluginTest
{
    [Configuration]
    public class Configuration
    {
        public int version { get; set; } = 0;
        public string test { get; set; } = "this is a test";

        [JsonIgnore]
        string ignore = "don't save me";
    }
}
