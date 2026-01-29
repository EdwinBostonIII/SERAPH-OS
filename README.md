# SERAPH Operating System

**S**emantic **E**xtensible **R**esilient **A**utomatic **P**ersistent **H**ypervisor

A novel operating system built on VOID semantics, capability-based security, and automatic differentiation.

## Philosophy

SERAPH eliminates entire classes of bugs at the type system level:
- **No null pointer crashes** - VOID propagates instead of crashing
- **No buffer overflows** - Capability tokens enforce bounds
- **No use-after-free** - Generation counters detect stale references
- **No data races** - Effect system tracks mutations at compile time

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest
```

## Project Structure

```
SERAPH-BUILD/
├── boot/              # UEFI bootloader
│   ├── efi_main.c     # UEFI entry point, memory map, GOP setup
│   ├── elf64_loader.c # Loads kernel ELF into memory
│   ├── graphics.c     # Framebuffer initialization
│   └── memory_map.c   # UEFI memory map processing
├── docs/              # Exhaustive documentation
├── include/seraph/    # Public headers
├── src/               # Kernel implementation
│   ├── kmain.c        # Kernel entry, console, init
│   ├── pmm.c          # Physical memory manager
│   ├── vmm.c          # Virtual memory manager
│   ├── early_mem.c    # Bootstrap memory allocator
│   ├── strand.c       # Green threads (Strands)
│   ├── scheduler.c    # Preemptive scheduler
│   ├── apic.c         # Local APIC driver
│   ├── interrupts.c   # IDT and exception handlers
│   └── ...            # See docs/00-overview.md
├── tools/
│   ├── seraphim/      # The SERAPH compiler
│   └── seraph-v/      # Type-2 hypervisor for development
└── tests/             # Test suites
```

## Micro-Concepts (MC)

| MC | Name | Description |
|----|------|-------------|
| MC0 | VOID | Universal error/nothing representation |
| MC1 | VBIT | Three-valued Kleene logic |
| MC2 | Bits | VOID-aware bit operations |
| MC3 | Semantic Byte | Byte with validity mask |
| MC4 | Integers | Entropic arithmetic (VOID/WRAP/SAT) |
| MC5 | Q128 | 64.64 fixed-point numbers |
| MC5+ | Galactic | Hyper-dual automatic differentiation |
| MC6 | Capability | Unforgeable memory access tokens |
| MC7 | Chronon | Causal/logical time |
| MC8 | Arena | Spectral memory allocator |
| MC9 | Glyph | SDF vector graphics |
| MC10 | Sovereign | Process isolation |
| MC11 | Surface | UI compositor |
| MC12 | Whisper | Zero-copy IPC |
| MC13 | Strand | Green threads |

## License

Proprietary - All rights reserved.
