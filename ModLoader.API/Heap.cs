namespace ModLoader.API;

public delegate void HeapBlockMovedCallback();

public class HeapBlock
{
    public Ptr start;
    public int size;
    public bool isFree = false;
    public HeapBlockMovedCallback? callback;

    public HeapBlock(Ptr start, int size)
    {
        this.start = start;
        this.size = size;
    }
}

public class Heap
{

    private Ptr start;
    private int size;
    private int allocedBytes = 0;
    private List<HeapBlock> blocks = new List<HeapBlock>();

    public Heap(Ptr start, int size)
    {
        this.start = start;
        this.size = size;
    }

    public HeapBlock? Alloc(int size) {
        if (allocedBytes >= size) return null;
        allocedBytes += size;
        var block = new HeapBlock(start + allocedBytes, size);
        blocks.Add(block);
        return block;
    }

    public void Free(HeapBlock block)
    {
        block.isFree = true;
    }

    public void Defrag() {
        List<HeapBlock> liveBlocks = new List<HeapBlock>();
        foreach (var block in blocks)
        {
            if (!block.isFree) liveBlocks.Add(block);
        }
        for (int i = 0; i < liveBlocks.Count; i++)
        {
            if (i == 0)
            {
                if (liveBlocks[i].start != start)
                {
                    // Block 0 isn't at the top.
                    var oldStart = liveBlocks[i].start;
                    liveBlocks[i].start = start;
                    for (int j = 0; j < liveBlocks[i].size; j++)
                    {
                        Memory.RAM.WriteU8((uint)liveBlocks[i].start, Memory.RAM.ReadU8((uint)(oldStart + j)));
                    }
                    liveBlocks[i].callback?.Invoke();
                }
            }
            else
            {
                int upperBlockEnd = (int)(liveBlocks[i - 1].start + liveBlocks[i].size);
                if (liveBlocks[i].start != upperBlockEnd)
                {
                    // Block isn't aligned with upper block. Move it.
                    var oldStart = liveBlocks[i].start;
                    liveBlocks[i].start = upperBlockEnd;
                    for (int j = 0; j < liveBlocks[i].size; j++)
                    {
                        Memory.RAM.WriteU8((uint)liveBlocks[i].start, Memory.RAM.ReadU8((uint)(oldStart + j)));
                    }
                    liveBlocks[i].callback?.Invoke();
                }
            }
        }
        blocks.Clear();
        foreach(var block in liveBlocks)
        {
            blocks.Add(block);
        }
    }

}
