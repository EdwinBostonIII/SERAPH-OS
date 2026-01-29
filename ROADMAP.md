# SERAPH OS Implementation Roadmap (Strict NIH, Code-Anchored)

This roadmap maps each phase to existing SERAPH-BUILD headers, sources, and tests. Updates below "preprocess" implementation by listing exact files, structs, and TODOs already visible in the code.

## Phase 0: Zero-FPU Architecture (COMPLETED)
### Status: ✅ Fully Implemented

The Zero-FPU architecture provides integer-only math operations for kernel-mode code where FPU state is unavailable or expensive. All trigonometric, rotation, and harmonic functions use pure integer arithmetic.

### Implemented Components

| Pillar | Description | Files |
|--------|-------------|-------|
| 1 | Q16.16 Zero-Table Polynomial Trig | `q16_trig.h`, `q16_trig.c` |
| 2 | Q64.64 Micro-Table Design (256-entry) | `q64_trig.h`, `q64_trig.c` |
| 3 | Rotation State Machine (O(1) updates) | `rotation.h`, `rotation.c` |
| 4 | Harmonic Synthesis (Chebyshev recurrence) | `harmonics.h`, `harmonics.c` |
| 5 | Tier Architecture (Q16/Q32/Q64 selection) | `math_tier.h` |
| 6 | Compiler FPU Enforcement | `fpu_check.c`, `pattern_opt.c` |

### Key Features
- **BMI2 Intrinsics**: MULX, ADCX, ADOX for high-performance 128-bit multiply (`bmi2_intrin.h`)
- **Chebyshev Approximation**: sin/cos with ±1 ULP accuracy using only integer ops
- **Branchless Memoization**: Thread-local caches with constant-time lookup (`math_cache.h`)
- **Pattern Optimization**: Compiler detects sin/cos pairs → sincos, x²+y² → hypot

## Phase 1: The Primordial Boot (The Entry)
### Existing anchors
- tests/test_main.c: current userspace entry and phase ordering.
- src/sovereign.c: seraph_sovereign_subsystem_init() creates seraph_the_primordial.
- include/seraph/sovereign.h: Seraph_Sovereign layout + Seraph_Spawn_Config.entry_point/entry_arg.
- src/strand.c + include/seraph/strand.h: Seraph_Strand lifecycle and cooperative scheduler.
- src/surface.c + include/seraph/surface.h: Surface UI and framebuffer rendering (seraph_surface_init(), seraph_surface_render()).

### Bootloader deliverables (UEFI PE32+)
1. New folder boot/ with efi_main.c and minimal UEFI CRT.
2. EFI entry: EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE*).
3. Protocols:
   - EFI_GRAPHICS_OUTPUT_PROTOCOL -> FrameBufferBase/Size/Stride (wire to seraph_surface_render()).
   - EFI_SIMPLE_FILE_SYSTEM_PROTOCOL -> read kernel ELF from EFI partition.
4. ELF64 loader:
   - Parse program headers; copy PT_LOAD segments into EfiLoaderData.
   - Build kernel load address map for relocation (sovereign.c has TODO to use load_addr).
5. Memory map:
   - Capture UEFI memory map for PMM init (Phase 2).
   - ExitBootServices() after you have the final map key.

### Kernel entry contract (kmain)
- New header: include/seraph/boot.h (define Seraph_BootInfo).
  - Must include: framebuffer pointer/size/stride/width/height, memory map + descriptor size,
    kernel_phys_base, kernel_virt_base, rsdp/acpi ptrs (if found), stack_size, primordial_arena_size.
- Prototype: void kmain(const Seraph_BootInfo* boot).
- Call order (replace tests/test_main.c):
  1. seraph_void_tracking_init() (src/void.c).
  2. seraph_sovereign_subsystem_init() -> initializes seraph_the_primordial.
  3. Create arenas for THE PRIMORDIAL (seraph_arena_create in src/arena.c).
  4. Initialize main Strand:
     - seraph_strand_create(&main_strand, kernel_loop, boot, boot->stack_size);
     - seraph_strand_start(&main_strand);
     - seraph_strand_set_current(&main_strand);
     - seraph_the_primordial->strands[0] = &main_strand; main_strand_idx=0; running_strands=1.
  5. Initialize Surface and render:
     - seraph_surface_init(&surface, boot->fb_width, boot->fb_height);
     - seraph_surface_render(&surface, boot->framebuffer, boot->fb_width, boot->fb_height).

## Phase 2: The Substrate (Memory Management)
### Existing anchors
- include/seraph/atlas.h: SERAPH_PAGE_SIZE (4096), SERAPH_ATLAS_BASE, SERAPH_AETHER_BASE.
- src/arena.c: seraph_arena_create()/destroy(), mmap-backed arenas.
- src/sovereign.c: malloc in seraph_sovereign_conceive() and seraph_sovereign_load_code().
- src/strand.c: malloc stack in allocate_stack().
- src/capability.c: calloc in seraph_cdt_init().
- src/chronon.c: calloc in seraph_vclock_init().
- tests/test_arena.c, test_sovereign.c, test_strand.c validate semantics.

### PMM (Bitmap Allocator)
- New file: src/pmm.c + include/seraph/pmm.h.
- Inputs: UEFI memory map from Phase 1 (EfiConventionalMemory regions only).
- Each bit = one 4KB page (SERAPH_PAGE_SIZE).
- API:
  - pmm_init(const Seraph_BootInfo* boot)
  - pmm_alloc_page(), pmm_free_page()
  - pmm_alloc_pages(n) for contiguous runs.

### VMM (Recursive PML4)
- New file: src/vmm.c + include/seraph/vmm.h.
- Map using constants from atlas.h/aether.h:
  - VOLATILE: 0x0000_0000_0000_0000 - 0x0000_7FFF_FFFF_FFFF
  - ATLAS:    0x0000_8000_0000_0000 - 0x0000_BFFF_FFFF_FFFF
  - AETHER:   0x0000_C000_0000_0000 - 0x0000_FFFF_FFFF_FFFF
- Identity-map kernel text/data and boot structures.
- Map arenas/stack pages in VOLATILE.
- Atlas/Aether ranges mapped Present=0 to force #PF.

### Heap replacement targets (kernel build)
- src/arena.c: replace malloc/aligned_alloc with pmm_alloc_pages + vmm_map.
- src/sovereign.c:
  - malloc(sizeof(Seraph_Sovereign)) -> pmm_alloc_pages + zero.
  - malloc(sizeof(Seraph_Arena)) -> pmm_alloc_pages.
- src/strand.c: stack allocation -> vmm_map pages.
- src/capability.c: seraph_cdt_init() -> use seraph_cdt_init_arena() with a kernel arena.
- src/chronon.c: seraph_vclock_init() -> use seraph_vclock_init_arena() with a kernel arena.
- Preserve all return semantics (VOID vs FALSE) and generation counters.

### Page Fault routing
- #PF reads CR2:
  - Atlas range -> Phase 4.
  - Aether range -> Phase 5.
  - Volatile unmapped -> SERAPH_VOID_REASON_OUT_OF_BOUNDS + terminate current Sovereign.

## Phase 3: The Void Interceptor (Interrupts)
### Existing anchors
- include/seraph/void.h: SERAPH_VOID_U64, SERAPH_VOID_RECORD, Seraph_VoidReason.
- src/void.c: seraph_void_record(), seraph_tracked_div_u64().
- include/seraph/sovereign.h: SERAPH_SOVEREIGN_KILLED, SERAPH_SOVEREIGN_VOIDED.
- include/seraph/strand.h: seraph_strand_yield().

### IDT + stubs
- New: src/idt.asm (stubs for vectors 0-31, IRQs 32-47).
- New: src/interrupts.c + include/seraph/interrupts.h.

### Exception specifics
1. #DE (vector 0):
   - Record: SERAPH_VOID_RECORD(SERAPH_VOID_REASON_DIV_ZERO, 0, dividend, divisor, "hw div0").
   - Write SERAPH_VOID_U64 into destination register.
   - Advance RIP to next instruction, iretq.
2. #GP (vector 13):
   - Identify current Sovereign (seraph_sovereign_current()).
   - Set state to SERAPH_SOVEREIGN_KILLED (or VOIDED for cap violation).
   - Yield/schedule next strand (seraph_strand_yield()).
3. #PF (vector 14): dispatch to Atlas/Aether handlers.

## Phase 4: The Infinite Drive (Atlas Driver)
### Existing anchors
- include/seraph/atlas.h:
  - SERAPH_ATLAS_BASE, SERAPH_ATLAS_MAGIC, SERAPH_ATLAS_VERSION.
  - Seraph_Atlas_Genesis is 256 bytes, aligned.
- src/atlas.c: atlas_format(), atlas_recover(), seraph_atlas_* API.
- include/seraph/surface.h: Surface persistence uses Atlas.

### NVMe-backed Atlas
- Replace atlas_mmap_windows/posix with NVMe DMA into pages at SERAPH_ATLAS_BASE.
- Preserve on-disk layout:
  - Genesis at offset 0 with SERAPH_ATLAS_MAGIC.
  - gen_table_offset = sizeof(Seraph_Atlas_Genesis).
  - next_alloc_offset = SERAPH_ATLAS_HEADER_SIZE (16KB).
- seraph_atlas_sync() -> NVMe FLUSH.

### Atlas page fault path
- LBA = (CR2 - SERAPH_ATLAS_BASE) / SERAPH_PAGE_SIZE.
- pmm_alloc_page() -> DMA buffer -> NVMe READ -> map Present=1.
- Resume with iretq.

## Phase 5: The Telepath (Aether Driver)
### Existing anchors
- include/seraph/aether.h:
  - SERAPH_AETHER_BASE/END, SERAPH_AETHER_PAGE_SIZE.
  - seraph_aether_get_node(), seraph_aether_get_offset(), seraph_aether_page_align().
  - Seraph_Aether_Request/Response and Seraph_Aether_Request_Type.
- src/aether.c: simulated nodes, cache, VOID context.
- tests/test_aether.c: behavior baseline.

### Raw NIC backend (strict NIH)
- Implement e1000.c or virtio_net.c for raw Ethernet frames.
- EtherType 0x88B5, payload = Seraph_Aether_Request/Response.

### Aether page fault path
- node_id = seraph_aether_get_node(CR2)
- offset = seraph_aether_page_align(seraph_aether_get_offset(CR2))
- Build SERAPH_AETHER_REQ_PAGE, send, wait for response.
- On success: map page Present=1 and insert into cache (seraph_aether_cache_insert()).
- On failure: set VOID context (SERAPH_AETHER_VOID_*), return VOID semantics.

### Coherence hooks
- For writes: send SERAPH_AETHER_REQ_WRITE, then broadcast invalidations using seraph_aether_broadcast_invalidation().
- For revocation: seraph_aether_revoke() increments generation and invalidates remote caps.

## Phase 6: The Architect (Seraphim Compiler)
### Existing anchors (already implemented)
- Tokens/Lexer/AST/Parser are present:
  - include/seraph/seraphim/token.h + src/seraphim/token.c
  - include/seraph/seraphim/lexer.h + src/seraphim/lexer.c
  - include/seraph/seraphim/ast.h + src/seraphim/ast.c
  - include/seraph/seraphim/parser.h + src/seraphim/parser.c
- Tests: tests/test_seraphim_lexer.c, tests/test_seraphim_parser.c.
- NOTE: current parser does NOT yet parse effect annotations or substrate blocks.
  - parse_fn_decl() ignores [pure] and effects(...).
  - seraph_parse_stmt() does not handle SERAPH_TOK_PERSIST/AETHER/RECOVER.

### Preprocess: Parser deltas (fill missing syntax)
- File: src/seraphim/parser.c
- Add to parse_fn_decl():
  - Optional leading [pure] and [effects(...)] before fn token, or effects(...) after signature.
  - Populate Seraph_AST_FnDecl.is_pure and .effects (AST_EFFECT_LIST).
- Add effect list parsing:
  - AST_EFFECT_LIST uses Seraph_AST_EffectList.effects bitmask.
  - Convert tokens: SERAPH_TOK_EFFECT_VOID/PERSIST/NETWORK/TIMER/IO -> bits.
- Add substrate statement parsing in seraph_parse_stmt():
  - SERAPH_TOK_PERSIST -> AST_STMT_PERSIST (use Seraph_AST_SubstrateBlock).
  - SERAPH_TOK_AETHER_BLOCK -> AST_STMT_AETHER.
  - SERAPH_TOK_RECOVER -> AST_STMT_RECOVER (parse recover { } else { }).
- Update tests/test_seraphim_parser.c:
  - Expand test_parse_fn_with_effects to assert is_pure/effects bitmask.
  - Add tests for persist/aether/recover blocks.

### Effect Solver (new)
- New files: include/seraph/seraphim/effects.h + src/seraphim/effects.c.
- Inputs: AST from parser (Seraph_AST_Module). Uses Seraph_AST_FnDecl.effects and block nodes.
- Provide Seraph_Effect_Mask enum bits:
  - SERAPH_EFFECT_VOID, PERSIST, NETWORK, TIMER, IO.
- Walk AST:
  - Effects from annotations + AST_STMT_PERSIST/AST_STMT_AETHER/AST_STMT_RECOVER.
  - For call graph: caller |= callee (lookup by name in module decl list).
- Enforce PURE:
  - if fn_decl.is_pure and effects != 0 -> parser-style diagnostic.

### Checker / Proof / Codegen (new)
- docs/20-seraphim-compiler.md defines pipeline and proof table layout.
- New placeholders (minimal stubs initially):
  - include/seraph/seraphim/checker.h + src/seraphim/checker.c
  - include/seraph/seraphim/proofs.h + src/seraphim/proofs.c
  - include/seraph/seraphim/codegen.h + src/seraphim/codegen.c
- Codegen targets:
  - VOID literal -> SERAPH_VOID_U64.
  - ?? -> coalesce; !! -> assert + SERAPH_VOID_RECORD.
  - Emit calls to seraph_* APIs (arena/capability/atlas/aether) using seraph.h.
- Integration: add new sources to src/seraphim/ so CMake picks them up.

## Phase 7: The Pulse (Scheduler)
### Existing anchors
- src/strand.c: seraph_strand_create/start/yield/run_quantum/schedule.
- include/seraph/strand.h: Seraph_Strand fields (stack_pointer, chronon_limit, next_ready).
- include/seraph/sovereign.h: Sovereign strand arrays and counters.

### Preemptive scheduler tasks
- Extend Seraph_Strand with saved context:
  - rsp, rip, rflags, gp registers (or a packed context struct).
  - cr3 for per-sovereign address space.
- APIC timer ISR:
  1. Save current regs to strand context.
  2. seraph_strand_tick() and compare chronon_limit.
  3. Enqueue current strand and pick next (use next_ready).
  4. Restore regs and iretq.

### Sovereign integration
- Implement seraph_sovereign_vivify() TODO in src/sovereign.c:
  - Create main Strand using Seraph_Spawn_Config.entry_point/entry_arg.
  - Insert into child->strands[0], set main_strand_idx, running_strands.
- On kill/exit: terminate strands, free arenas via PMM/VMM.

### IPC + lends on tick
- Call seraph_strand_process_lends() for each runnable strand.
- If Whisper is enabled, call seraph_whisper_channel_transfer() regularly to move messages.
