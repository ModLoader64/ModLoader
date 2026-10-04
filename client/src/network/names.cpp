#include "names.h"
#include "protocol.h"

namespace {

constexpr const char* gAdjectives[] = {
    "brave",  "calm",   "clever", "cosmic", "crimson", "curious", "daring", "dusty",  "eager",  "fancy", "fuzzy", "gentle",
    "golden", "grumpy", "happy",  "hidden", "humble",  "jolly",   "lucky",  "mellow", "mighty", "misty", "noble", "odd",
    "proud",  "quick",  "quiet",  "rapid",  "rusty",   "shiny",   "silent", "silver", "sleepy", "sly",   "snowy", "spicy",
    "steady", "sunny",  "swift",  "tiny",   "vivid",   "wild",    "windy",  "wise",   "witty",  "young", "zesty", "zany"
};

constexpr const char* gNouns[] = {
    "badger", "beetle", "boulder", "cactus", "comet",     "cricket",  "cucco",   "deku",    "dodongo",  "falcon",
    "fairy",  "ferret", "gecko",   "goron",  "heron",     "hookshot", "keese",   "lantern", "lizalfos", "lynx",
    "maple",  "meadow", "moblin",  "moon",   "octorok",   "otter",    "owl",     "pebble",  "pumpkin",  "puffin",
    "raven",  "river",  "rupee",   "salmon", "skulltula", "sparrow",  "stalfos", "tektite", "thistle",  "tortoise",
    "walrus", "willow", "wolfos",  "wren",   "yak",       "zora" 
};

u32 Random_Below(u32 bound) {
    u32 value = 0;

    if (!Random_Bytes(&value, sizeof(value))) {
        value = static_cast<u32>(Time_Wall_Microseconds());
    }
    return value % bound;
}

} // namespace

Online_Names Random_Names() {
    const char* adjective = gAdjectives[Random_Below(std::size(gAdjectives))];
    const char* noun = gNouns[Random_Below(std::size(gNouns))];
    Online_Names names;

    names.nickname = Text_Format("%c%s%c%s", adjective[0] - 'a' + 'A', adjective + 1, noun[0] - 'a' + 'A', noun + 1);
    adjective = gAdjectives[Random_Below(std::size(gAdjectives))];
    noun = gNouns[Random_Below(std::size(gNouns))];
    names.lobby = Text_Format("%s-%s-%u", noun, adjective, Random_Below(100));
    return names;
}
