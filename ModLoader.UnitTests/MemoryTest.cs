using ModLoader.API;

namespace ModLoader.UnitTests;

[TestClass]
public class MemoryTest
{

    private static BindingLoader? bindings;

    [ClassInitialize]
    public static void TestFixtureSetup(TestContext context)
    {
        bindings = new BindingLoader();
    }

    [TestInitialize]
    public void Setup()
    {
        Memory.RAM.WriteU8(0x69, 0x69);
    }

    [TestMethod]
    public void WriteU8()
    {
        Memory.RAM.WriteU8(0x420, 0xFF);
        var b = Memory.RAM.ReadU8(0x420);
        Assert.AreEqual(b, 0xFF);
    }

    [TestMethod]
    public void ReadU8()
    {
        var b = Memory.RAM.ReadU8(0x69);
        Assert.AreEqual(b, 0x69);
    }

}
