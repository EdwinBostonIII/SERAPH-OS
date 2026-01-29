# Seraph-V: Type-2 Hypervisor

Seraph-V is a lightweight Type-2 hypervisor that runs Seraph OS on Windows (WHP) and Linux (KVM) for development and testing without requiring bare-metal hardware or UEFI boot.

## Architecture

Seraph-V uses hardware virtualization extensions (Intel VT-x/AMD-V) through platform APIs:
- **Windows**: Windows Hypervisor Platform (WHP)
- **Linux**: Kernel Virtual Machine (KVM)

The hypervisor creates a virtual machine with:
- 512 MB RAM (configurable)
- 1024x768 32-bit framebuffer at guest physical address 0xC0000000
- Emulated LAPIC for timer/interrupt support
- Pre-configured 4-level page tables for long mode

## File Structure

```
tools/seraph-v/
├── main.c          # Entry point, CLI argument parsing
├── seraph_v.h      # Public API and data structures
├── seraph_v.c      # VM creation, memory mapping, page tables
├── vm_run.c        # VM execution loop, MMIO/IO handling
├── elf_loader.c    # ELF64 loader, boot info setup
├── hypercall.c     # Hypercall interface (vmcall)
├── devices.c       # Virtual devices (framebuffer, NIC, clock)
├── dobuild.bat     # Windows build script (MSVC)
└── Makefile        # Linux build (GCC)
```

## File Descriptions

### main.c
Entry point. Parses command-line arguments, initializes WHP/KVM, creates VM, loads kernel ELF, runs VM, and dumps framebuffer on exit. Disables stdout buffering for proper output capture.

**Key functions:**
- `main()` - CLI parsing, VM lifecycle

### seraph_v.h
Public header defining:
- `SeraphV_VM` - VM state structure (partition handle, memory, registers)
- `SeraphV_FramebufferInfo` - Framebuffer dimensions and format
- Error codes (`SV_OK`, `SV_ERROR_*`)
- Memory constants (`SV_FRAMEBUFFER_BASE = 0xC0000000`)

### seraph_v.c
VM creation and memory setup.

**Key functions:**
- `sv_create_vm()` - Creates WHP partition, allocates guest memory, maps via `WHvMapGpaRange`
- `sv_setup_long_mode()` - Configures 4-level page tables:
  - PML4[0] → identity map first 4GB (2MB huge pages)
  - PML4[256] → physical map at 0xFFFF800000000000 (256 x 2MB pages)
  - PML4[384] → kernel heap at 0xFFFFC00000000000 (32 x 2MB pages)
  - PML4[510] → recursive mapping
- `sv_destroy_vm()` - Cleanup and resource release

**Page Table Layout (guest physical addresses):**
```
0x1000: PML4 (Page Map Level 4)
0x2000: PDPT for PML4[0] (identity map)
0x3000: PD0 for first 1GB
0x4000: PD1 for second 1GB
0x5000: PD2 for third 1GB
0x6000: PD3 for fourth 1GB (includes framebuffer)
0x7000: PDPT for PML4[256] (physical map)
0x8000: PD for physical map
0x9000: PDPT for PML4[384] (kernel heap)
0xA000: PD for kernel heap
```

### vm_run.c
VM execution loop handling VM exits.

**Key functions:**
- `sv_run()` - Main execution loop calling `WHvRunVirtualProcessor`
- `handle_mmio()` - Memory-mapped I/O for LAPIC (0xFEE00xxx)
- `handle_io()` - Port I/O for PIT (0x40-0x43), PIC, serial
- `handle_lapic()` - LAPIC register emulation (SVR, timer, TPR)

**VM Exit Handling:**
| Exit Reason | Handler |
|-------------|---------|
| `WHvRunVpExitReasonMemoryAccess` | MMIO (LAPIC registers) |
| `WHvRunVpExitReasonX64IoPortAccess` | Port I/O (PIT, serial) |
| `WHvRunVpExitReasonX64Halt` | HLT instruction (idle loop detection) |
| `WHvRunVpExitReasonX64Cpuid` | CPUID passthrough |
| `WHvRunVpExitReasonHypercall` | Seraph hypercalls |

**Idle Loop Detection:**
After 100 consecutive HLT instructions at the same RIP, dumps framebuffer and exits. This detects when the kernel enters its idle loop.

### elf_loader.c
Loads kernel ELF64 and prepares boot environment.

**Key functions:**
- `sv_load_kernel()` - Parses ELF headers, loads segments to guest memory
- Sets up `Seraph_BootInfo` structure at 0x80000 with:
  - Magic: `SERAPHTB` (0x5345524150484254)
  - Framebuffer: 1024x768 @ 0xC0000000, stride 4096, BGRA8
  - Memory map: 5 descriptors at 0x80118
  - Primordial arena: 16MB at 0x1000000
  - PML4 physical address: 0x1000

**Boot Info Struct Layout (packed, 280 bytes):**
```
Offset  Size  Field
0       8     magic (SERAPHTB)
8       4     version
12      4     flags (FRAMEBUFFER | SERIAL)
16      8     framebuffer_base (0xC0000000)
24      8     framebuffer_size
32      4     fb_width (1024)
36      4     fb_height (768)
40      4     fb_stride (4096)
44      4     fb_format (BGRA8)
48      8     memory_map_base
...
136     8     primordial_arena_phys (0x1000000)
144     8     primordial_arena_size (16MB)
152     8     pml4_phys (0x1000)
```

### hypercall.c
Hypercall interface for guest-to-host communication.

**Hypercall Convention:**
- RAX = hypercall number
- RDI, RSI, RDX, RCX = arguments
- Returns result in RAX

**Hypercall Numbers:**
| Number | Name | Description |
|--------|------|-------------|
| 0 | `SV_HC_PRINT` | Debug print string |
| 1 | `SV_HC_EXIT` | Terminate VM |
| 2 | `SV_HC_GET_TIME` | Get host timestamp |

### devices.c
Virtual device emulation.

**Key functions:**
- `sv_init_framebuffer()` - Allocates 16MB framebuffer, maps at 0xC0000000
- `sv_init_oracle()` - Initializes virtual clock (boot timestamp)
- `sv_init_aether()` - Initializes virtual NIC (MAC, MTU)
- `sv_save_framebuffer_bmp()` - Saves framebuffer to BMP file
- `sv_dump_framebuffer_ascii()` - ASCII art preview of framebuffer

**Framebuffer:**
- Base: 0xC0000000 (guest physical)
- Size: 1024 * 768 * 4 = 3MB (mapped as 16MB region)
- Format: BGRA8 (Blue, Green, Red, Alpha - each 8 bits)
- Directly mapped via `WHvMapGpaRange` (no MMIO trapping)

## Memory Layout

```
Guest Physical Address Space:
0x00000000 - 0x00000FFF: Reserved
0x00001000 - 0x0000AFFF: Page tables (PML4, PDPTs, PDs)
0x00080000 - 0x000801FF: Seraph_BootInfo structure
0x00090000 - 0x0009FFFF: Initial kernel stack
0x00101000 - 0x00333FFF: Kernel code/data (loaded from ELF)
0x01000000 - 0x01FFFFFF: Primordial arena (16MB, pre-zeroed)
0x02000000 - 0x05FFFFFF: Kernel heap backing (64MB)
0xC0000000 - 0xC0FFFFFF: Framebuffer (16MB mapped)
0xFEE00000 - 0xFEE00FFF: LAPIC registers (MMIO trapped)
```

## Building

### Windows (MSVC)
```batch
cd tools\seraph-v
dobuild.bat
```

### Linux (GCC)
```bash
cd tools/seraph-v
make
```

## Usage

```bash
# Run kernel with default settings
seraph-v.exe ..\..\build-win\kernel.elf

# Run with debug output (logs every HLT)
seraph-v.exe -debug ..\..\build-win\kernel.elf
```

## Output Files

- `seraph_framebuffer.bmp` - Framebuffer snapshot at VM exit (1024x768 24-bit BMP)

## LAPIC Emulation

Seraph-V emulates essential LAPIC registers for kernel boot:

| Offset | Register | Behavior |
|--------|----------|----------|
| 0x0F0 | SVR | Read/write, initialized to 0x1FF (APIC enabled) |
| 0x080 | TPR | Read/write task priority |
| 0x320 | LVT Timer | Read/write, masked by default |
| 0x380 | Initial Count | Timer initial value |
| 0x390 | Current Count | Returns 0 (timer not ticking) |
| 0x3E0 | Divide Config | Timer divider |

## Limitations

- No SMP support (single vCPU only)
- No disk emulation (Atlas not available)
- No network I/O (Aether NIC is stub only)
- Timer interrupts not delivered (PIT/LAPIC timer read-only)
- UEFI runtime services not available
