using Network.Converter;
using Network.Packets;
using Newtonsoft.Json;
using System.Reflection;

namespace ModLoader.API
{
    public static class Utils
    {

        public static string GetHashSHA1(this byte[] data)
        {
            using (var sha1 = System.Security.Cryptography.SHA1.Create())
            {
                return string.Concat(sha1.ComputeHash(data).Select(x => x.ToString("X2")));
            }
        }

        public static byte[] ObjectToByteArray<T>(T obj) {
            JsonSerializerSettings jsonSettings = new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };
            var str = JsonConvert.SerializeObject(obj, typeof(object), jsonSettings);
            var raw = RawDataConverter.FromASCIIString("ModData", str);
            var bytes = raw.Data;
            return bytes;
        }

        public static T ByteArrayToObject<T>(byte[] bytes) {
            JsonSerializerSettings jsonSettings = new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };
            RawData raw = (RawData)typeof(RawData).GetConstructor(BindingFlags.NonPublic | BindingFlags.Instance, null, Type.EmptyTypes, null).Invoke(new object[] { "ModData", bytes });
            var str = RawDataConverter.ToASCIIString(raw);
            var obj = JsonConvert.DeserializeObject<T>(str);
            return obj;
        }
    }
   
}
