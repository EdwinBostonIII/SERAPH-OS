# SERAPH Glossary

## Core Concepts

### VOID
The universal representation of "nothing," "error," or "undefined" in SERAPH. Represented as all-1s bit pattern (0xFF, 0xFFFF, etc.). VOID propagates through operations automatically, eliminating the need for explicit error checking at every step.

### VBIT
A three-valued logical type implementing Kleene's strong three-valued logic. Values: TRUE (0x01), FALSE (0x00), VOID (0xFF). Supports short-circuit evaluation where FALSE dominates AND and TRUE dominates OR.

### Semantic Byte
A 16-bit value combining 8 bits of data with 8 bits of validity mask. Allows tracking partial validity at the bit level. Mask bit = 1 means corresponding data bit is valid; 0 means VOID.

### Entropic Arithmetic
Integer arithmetic with explicit overflow handling. Three modes:
- **VOID**: Overflow returns VOID (safest)
- **WRAP**: Overflow wraps around (fastest)
- **SATURATE**: Overflow clamps to max/min (useful for graphics)

### Q128 / Q64.64
128-bit fixed-point number format with 64 bits for integer part (signed) and 64 bits for fractional part. Provides ~18 decimal digits of uniform precision.

### Galactic Number
256-bit hyper-dual number for automatic differentiation. Contains a primal value (Q128) and a tangent value (Q128). Operations automatically compute derivatives.

### Capability
An unforgeable token that grants permission to access a specific memory region. Contains: base address, length, generation counter, and permissions (read/write/execute).

### Chronon
A unit of causal time in SERAPH. Unlike wall-clock time, chronons represent logical ordering of events. Vector clocks extend this to distributed systems.

### Spectral Arena
SERAPH's memory allocator that automatically transforms Array-of-Structs (AoS) to Structure-of-Arrays (SoA) for cache efficiency. Uses bump-pointer allocation for O(1) alloc and generation-based bulk deallocation.

### Glyph
A Signed Distance Field (SDF) representation of a shape. Instead of storing pixels, stores the mathematical function that defines the shape's boundary. Enables resolution-independent rendering.

### Sovereign
SERAPH's process abstraction. A Sovereign owns a set of capabilities and represents an isolated execution context. Can only access memory for which it has valid capabilities.

### Strand
A lightweight cooperative thread within a Sovereign. Much cheaper than OS threads (thousands vs. dozens). Inherits capabilities from parent Sovereign.

### Whisper
SERAPH's inter-process communication mechanism. Uses zero-copy message passing through shared memory views. Sender grants time-limited capability to receiver.

### Surface
A rectangular region in SERAPH's UI system. Surfaces compose together and can contain Glyphs. Uses Galactic numbers for smooth animation interpolation.

### Atlas
SERAPH's single-level store that unifies RAM and persistent storage. The entire NVMe device is memory-mapped. No files, no serialization.

### Aether
SERAPH's distributed shared memory system. Extends the Atlas concept across network boundaries. Remote memory accessed through special address ranges.

### Seraphim
The SERAPH programming language and compiler. Enforces VOID propagation, capability validation, and effect tracking at compile time. Features both C transpilation and native code generation.

### Celestial IR
SERAPH's custom intermediate representation for native compilation. SSA-based, VOID-aware, capability-conscious. Sits between the AST and machine code generation.

### Seraphic
The command-line compiler driver. Orchestrates the complete compilation pipeline from source to executable.

### Seraph-V
Type-2 hypervisor for running SERAPH on Windows (WHP) or Linux (KVM). Provides virtual hardware including framebuffer, LAPIC, and memory. Used for development without bare-metal boot.

### Effect System
Compile-time tracking of what a function CAN do. Effects include PURE (no effects), VOID (may return VOID), PERSIST (uses Atlas), NETWORK (uses Aether), TIMER (uses Chronon), and IO (reads/writes memory).

### Proof-Carrying Code
SERAPH binaries embed compile-time safety proofs. The kernel verifies these proofs at load time. Invalid proofs = no execution.

### Zero-FPU Architecture
SERAPH's integer-only math system. Provides trigonometry, rotation, and harmonic synthesis using only integer operations. Critical for kernel code where FPU state is expensive or unavailable. Consists of 6 pillars: Q16 polynomial trig, Q64 micro-table, rotation FSM, harmonic synthesis, tier architecture, and compiler enforcement.

### Q16.16
32-bit fixed-point format with 16 bits integer and 16 bits fraction. Provides ~4 decimal digits of precision. Used for graphics, audio, and game logic. See also: Q64.64, Q128.

### Q64.64
128-bit fixed-point format with 64 bits integer and 64 bits fraction. Provides ~18 decimal digits of precision. Used in Zero-FPU architecture for high-precision physics calculations.

### Rotation State Machine
An optimization for continuous rotation that maintains sin/cos values and updates them in O(1) time using the angle addition formula as complex multiplication. Avoids recomputing trig functions each frame.

### Chebyshev Approximation
Polynomial approximation technique used in Zero-FPU Q16 trig. Minimizes maximum error over an interval. Allows sin/cos computation with ±1 ULP accuracy using only integer arithmetic.

### Chebyshev Recurrence
Formula for computing sin(nθ) and cos(nθ) given sin(θ) and cos(θ): `sin(nθ) = 2·cos(θ)·sin((n-1)θ) - sin((n-2)θ)`. Enables O(1) per-harmonic computation in Fourier synthesis.

### BMI2
Intel/AMD instruction set extension providing MULX (multiply without flags), ADCX, and ADOX (parallel carry chains). Used in Zero-FPU for high-performance 128-bit fixed-point multiplication.

## Abbreviations

| Abbrev | Full Form | Description |
|--------|-----------|-------------|
| MC | Micro-Concept | A fundamental building block in SERAPH's design |
| SoA | Structure of Arrays | Memory layout where each field is contiguous |
| AoS | Array of Structs | Traditional memory layout with structs together |
| SDF | Signed Distance Field | Shape representation via distance function |
| IPC | Inter-Process Communication | How processes exchange data |
| CDT | Capability Descriptor Table | Table mapping capability IDs to full capabilities |
| LSB | Least Significant Bit | Bit position 0 |
| MSB | Most Significant Bit | Highest bit position |
| CIR | Celestial IR | SERAPH's native intermediate representation |
| SSA | Static Single Assignment | IR form where each variable is assigned exactly once |
| ELF | Executable and Linkable Format | Standard binary format on Linux/SERAPH |
| NIH | Not Invented Here | SERAPH philosophy of zero external dependencies |
| GEP | Get Element Pointer | IR instruction to compute struct field addresses |
| REX | Register Extension | x86-64 prefix for 64-bit and extended registers |
| WHP | Windows Hypervisor Platform | Windows API for hardware virtualization |
| KVM | Kernel Virtual Machine | Linux kernel module for hardware virtualization |
| LAPIC | Local APIC | Per-CPU interrupt controller |
| FPU | Floating-Point Unit | Hardware for IEEE 754 floating-point operations |
| BMI2 | Bit Manipulation Instructions 2 | Intel/AMD extension with MULX, ADCX, ADOX |
| FSM | Finite State Machine | Used in rotation state machine context |
| ULP | Unit in Last Place | Measure of floating/fixed-point precision |
| FFT | Fast Fourier Transform | Algorithm for frequency analysis |
| DFT | Discrete Fourier Transform | Mathematical transform, computed by FFT |
| MMIO | Memory-Mapped I/O | Device registers accessed via memory addresses |
| PML4 | Page Map Level 4 | Top-level page table in x86-64 4-level paging |
| PDPT | Page Directory Pointer Table | Second-level page table |
| PD | Page Directory | Third-level page table |
| PT | Page Table | Fourth-level page table (or use 2MB huge pages) |
| GOP | Graphics Output Protocol | UEFI framebuffer interface |
| BGRA8 | Blue-Green-Red-Alpha 8-bit | Pixel format with 8 bits per channel |

## Mathematical Notation

| Symbol | Meaning |
|--------|---------|
| ∅ | VOID (empty set / undefined) |
| ⊤ | TRUE |
| ⊥ | FALSE |
| ∧ | AND (conjunction) |
| ∨ | OR (disjunction) |
| ¬ | NOT (negation) |
| → | IMPLIES (material implication) |
| ↔ | IFF (if and only if / equivalence) |

## Kleene Three-Valued Logic

The extension of Boolean logic to include VOID (unknown). Key properties:
- FALSE ∧ x = FALSE (FALSE dominates AND)
- TRUE ∨ x = TRUE (TRUE dominates OR)
- VOID propagates otherwise

Named after Stephen Cole Kleene (1909-1994), American logician.

## Type Hierarchy

```
u8 → u16 → u32 → u64 → Q128 → Galactic
 ↑     ↑     ↑     ↑
i8 → i16 → i32 → i64
```

Smaller types automatically promote to larger types. Q128 can hold any integer value. Galactic can hold Q128 plus its derivative.

## Memory Address Ranges

### Physical Addresses (Seraph-V)
| Range | Purpose |
|-------|---------|
| 0x00001000 | PML4 page table |
| 0x00080000 | Seraph_BootInfo structure |
| 0x00090000 | Initial kernel stack |
| 0x00101000 | Kernel code/data (ELF load) |
| 0x01000000 | Primordial arena (16 MB) |
| 0x02000000 | Kernel heap backing (64 MB) |
| 0xC0000000 | Framebuffer (16 MB) |
| 0xFEE00000 | LAPIC registers |

### Virtual Addresses (Kernel)
| Range | Purpose |
|-------|---------|
| 0x0000000000000000 | Identity-mapped first 4 GB |
| 0xFFFF800000000000 | Physical memory map (PML4[256]) |
| 0xFFFFC00000000000 | Kernel heap (PML4[384]) |
| 0xFFFFFF0000000000 | Recursive page table mapping (PML4[510]) |

### Reserved (Future)
| Range | Purpose |
|-------|---------|
| 0x8000... | Atlas (persistent storage) |
| 0xC000... | Aether (remote memory) |

## Color Codes (Documentation)

In SERAPH documentation:
- 🟢 Implemented and tested
- 🟡 Partially implemented
- 🔴 Not yet implemented
- ⚪ Planned for future phase
