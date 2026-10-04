// A core can declare a fixed game mapping, ex:
// #pragma modloader region(cpu, rdram, 0xE0000000, 0xE8000000)
#pragma once

#pragma modloader space(cpu, 0, flat, width = 32, abi = n64)
#pragma modloader space(rdram, 1, word_swapped, 0x07FFFFFF, dirty, width = 32)
#pragma modloader space(rom, 2, big_endian, 0x0FFFFFFF, width = 32)
#pragma modloader space(sp_mem, 3, big_endian, 0x1FFF, width = 32)
#pragma modloader region(cpu, rdram, 0x80000000, 0x88000000)
#pragma modloader region(cpu, rdram, 0xA0000000, 0xA3F00000)
#pragma modloader region(cpu, rom, 0xB0000000, 0xBFC00000)
#pragma modloader region(cpu, sp_mem, 0xA4000000, 0xA4002000)
