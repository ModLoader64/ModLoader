namespace ModLoader.Core
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

    }
}
