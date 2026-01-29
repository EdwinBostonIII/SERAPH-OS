cmake_minimum_required(VERSION 3.16)
project(SERAPH VERSION 0.1.0 LANGUAGES C)

# C11 standard required (for _Alignof, _Thread_local, etc.)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # Need __int128 for Q128, binary literals for tests

#============================================================================
# Build Options
#============================================================================

option(SERAPH_KERNEL_BUILD "Build as kernel (includes boot, drivers, ASM)" OFF)
option(SERAPH_ENABLE_TESTS "Build test executables" ON)
option(SERAPH_USE_ASM_STUBS "Use C stubs instead of assembly (for testing without NASM)" OFF)
option(SERAPH_CACHE_STATS "Enable Zero-FPU math cache statistics (for profiling)" OFF)

# Apply optional compile definitions
if(SERAPH_CACHE_STATS)
    add_compile_definitions(SERAPH_CACHE_STATS)
endif()

#============================================================================
# Compiler Flags
#============================================================================

# Strict compilation flags
if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # Note: -Wpedantic removed because codebase uses GCC extensions
    # (binary literals, __int128, __thread, etc.)
    # Use generator expression to apply only to C, not NASM
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wall> $<$<COMPILE_LANGUAGE:C>:-Wextra> $<$<COMPILE_LANGUAGE:C>:-Werror>)
    # SIMD temporarily disabled for debugging - causes crashes on some systems
    # add_compile_options(-mavx2 -msse4.2)
    # BMI2 for Zero-FPU architecture (MULX, ADCX, ADOX instructions)
    # Runtime detection is used, so this just enables the intrinsics
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-mbmi2>)
endif()

# NASM-specific flags (don't use -Werror to allow external function calls)
set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -w-reloc-rel-dword -w-reloc-abs-qword")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

#============================================================================
# Core Library Sources
#============================================================================

# Core source files (always included)
file(GLOB SERAPH_CORE_SOURCES "src/*.c")

# Seraphim compiler sources
file(GLOB SERAPHIM_SOURCES "src/seraphim/*.c")

#============================================================================
# Zero-FPU Architecture Sources (MC26 Performance Revolution)
#
# These files implement the 6-pillar integer-only math system:
#   Pillar 1: Q16.16 Zero-Table Polynomial Trig (src/q16_trig.c)
#   Pillar 2: Q64.64 Micro-Table Design (src/q64_trig.c)
#   Pillar 3: Rotation State Machine (src/rotation.c)
#   Pillar 4: Harmonic Synthesis (src/harmonics.c)
#   Pillar 5: Tier Architecture (headers only - include/seraph/math_tier.h)
#   Pillar 6: Compiler FPU Enforcement (src/seraphim/fpu_check.c,
#             src/seraphim/pattern_opt.c)
#   Support:  Branchless Math Cache (src/math_cache.c)
#
# Headers:
#   include/seraph/bmi2_intrin.h  - BMI2 intrinsics (MULX, ADCX, ADOX)
#   include/seraph/q16_trig.h     - Q16.16 trig API
#   include/seraph/q64_trig.h     - Q64.64 trig API
#   include/seraph/rotation.h     - Rotation state machine API
#   include/seraph/harmonics.h    - Harmonic synthesis API
#   include/seraph/math_tier.h    - Tier selection macros
#   include/seraph/math_cache.h   - Branchless cache API
#
# All sources are picked up by GLOB patterns above.
#============================================================================

# Driver sources
file(GLOB_RECURSE DRIVER_SOURCES "src/drivers/*.c")

# Combine into main library sources
set(SERAPH_SOURCES
    ${SERAPH_CORE_SOURCES}
    ${SERAPHIM_SOURCES}
    ${DRIVER_SOURCES}
)

# Remove kernel-only files from the shared library
# kruntime.c provides freestanding C runtime for kernel - conflicts with stdlib
# kmain.c has kernel entry point - not needed for userspace
# early_mem.c has early memory init - kernel only
list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/kruntime\\.c$")
list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/kmain\\.c$")
list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/early_mem\\.c$")

#============================================================================
# Kernel Build Configuration
#============================================================================

if(SERAPH_KERNEL_BUILD)
    message(STATUS "Building SERAPH as kernel")
    # Note: SERAPH_KERNEL is NOT defined globally to avoid conflicting with
    # userspace tools like seraphic. Instead, it's defined per-target below.

    # Disable strict warnings for kernel build (UEFI types have MSVC issues)
    if(MSVC)
        # Remove /WX (warnings as errors) for kernel build
        string(REPLACE "/WX" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        add_compile_options(/wd4146)  # unary minus on unsigned
    endif()

    # Boot sources - compile with kernel when using GCC/MinGW (not MSVC)
    # Note: For actual EFI binary, use separate EFI build target
    if(NOT MSVC)
        file(GLOB BOOT_SOURCES "boot/*.c")
        list(APPEND SERAPH_SOURCES ${BOOT_SOURCES})
        message(STATUS "  Boot sources: ${BOOT_SOURCES}")
    else()
        message(STATUS "  Boot sources: (skipped on MSVC)")
    endif()

    if(SERAPH_USE_ASM_STUBS)
        # Use C stubs instead of assembly (for testing without NASM)
        message(STATUS "  Using ASM stubs (no NASM required)")
        add_compile_definitions(SERAPH_ASM_STUBS)
        # Stubs are already in SERAPH_CORE_SOURCES (idt_stub.c, context_stub.c)
        # Remove the real idt.c and interrupts.c to avoid duplicate symbols
        list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/idt\\.c$")
        list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/interrupts\\.c$")
    else()
        # Enable NASM for assembly files
        enable_language(ASM_NASM)

        # Assembly sources
        file(GLOB ASM_SOURCES "src/*.asm")
        list(APPEND SERAPH_SOURCES ${ASM_SOURCES})
        message(STATUS "  ASM sources: ${ASM_SOURCES}")

        # Remove stub files when using real assembly
        list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/idt_stub\\.c$")
        list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/context_stub\\.c$")
    endif()
endif()

# For non-kernel builds, remove real idt/interrupts and keep stubs
if(NOT SERAPH_KERNEL_BUILD)
    list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/idt\\.c$")
    list(FILTER SERAPH_SOURCES EXCLUDE REGEX "src/interrupts\\.c$")
endif()

#============================================================================
# Main Library
#============================================================================

add_library(seraph STATIC ${SERAPH_SOURCES})

# For non-kernel builds, exclude kernel-specific symbols
if(NOT SERAPH_KERNEL_BUILD)
    target_compile_definitions(seraph PRIVATE SERAPH_USERSPACE)
endif()

#============================================================================
# Seraphim Compiler Executable (seraphic)
#============================================================================

# seraphic is the native Seraphim compiler driver
# It compiles .srph files directly to native executables (no LLVM, no GCC)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/seraphim/seraphic.c")
    # Exclude seraphic.c from the library (it has main())
    list(FILTER SERAPH_SOURCES EXCLUDE REGEX "seraphic\\.c$")

    # Get seraphim library sources (everything except seraphic.c)
    file(GLOB SERAPHIM_LIB_SOURCES "src/seraphim/*.c")
    list(FILTER SERAPHIM_LIB_SOURCES EXCLUDE REGEX "seraphic\\.c$")

    # Create seraphic executable
    add_executable(seraphic src/seraphim/seraphic.c)
    target_link_libraries(seraphic seraph)

    # Set output properties
    set_target_properties(seraphic PROPERTIES
        OUTPUT_NAME "seraphic"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    # Installation
    install(TARGETS seraphic DESTINATION bin)

    message(STATUS "Seraphim Compiler (seraphic) target configured")
endif()

#============================================================================
# Kernel and Bootloader Executables (SERAPH_KERNEL_BUILD only)
#============================================================================

if(SERAPH_KERNEL_BUILD AND NOT MSVC)
    message(STATUS "Configuring kernel and bootloader targets...")

    #------------------------------------------------------------------------
    # Kernel ELF Executable
    #------------------------------------------------------------------------

    # Collect kernel-specific sources
    # NOTE: Seraphim compiler sources are NOT included in the kernel
    # The kernel is a freestanding environment without stdlib
    file(GLOB KERNEL_C_SOURCES "src/*.c")
    file(GLOB KERNEL_DRIVER_SOURCES "src/drivers/*.c" "src/drivers/**/*.c")

    set(KERNEL_SOURCES
        ${KERNEL_C_SOURCES}
        ${KERNEL_DRIVER_SOURCES}
    )

    # Remove stub files when using real assembly
    if(NOT SERAPH_USE_ASM_STUBS)
        list(FILTER KERNEL_SOURCES EXCLUDE REGEX "src/idt_stub\\.c$")
        list(FILTER KERNEL_SOURCES EXCLUDE REGEX "src/context_stub\\.c$")

        # Add assembly sources
        file(GLOB KERNEL_ASM_SOURCES "src/*.asm")
        list(APPEND KERNEL_SOURCES ${KERNEL_ASM_SOURCES})
    else()
        # Use stubs, remove real idt/interrupts/pic (stubs provide their own)
        list(FILTER KERNEL_SOURCES EXCLUDE REGEX "src/idt\\.c$")
        list(FILTER KERNEL_SOURCES EXCLUDE REGEX "src/interrupts\\.c$")
        list(FILTER KERNEL_SOURCES EXCLUDE REGEX "src/pic\\.c$")
    endif()

    # Create kernel executable
    add_executable(kernel_elf ${KERNEL_SOURCES})

    # Kernel compile definitions
    target_compile_definitions(kernel_elf PRIVATE SERAPH_KERNEL)

    # Freestanding kernel compile options
    target_compile_options(kernel_elf PRIVATE
        -ffreestanding
        -fno-builtin
        -fno-stack-protector
        -fno-pic
        -fno-pie
        -mno-red-zone
    )

    # Platform-specific kernel linking
    if(WIN32 AND MINGW)
        # MinGW produces PE, but we need ELF for the bootloader.
        # Strategy: Build PE, then convert to ELF64 using objcopy.
        # The PE is built at load address 0x100000 (KERNEL_LMA).
        # The bootloader maps virtual 0xFFFF800000000000 → physical 0x100000.
        target_link_options(kernel_elf PRIVATE
            -nostdlib
            -nostartfiles
            -static
            -Wl,--subsystem,native
            -Wl,--entry,_start
            -Wl,--image-base,0x100000
        )
        set_target_properties(kernel_elf PROPERTIES
            OUTPUT_NAME "kernel"
            SUFFIX ".pe"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )

        # Find objcopy for PE→ELF conversion
        find_program(OBJCOPY_EXECUTABLE
            NAMES objcopy x86_64-w64-mingw32-objcopy
            HINTS ENV PATH
        )

        if(OBJCOPY_EXECUTABLE)
            # Convert PE to ELF64 after building
            add_custom_command(TARGET kernel_elf POST_BUILD
                COMMAND ${OBJCOPY_EXECUTABLE}
                    -O elf64-x86-64
                    --set-section-flags .bss=alloc,load,contents
                    "${CMAKE_BINARY_DIR}/kernel.pe"
                    "${CMAKE_BINARY_DIR}/kernel.elf"
                COMMENT "Converting kernel.pe to kernel.elf (ELF64 format)"
                VERBATIM
            )
            message(STATUS "  kernel.elf target configured (PE→ELF via objcopy)")
        else()
            message(WARNING "objcopy not found - kernel.pe will not be converted to ELF")
            message(STATUS "  kernel.pe target configured (PE format only)")
        endif()
    else()
        # Unix/cross-compiler: Use ELF linker script
        set(KERNEL_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/kernel.ld")
        target_compile_options(kernel_elf PRIVATE -mcmodel=large)
        target_link_options(kernel_elf PRIVATE
            -nostdlib
            -nostartfiles
            -static
            -T ${KERNEL_LINKER_SCRIPT}
            -Wl,--build-id=none
        )
        set_target_properties(kernel_elf PROPERTIES
            OUTPUT_NAME "kernel"
            SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
        message(STATUS "  kernel.elf target configured")
    endif()

    #------------------------------------------------------------------------
    # UEFI Bootloader (BOOTX64.EFI)
    #------------------------------------------------------------------------

    # Boot sources
    file(GLOB BOOT_SOURCES "boot/*.c")

    # Create bootloader object library first (EFI requires special handling)
    add_executable(bootloader_efi ${BOOT_SOURCES})

    # EFI compile definitions
    target_compile_definitions(bootloader_efi PRIVATE
        SERAPH_KERNEL
        SERAPH_EFI_BOOT
    )

    # EFI compile options - freestanding, no-stdlib
    target_compile_options(bootloader_efi PRIVATE
        -ffreestanding
        -fno-builtin
        -fno-stack-protector
        -fshort-wchar
        -mno-red-zone
    )

    # EFI link options with linker script
    # Note: For proper PE/COFF EFI, we use objcopy after linking
    set(EFI_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/boot/efi.ld")

    # First link as ELF64, then convert to PE
    target_link_options(bootloader_efi PRIVATE
        -nostdlib
        -nostartfiles
        -static
        -Wl,-e,efi_main
        -Wl,--subsystem,10
        -Wl,--image-base,0
        -Wl,--section-alignment,0x1000
        -Wl,--file-alignment,0x1000
        -Wl,--major-os-version,0
        -Wl,--minor-os-version,0
        -Wl,--major-image-version,0
        -Wl,--minor-image-version,0
        -Wl,--major-subsystem-version,0
        -Wl,--minor-subsystem-version,0
    )

    # Set output name
    set_target_properties(bootloader_efi PROPERTIES
        OUTPUT_NAME "BOOTX64"
        SUFFIX ".EFI"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    message(STATUS "  BOOTX64.EFI target configured")

    #------------------------------------------------------------------------
    # Combined kernel target
    #------------------------------------------------------------------------

    add_custom_target(kernel
        DEPENDS kernel_elf bootloader_efi
        COMMENT "Building SERAPH kernel and bootloader"
    )

    message(STATUS "  'kernel' meta-target configured")

endif()

#============================================================================
# Test Configuration
#============================================================================

if(SERAPH_ENABLE_TESTS)
    # Collect all test sources EXCEPT integration tests and standalone tests
    file(GLOB TEST_SOURCES "tests/*.c")

    # Remove integration tests and standalone tests (they have their own main())
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_integration_.*\\.c$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_seraphim_codegen\\.c$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_seraphim_proofs\\.c$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_proof_blob\\.c$")

    # Main test executable (runs all unit tests)
    add_executable(seraph_tests ${TEST_SOURCES})
    target_link_libraries(seraph_tests seraph)
    # Relax warnings for test files - DCE tests intentionally have unused variables
    target_compile_options(seraph_tests PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable)

    # Increase stack size for tests - Whisper structures are very large (77KB each)
    # Default stack (1MB) is not enough when multiple large structures are on stack
    if(WIN32 OR MINGW)
        target_link_options(seraph_tests PRIVATE -Wl,--stack,8388608)  # 8MB stack
    elseif(UNIX)
        # On Unix, stack size is controlled at runtime, but we can try setting it
        # Actually, on Linux the default is usually 8MB which should be enough
    endif()

    # Enable testing
    enable_testing()
    add_test(NAME seraph_tests COMMAND seraph_tests)

    # Seraphim codegen tests as separate executable
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_seraphim_codegen.c")
        add_executable(test_seraphim_codegen tests/test_seraphim_codegen.c)
        target_link_libraries(test_seraphim_codegen seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_seraphim_codegen PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME seraphim_codegen COMMAND test_seraphim_codegen)
    endif()

    # Seraphim proofs tests as separate executable
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_seraphim_proofs.c")
        add_executable(test_seraphim_proofs tests/test_seraphim_proofs.c)
        target_link_libraries(test_seraphim_proofs seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_seraphim_proofs PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME seraphim_proofs COMMAND test_seraphim_proofs)
    endif()

    # Galactic scheduler tests as separate executable
    # Disabled: test uses functions not yet implemented (is_cpu_bound, is_io_bound)
    # if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_galactic_scheduler.c")
    #     add_executable(test_galactic_scheduler tests/test_galactic_scheduler.c)
    #     target_link_libraries(test_galactic_scheduler seraph)
    #     add_test(NAME galactic_scheduler COMMAND test_galactic_scheduler)
    # endif()

    # Proof blob tests as separate executable
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_proof_blob.c")
        add_executable(test_proof_blob tests/test_proof_blob.c)
        target_link_libraries(test_proof_blob seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_proof_blob PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME proof_blob COMMAND test_proof_blob)
    endif()

    # Individual integration test executables (optional)
    # Note: All integration tests get 8MB stack for large Whisper/Atlas structures
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_integration_memory.c")
        add_executable(test_integration_memory tests/test_integration_memory.c)
        target_link_libraries(test_integration_memory seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_integration_memory PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME integration_memory COMMAND test_integration_memory)
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_integration_interrupts.c")
        add_executable(test_integration_interrupts tests/test_integration_interrupts.c)
        target_link_libraries(test_integration_interrupts seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_integration_interrupts PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME integration_interrupts COMMAND test_integration_interrupts)
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_integration_compiler.c")
        add_executable(test_integration_compiler tests/test_integration_compiler.c)
        target_link_libraries(test_integration_compiler seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_integration_compiler PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME integration_compiler COMMAND test_integration_compiler)
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_integration_system.c")
        add_executable(test_integration_system tests/test_integration_system.c)
        target_link_libraries(test_integration_system seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_integration_system PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME integration_system COMMAND test_integration_system)
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_integration_drivers.c")
        add_executable(test_integration_drivers tests/test_integration_drivers.c)
        target_link_libraries(test_integration_drivers seraph)
        if(WIN32 OR MINGW)
            target_link_options(test_integration_drivers PRIVATE -Wl,--stack,8388608)
        endif()
        add_test(NAME integration_drivers COMMAND test_integration_drivers)
    endif()
endif()

#============================================================================
# Print Build Summary
#============================================================================

message(STATUS "")
message(STATUS "SERAPH Build Configuration:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Kernel build: ${SERAPH_KERNEL_BUILD}")
message(STATUS "  ASM stubs:    ${SERAPH_USE_ASM_STUBS}")
message(STATUS "  Tests:        ${SERAPH_ENABLE_TESTS}")
message(STATUS "  Cache stats:  ${SERAPH_CACHE_STATS}")
message(STATUS "  C Standard:   ${CMAKE_C_STANDARD}")
message(STATUS "")

#============================================================================
# Installation
#============================================================================

install(TARGETS seraph DESTINATION lib)
install(DIRECTORY include/seraph DESTINATION include)

# Optionally install boot files for kernel deployment
if(SERAPH_KERNEL_BUILD)
    install(DIRECTORY boot DESTINATION share/seraph)
endif()
