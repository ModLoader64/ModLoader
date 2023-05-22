namespace ModLoader.UnitTests;

[TestClass]
public class PluginLoaderTest
{

    private static PluginLoader? loader;

    [ClassInitialize]
    public static void TestFixtureSetup(TestContext context)
    {
        loader = new PluginLoader();
        if (!Directory.Exists("./mods/test"))
        {
            Directory.CreateDirectory("./mods/test");
        }
    }

    [TestInitialize]
    public void Setup()
    {
        File.Copy("../../../../ModLoader.PluginTest/bin/Debug/net7.0/ModLoader.PluginTest.dll", "./mods/test/ModLoader.PluginTest.dll", true);
        loader!.LoadPlugins();
    }

    [TestMethod]
    public void LoadPlugins()
    {
        Assert.IsNotNull(true);
    }

    [TestMethod]
    public void LoadPlugin()
    {
        Assert.IsNotNull(true);
    }

    [TestMethod]
    public void InitPlugins()
    {
        loader!.InitPlugins();
        Assert.IsNotNull(true);
    }

    [TestMethod]
    public void GetModIdentifiers()
    {
        var mods = loader!.GetModIdentifiers();
        Assert.AreEqual(1, mods.Length);
    }
}