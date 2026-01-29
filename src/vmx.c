/**
 * @file vmx.c
 * @brief VMX (Virtual Machine Extensions) Implementation for SERAPH
 *
 * This file implements Intel VT-x virtualization support, providing the
 * low-level mechanisms for running guest VMs. The Foreign Substrate layer
 * (foreign_substrate.c) builds on this to run Linux for driver support.
 *
 * VMX Operation Flow:
 *   1. Check CPU support (CPUID)
 *   2. Enable VMX (set CR4.VMXE, execute VMXON)
 *   3. Allocate and configure VMCS
 *   4. Set up EPT for guest physical memory
 *   5. Configure VM-execution controls
 *   6. Launch guest (VMLAUNCH)
 *   7. Handle VM-exits, resume guest (VMRESUME)
 *
 * Memory Requirements:
 *   - VMXON region: 4KB aligned, size from IA32_VMX_BASIC
 *   - VMCS: 4KB aligned, size from IA32_VMX_BASIC
 *   - I/O bitmap: 2x 4KB pages
 *   - MSR bitmap: 4KB
 *   - EPT tables: 4KB each
 *
 * Reference: Intel SDM Volume 3C, Chapters 23-33
 */

#include "seraph/vmx.h"
#include "seraph/kmalloc.h"
#include "seraph/pmm.h"
#include "seraph/vmm.h"

#include <string.h>

#ifdef SERAPH_KERNEL

/*============================================================================
 * Inline Assembly Helpers
 *
 * These provide raw access to VMX instructions and x86 registers.
 * All VMX instructions can fail - we check the carry/zero flags.
 *============================================================================*/

/**
 * @brief Read a Model-Specific Register (MSR)
 */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(msr)
    );
    return ((uint64_t)hi << 32) | lo;
}

/**
 * @brief Write a Model-Specific Register (MSR)
 */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ __volatile__(
        "wrmsr"
        :
        : "c"(msr), "a"(lo), "d"(hi)
    );
}

/**
 * @brief Read CR0 register
 */
static inline uint64_t read_cr0(void) {
    uint64_t value;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(value));
    return value;
}

/**
 * @brief Write CR0 register
 */
static inline void write_cr0(uint64_t value) {
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(value));
}

/**
 * @brief Read CR3 register
 */
static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(value));
    return value;
}

/**
 * @brief Read CR4 register
 */
static inline uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(value));
    return value;
}

/**
 * @brief Write CR4 register
 */
static inline void write_cr4(uint64_t value) {
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(value));
}

/**
 * @brief Read RFLAGS register
 */
static inline uint64_t read_rflags(void) {
    uint64_t flags;
    __asm__ __volatile__(
        "pushfq\n\t"
        "popq %0"
        : "=r"(flags)
    );
    return flags;
}

/**
 * @brief Execute CPUID instruction
 *
 * @param leaf CPUID leaf (EAX input)
 * @param eax Output EAX
 * @param ebx Output EBX
 * @param ecx Output ECX
 * @param edx Output EDX
 */
static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx,
                         uint32_t* ecx, uint32_t* edx) {
    __asm__ __volatile__(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

/**
 * @brief Execute CPUID with subleaf
 */
static inline void cpuid_count(uint32_t leaf, uint32_t subleaf,
                                uint32_t* eax, uint32_t* ebx,
                                uint32_t* ecx, uint32_t* edx) {
    __asm__ __volatile__(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

/**
 * @brief Read segment selector for a segment register
 */
static inline uint16_t read_es(void) {
    uint16_t sel;
    __asm__ __volatile__("mov %%es, %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_cs(void) {
    uint16_t sel;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_ss(void) {
    uint16_t sel;
    __asm__ __volatile__("mov %%ss, %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_ds(void) {
    uint16_t sel;
    __asm__ __volatile__("mov %%ds, %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_fs(void) {
    uint16_t sel;
    __asm__ __volatile__("mov %%fs, %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_gs(void) {
    uint16_t sel;
    __asm__ __volatile__("mov %%gs, %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_tr(void) {
    uint16_t sel;
    __asm__ __volatile__("str %0" : "=r"(sel));
    return sel;
}

static inline uint16_t read_ldtr(void) {
    uint16_t sel;
    __asm__ __volatile__("sldt %0" : "=r"(sel));
    return sel;
}

/**
 * @brief Store GDT register
 */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) DescriptorTableReg;

static inline void sgdt(DescriptorTableReg* gdtr) {
    __asm__ __volatile__("sgdt %0" : "=m"(*gdtr));
}

static inline void sidt(DescriptorTableReg* idtr) {
    __asm__ __volatile__("sidt %0" : "=m"(*idtr));
}

/**
 * @brief Get segment base from GDT
 *
 * Parses a GDT entry to extract the segment base address.
 */
static uint64_t get_segment_base(uint64_t gdt_base, uint16_t selector) {
    if (selector == 0) {
        return 0;
    }

    /* Extract GDT index from selector (bits 15:3) */
    uint16_t index = selector >> 3;

    /* Point to the GDT entry */
    uint64_t* entry = (uint64_t*)(gdt_base + index * 8);
    uint64_t desc = *entry;

    /*
     * Segment descriptor format (normal segment):
     *   Bits 15:0 of limit: bits 15:0 of descriptor
     *   Bits 23:0 of base: bits 39:16 of descriptor
     *   Bits 31:24 of base: bits 63:56 of descriptor
     */
    uint64_t base = ((desc >> 16) & 0xFFFFFF) |     /* Bits 23:0 */
                    ((desc >> 32) & 0xFF000000);    /* Bits 31:24 */

    /*
     * For system segments (TSS, LDT), the descriptor is 16 bytes.
     * Check type field (bits 43:40) - types 0x9 and 0xB are 64-bit TSS.
     */
    uint8_t type = (desc >> 40) & 0xF;
    if (type == 0x9 || type == 0xB) {
        /* 64-bit TSS: upper 32 bits of base are in next 8 bytes */
        uint64_t upper = *(entry + 1);
        base |= (upper & 0xFFFFFFFF) << 32;
    }

    return base;
}

/**
 * @brief Get segment access rights for VMCS
 *
 * VMCS access rights format differs from GDT descriptor format.
 */
__attribute__((unused))
static uint32_t get_segment_access_rights(uint64_t gdt_base, uint16_t selector) {
    if (selector == 0) {
        /* Unusable segment */
        return 0x10000;
    }

    uint16_t index = selector >> 3;
    uint64_t* entry = (uint64_t*)(gdt_base + index * 8);
    uint64_t desc = *entry;

    /*
     * Access rights in VMCS:
     *   Bits 3:0: Segment type
     *   Bit 4: S (descriptor type: 0=system, 1=code/data)
     *   Bits 6:5: DPL
     *   Bit 7: P (present)
     *   Bits 11:8: Reserved (0)
     *   Bit 12: AVL
     *   Bit 13: L (64-bit code segment)
     *   Bit 14: D/B
     *   Bit 15: G (granularity)
     *   Bit 16: Unusable
     */
    uint32_t ar = ((desc >> 40) & 0xFF) |        /* Type, S, DPL, P */
                  ((desc >> 44) & 0xF) << 12;    /* AVL, L, D/B, G */

    return ar;
}

/**
 * @brief Get segment limit from GDT
 */
__attribute__((unused))
static uint32_t get_segment_limit(uint64_t gdt_base, uint16_t selector) {
    if (selector == 0) {
        return 0;
    }

    uint16_t index = selector >> 3;
    uint64_t* entry = (uint64_t*)(gdt_base + index * 8);
    uint64_t desc = *entry;

    /* Limit is in bits 15:0 and 51:48 */
    uint32_t limit = (desc & 0xFFFF) | ((desc >> 32) & 0xF0000);

    /* If granularity bit is set, limit is in 4KB units */
    if (desc & (1ULL << 55)) {
        limit = (limit << 12) | 0xFFF;
    }

    return limit;
}

/*============================================================================
 * VMX Instruction Wrappers
 *
 * These wrap the VMX instructions with error checking.
 * VMX instructions set CF=1 on failure, ZF=1 on VMfail.
 *============================================================================*/

/**
 * @brief Execute VMXON instruction
 *
 * Enters VMX root operation.
 *
 * @param vmxon_phys Physical address of VMXON region
 * @return true on success
 */
static bool vmxon(uint64_t vmxon_phys) {
    uint8_t cf, zf;

    __asm__ __volatile__(
        "vmxon %[vmxon]\n\t"
        "setc %[cf]\n\t"
        "setz %[zf]"
        : [cf] "=rm"(cf), [zf] "=rm"(zf)
        : [vmxon] "m"(vmxon_phys)
        : "cc", "memory"
    );

    /* Success if neither CF nor ZF is set */
    return (cf == 0) && (zf == 0);
}

/**
 * @brief Execute VMXOFF instruction
 *
 * Exits VMX operation.
 */
static void vmxoff(void) {
    __asm__ __volatile__("vmxoff" ::: "cc", "memory");
}

/**
 * @brief Execute VMCLEAR instruction
 *
 * Clears the launch state of a VMCS.
 *
 * @param vmcs_phys Physical address of VMCS
 * @return true on success
 */
static bool vmclear(uint64_t vmcs_phys) {
    uint8_t cf, zf;

    __asm__ __volatile__(
        "vmclear %[vmcs]\n\t"
        "setc %[cf]\n\t"
        "setz %[zf]"
        : [cf] "=rm"(cf), [zf] "=rm"(zf)
        : [vmcs] "m"(vmcs_phys)
        : "cc", "memory"
    );

    return (cf == 0) && (zf == 0);
}

/**
 * @brief Execute VMPTRLD instruction
 *
 * Loads a VMCS as the current VMCS.
 *
 * @param vmcs_phys Physical address of VMCS
 * @return true on success
 */
static bool vmptrld(uint64_t vmcs_phys) {
    uint8_t cf, zf;

    __asm__ __volatile__(
        "vmptrld %[vmcs]\n\t"
        "setc %[cf]\n\t"
        "setz %[zf]"
        : [cf] "=rm"(cf), [zf] "=rm"(zf)
        : [vmcs] "m"(vmcs_phys)
        : "cc", "memory"
    );

    return (cf == 0) && (zf == 0);
}

/**
 * @brief Execute VMREAD instruction
 *
 * Reads a field from the current VMCS.
 *
 * @param field VMCS field encoding
 * @param value Output value
 * @return true on success
 */
static bool vmread_internal(uint64_t field, uint64_t* value) {
    uint8_t cf, zf;

    __asm__ __volatile__(
        "vmread %[field], %[value]\n\t"
        "setc %[cf]\n\t"
        "setz %[zf]"
        : [value] "=r"(*value), [cf] "=rm"(cf), [zf] "=rm"(zf)
        : [field] "r"(field)
        : "cc", "memory"
    );

    return (cf == 0) && (zf == 0);
}

/**
 * @brief Execute VMWRITE instruction
 *
 * Writes a field in the current VMCS.
 *
 * @param field VMCS field encoding
 * @param value Value to write
 * @return true on success
 */
static bool vmwrite_internal(uint64_t field, uint64_t value) {
    uint8_t cf, zf;

    __asm__ __volatile__(
        "vmwrite %[value], %[field]\n\t"
        "setc %[cf]\n\t"
        "setz %[zf]"
        : [cf] "=rm"(cf), [zf] "=rm"(zf)
        : [field] "r"(field), [value] "r"(value)
        : "cc", "memory"
    );

    return (cf == 0) && (zf == 0);
}

/**
 * @brief Execute INVEPT instruction
 *
 * Invalidates EPT-derived translations.
 *
 * @param type Invalidation type (1=single-context, 2=all-context)
 * @param eptp EPTP for single-context invalidation
 */
static void invept(uint32_t type, uint64_t eptp) {
    struct {
        uint64_t eptp;
        uint64_t reserved;
    } descriptor = { eptp, 0 };

    __asm__ __volatile__(
        "invept %[desc], %[type]"
        :
        : [desc] "m"(descriptor), [type] "r"((uint64_t)type)
        : "cc", "memory"
    );
}

/*============================================================================
 * VM-Exit Handler Table
 *============================================================================*/

/** Array of exit handlers indexed by exit reason */
static Seraph_VMX_ExitHandler g_exit_handlers[SERAPH_EXIT_REASON_MAX] = { NULL };

/*============================================================================
 * Public API Implementation
 *============================================================================*/

bool seraph_vmx_supported(void) {
    uint32_t eax, ebx, ecx, edx;

    /* Check CPUID.1:ECX.VMX[bit 5] */
    cpuid(1, &eax, &ebx, &ecx, &edx);

    return (ecx & SERAPH_CPUID_VMX_BIT) != 0;
}

bool seraph_vmx_read_capabilities(Seraph_VMX_Basic* basic) {
    if (!basic) {
        return false;
    }

    /*
     * Read IA32_VMX_BASIC MSR (0x480)
     * Format:
     *   Bits 30:0  - VMCS revision identifier
     *   Bits 44:32 - VMCS region size (number of bytes)
     *   Bit 48     - Physical address width (0=full, 1=limited to 32 bits)
     *   Bit 49     - Dual-monitor SMM support
     *   Bits 53:50 - Memory type for VMCS access
     *   Bit 54     - INS/OUTS info on VM-exit
     *   Bit 55     - True VMX controls MSRs available
     */
    uint64_t vmx_basic = rdmsr(SERAPH_MSR_VMX_BASIC);

    basic->vmcs_revision = vmx_basic & 0x7FFFFFFF;
    basic->vmcs_region_size = ((vmx_basic >> 32) & 0x1FFF);
    basic->physaddr_32bit = (vmx_basic >> 48) & 1;
    basic->dual_monitor = (vmx_basic >> 49) & 1;
    basic->memory_type = (vmx_basic >> 50) & 0xF;
    basic->ins_outs_info = (vmx_basic >> 54) & 1;
    basic->true_ctls = (vmx_basic >> 55) & 1;

    /* VMCS size is at minimum 4KB */
    if (basic->vmcs_region_size == 0) {
        basic->vmcs_region_size = 4096;
    }

    return true;
}

/**
 * @brief Adjust control value based on MSR-reported allowed settings
 *
 * VMX control MSRs (e.g., IA32_VMX_PINBASED_CTLS) encode:
 *   - Low 32 bits: Bits that are allowed to be 0
 *   - High 32 bits: Bits that are allowed to be 1
 *
 * This function adjusts the desired value to meet hardware requirements.
 */
static uint32_t adjust_controls(uint32_t desired, uint32_t msr) {
    uint64_t msr_val = rdmsr(msr);
    uint32_t allowed0 = (uint32_t)msr_val;          /* Must be 1 if bit is 0 here */
    uint32_t allowed1 = (uint32_t)(msr_val >> 32);  /* May be 1 if bit is 1 here */

    /* Bits set in allowed0 must be set in result */
    desired |= allowed0;

    /* Bits clear in allowed1 must be clear in result */
    desired &= allowed1;

    return desired;
}

bool seraph_vmx_enable(Seraph_VMX_VCPU* vcpu) {
    if (!vcpu) {
        return false;
    }

    /* Check VMX support */
    if (!seraph_vmx_supported()) {
        return false;
    }

    /*
     * Check IA32_FEATURE_CONTROL MSR
     * VMX requires:
     *   - Bit 0 (lock) = 1
     *   - Bit 2 (enable VMX outside SMX) = 1
     */
    uint64_t feature_control = rdmsr(SERAPH_MSR_IA32_FEATURE_CONTROL);

    if (!(feature_control & SERAPH_FEATURE_CONTROL_LOCK)) {
        /*
         * Feature control not locked - we can set it.
         * Enable VMX and lock it.
         */
        feature_control |= SERAPH_FEATURE_CONTROL_LOCK | SERAPH_FEATURE_CONTROL_VMXON;
        wrmsr(SERAPH_MSR_IA32_FEATURE_CONTROL, feature_control);
    } else if (!(feature_control & SERAPH_FEATURE_CONTROL_VMXON)) {
        /* Locked but VMX disabled - cannot enable */
        return false;
    }

    /* Read VMX capabilities */
    Seraph_VMX_Basic basic;
    if (!seraph_vmx_read_capabilities(&basic)) {
        return false;
    }
    vcpu->vmcs_revision = basic.vmcs_revision;

    /*
     * Adjust CR0 and CR4 for VMX operation.
     *
     * VMX requires certain CR0 and CR4 bits to be set or clear.
     * These are reported by IA32_VMX_CR0_FIXED0/1 and IA32_VMX_CR4_FIXED0/1 MSRs.
     */
    uint64_t cr0_fixed0 = rdmsr(SERAPH_MSR_VMX_CR0_FIXED0);
    uint64_t cr0_fixed1 = rdmsr(SERAPH_MSR_VMX_CR0_FIXED1);
    uint64_t cr4_fixed0 = rdmsr(SERAPH_MSR_VMX_CR4_FIXED0);
    uint64_t cr4_fixed1 = rdmsr(SERAPH_MSR_VMX_CR4_FIXED1);

    uint64_t cr0 = read_cr0();
    cr0 |= cr0_fixed0;  /* Bits that must be 1 */
    cr0 &= cr0_fixed1;  /* Bits that must be 0 */
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= cr4_fixed0;
    cr4 &= cr4_fixed1;
    cr4 |= SERAPH_CR4_VMXE;  /* Enable VMX */
    write_cr4(cr4);

    /*
     * Allocate VMXON region (4KB aligned)
     * The VMXON region must contain the VMCS revision ID in its first 4 bytes.
     */
    vcpu->vmxon_region = seraph_kmalloc_pages(1);
    if (!vcpu->vmxon_region) {
        return false;
    }

    /* Get physical address of VMXON region */
    vcpu->vmxon_phys = seraph_virt_to_phys_direct(vcpu->vmxon_region);

    /* Clear and write revision ID */
    memset(vcpu->vmxon_region, 0, 4096);
    *(uint32_t*)vcpu->vmxon_region = vcpu->vmcs_revision;

    /* Execute VMXON */
    if (!vmxon(vcpu->vmxon_phys)) {
        seraph_kfree_pages(vcpu->vmxon_region, 1);
        vcpu->vmxon_region = NULL;
        return false;
    }

    vcpu->vmx_enabled = true;
    return true;
}

void seraph_vmx_disable(Seraph_VMX_VCPU* vcpu) {
    if (!vcpu || !vcpu->vmx_enabled) {
        return;
    }

    /* Execute VMXOFF */
    vmxoff();

    /* Clear CR4.VMXE */
    uint64_t cr4 = read_cr4();
    cr4 &= ~SERAPH_CR4_VMXE;
    write_cr4(cr4);

    /* Free VMXON region */
    if (vcpu->vmxon_region) {
        seraph_kfree_pages(vcpu->vmxon_region, 1);
        vcpu->vmxon_region = NULL;
    }

    vcpu->vmx_enabled = false;
}

bool seraph_vmx_alloc_vmcs(Seraph_VMX_VCPU* vcpu) {
    if (!vcpu || !vcpu->vmx_enabled) {
        return false;
    }

    /* Allocate VMCS (4KB aligned) */
    vcpu->vmcs_region = seraph_kmalloc_pages(1);
    if (!vcpu->vmcs_region) {
        return false;
    }

    vcpu->vmcs_phys = seraph_virt_to_phys_direct(vcpu->vmcs_region);

    /* Clear and write revision ID */
    memset(vcpu->vmcs_region, 0, 4096);
    *(uint32_t*)vcpu->vmcs_region = vcpu->vmcs_revision;

    vcpu->vmcs_loaded = false;
    return true;
}

void seraph_vmx_free_vmcs(Seraph_VMX_VCPU* vcpu) {
    if (!vcpu) {
        return;
    }

    /* Clear VMCS if loaded */
    if (vcpu->vmcs_loaded) {
        seraph_vmx_clear_vmcs(vcpu);
    }

    if (vcpu->vmcs_region) {
        seraph_kfree_pages(vcpu->vmcs_region, 1);
        vcpu->vmcs_region = NULL;
        vcpu->vmcs_phys = 0;
    }
}

bool seraph_vmx_load_vmcs(Seraph_VMX_VCPU* vcpu) {
    if (!vcpu || !vcpu->vmcs_region || !vcpu->vmx_enabled) {
        return false;
    }

    if (!vmptrld(vcpu->vmcs_phys)) {
        return false;
    }

    vcpu->vmcs_loaded = true;
    return true;
}

bool seraph_vmx_clear_vmcs(Seraph_VMX_VCPU* vcpu) {
    if (!vcpu || !vcpu->vmcs_region) {
        return false;
    }

    if (!vmclear(vcpu->vmcs_phys)) {
        return false;
    }

    vcpu->vmcs_loaded = false;
    return true;
}

bool seraph_vmx_vmread(uint32_t field, uint64_t* value) {
    if (!value) {
        return false;
    }
    return vmread_internal((uint64_t)field, value);
}

bool seraph_vmx_vmwrite(uint32_t field, uint64_t value) {
    return vmwrite_internal((uint64_t)field, value);
}

bool seraph_vmx_setup_guest_state(Seraph_VMX_Context* ctx,
                                   uint64_t entry_point,
                                   uint64_t stack_ptr,
                                   uint64_t page_table) {
    if (!ctx || !ctx->vcpu.vmcs_loaded) {
        return false;
    }

    /*
     * Guest State Area Setup for 64-bit Long Mode
     *
     * We configure the guest to start in 64-bit mode with paging enabled.
     * The guest is configured similarly to how Linux expects to start.
     */

    /* Guest CR0: Enable paging (PG), protection (PE), numeric exception (NE) */
    uint64_t guest_cr0 = SERAPH_CR0_PE | SERAPH_CR0_NE | (1ULL << 31); /* PG bit */
    /* Adjust to hardware requirements */
    uint64_t cr0_fixed0 = rdmsr(SERAPH_MSR_VMX_CR0_FIXED0);
    uint64_t cr0_fixed1 = rdmsr(SERAPH_MSR_VMX_CR0_FIXED1);
    guest_cr0 |= cr0_fixed0;
    guest_cr0 &= cr0_fixed1;

    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CR0, guest_cr0);
    seraph_vmx_vmwrite(SERAPH_VMCS_CR0_READ_SHADOW, guest_cr0);

    /* Guest CR3: Page table root */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CR3, page_table);

    /* Guest CR4: Enable PAE, and other required bits */
    uint64_t guest_cr4 = (1ULL << 5);  /* PAE bit */
    uint64_t cr4_fixed0 = rdmsr(SERAPH_MSR_VMX_CR4_FIXED0);
    uint64_t cr4_fixed1 = rdmsr(SERAPH_MSR_VMX_CR4_FIXED1);
    guest_cr4 |= cr4_fixed0;
    guest_cr4 &= cr4_fixed1;

    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CR4, guest_cr4);
    seraph_vmx_vmwrite(SERAPH_VMCS_CR4_READ_SHADOW, guest_cr4);

    /* Guest DR7: Default value */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_DR7, 0x400);

    /* Guest RSP and RIP */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_RSP, stack_ptr);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_RIP, entry_point);

    /* Guest RFLAGS: Interrupts disabled, reserved bit 1 set */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_RFLAGS, 0x2);

    /*
     * Guest Segment Registers
     *
     * Set up flat segments for 64-bit mode:
     * - CS: 64-bit code segment, DPL 0
     * - DS, ES, FS, GS, SS: Data segments, DPL 0
     * - TR: 64-bit TSS
     * - LDTR: Unusable
     */

    /* CS: 64-bit code segment */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CS_SEL, 0x08);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CS_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    /* Access rights: Present, DPL 0, code segment, read/execute, 64-bit (L=1) */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_CS_ACCESS, 0xA09B);

    /* SS: Data segment */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SS_SEL, 0x10);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SS_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SS_ACCESS, 0xC093);

    /* DS, ES, FS, GS: Data segments (same as SS) */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_DS_SEL, 0x10);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_DS_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_DS_ACCESS, 0xC093);

    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_ES_SEL, 0x10);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_ES_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_ES_ACCESS, 0xC093);

    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_FS_SEL, 0x10);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_FS_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_FS_LIMIT, 0xFFFFFFFF);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_FS_ACCESS, 0xC093);

    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_GS_SEL, 0x10);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_GS_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_GS_LIMIT, 0xFFFFFFFF);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_GS_ACCESS, 0xC093);

    /* TR: 64-bit TSS */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_TR_SEL, 0x18);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_TR_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_TR_LIMIT, 0x67);
    /* Access rights: Present, 64-bit TSS busy (type 0xB) */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_TR_ACCESS, 0x8B);

    /* LDTR: Unusable */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_LDTR_SEL, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_LDTR_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_LDTR_LIMIT, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_LDTR_ACCESS, 0x10000); /* Unusable */

    /* Guest GDTR and IDTR (guest will set these up) */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_GDTR_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_GDTR_LIMIT, 0xFFFF);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_IDTR_BASE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_IDTR_LIMIT, 0xFFFF);

    /* Guest MSRs */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SYSENTER_CS, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SYSENTER_ESP, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_SYSENTER_EIP, 0);

    /* Guest IA32_EFER: LME and LMA set for 64-bit mode */
    uint64_t guest_efer = (1ULL << 8) | (1ULL << 10);  /* LME | LMA */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_EFER, guest_efer);

    /* VMCS link pointer: -1 (invalid) */
    seraph_vmx_vmwrite(SERAPH_VMCS_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);

    /* Guest interruptibility and activity states */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_INTR_STATE, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_ACTIVITY_STATE, 0);  /* Active */
    seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_PENDING_DBG, 0);

    return true;
}

bool seraph_vmx_setup_host_state(Seraph_VMX_Context* ctx) {
    if (!ctx || !ctx->vcpu.vmcs_loaded) {
        return false;
    }

    /*
     * Host State Area
     *
     * The host state is loaded on VM-exit. We set it to the current
     * SERAPH kernel state so we return to the hypervisor properly.
     */

    /* Host control registers */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_CR0, read_cr0());
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_CR3, read_cr3());
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_CR4, read_cr4());

    /* Host segment selectors */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_ES_SEL, read_es() & 0xF8);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_CS_SEL, read_cs() & 0xF8);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_SS_SEL, read_ss() & 0xF8);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_DS_SEL, read_ds() & 0xF8);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_FS_SEL, read_fs() & 0xF8);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_GS_SEL, read_gs() & 0xF8);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_TR_SEL, read_tr() & 0xF8);

    /* Get GDT base for segment base lookups */
    DescriptorTableReg gdtr, idtr;
    sgdt(&gdtr);
    sidt(&idtr);

    /* Host segment bases */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_FS_BASE, rdmsr(0xC0000100)); /* IA32_FS_BASE */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_GS_BASE, rdmsr(0xC0000101)); /* IA32_GS_BASE */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_TR_BASE, get_segment_base(gdtr.base, read_tr()));
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_GDTR_BASE, gdtr.base);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_IDTR_BASE, idtr.base);

    /* Host MSRs */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_SYSENTER_CS, rdmsr(0x174));
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_SYSENTER_ESP, rdmsr(0x175));
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_SYSENTER_EIP, rdmsr(0x176));

    /* Host EFER */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_EFER, rdmsr(0xC0000080)); /* IA32_EFER */

    /*
     * Host RSP and RIP
     *
     * These are set just before VMLAUNCH/VMRESUME to the actual
     * stack pointer and exit handler address. We set temporary
     * values here; they'll be updated by the launch code.
     */
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_RSP, ctx->host_rsp);
    seraph_vmx_vmwrite(SERAPH_VMCS_HOST_RIP, ctx->host_rip);

    return true;
}

bool seraph_vmx_setup_controls(Seraph_VMX_Context* ctx) {
    if (!ctx || !ctx->vcpu.vmcs_loaded) {
        return false;
    }

    /*
     * VM-Execution Control Fields
     *
     * These control what causes VM-exits and how the guest runs.
     */

    /* Read capabilities to know if true controls are available */
    Seraph_VMX_Basic basic;
    seraph_vmx_read_capabilities(&basic);

    uint32_t pinbased_msr = basic.true_ctls ?
                            SERAPH_MSR_VMX_TRUE_PINBASED :
                            SERAPH_MSR_VMX_PINBASED_CTLS;
    uint32_t procbased_msr = basic.true_ctls ?
                             SERAPH_MSR_VMX_TRUE_PROCBASED :
                             SERAPH_MSR_VMX_PROCBASED_CTLS;
    uint32_t exit_msr = basic.true_ctls ?
                        SERAPH_MSR_VMX_TRUE_EXIT :
                        SERAPH_MSR_VMX_EXIT_CTLS;
    uint32_t entry_msr = basic.true_ctls ?
                         SERAPH_MSR_VMX_TRUE_ENTRY :
                         SERAPH_MSR_VMX_ENTRY_CTLS;

    /*
     * Pin-based controls:
     *   - External interrupt exiting: VM-exit on external interrupts
     *   - NMI exiting: VM-exit on NMI
     */
    uint32_t pinbased = SERAPH_PIN_EXTERNAL_INTR_EXIT | SERAPH_PIN_NMI_EXIT;
    pinbased = adjust_controls(pinbased, pinbased_msr);
    seraph_vmx_vmwrite(SERAPH_VMCS_PIN_BASED_CTLS, pinbased);

    /*
     * Primary processor-based controls:
     *   - HLT exiting: VM-exit on HLT (so we can handle idle)
     *   - I/O exiting: VM-exit on I/O instructions
     *   - Unconditional I/O exiting
     *   - Secondary controls: Enable secondary control field
     */
    uint32_t procbased = SERAPH_PROC_HLT_EXIT |
                         SERAPH_PROC_UNCOND_IO_EXIT |
                         SERAPH_PROC_SECONDARY_CTLS;
    procbased = adjust_controls(procbased, procbased_msr);
    seraph_vmx_vmwrite(SERAPH_VMCS_PROC_BASED_CTLS, procbased);

    /*
     * Secondary processor-based controls:
     *   - Enable EPT: Use Extended Page Tables
     *   - Unrestricted guest: Allow real-mode if needed
     */
    uint32_t procbased2 = SERAPH_PROC2_ENABLE_EPT;

    /* Check if unrestricted guest is supported */
    uint64_t proc2_msr = rdmsr(SERAPH_MSR_VMX_PROCBASED_CTLS2);
    if ((proc2_msr >> 32) & SERAPH_PROC2_UNRESTRICTED) {
        procbased2 |= SERAPH_PROC2_UNRESTRICTED;
    }

    procbased2 = adjust_controls(procbased2, SERAPH_MSR_VMX_PROCBASED_CTLS2);
    seraph_vmx_vmwrite(SERAPH_VMCS_PROC_BASED_CTLS2, procbased2);

    /*
     * VM-exit controls:
     *   - Host address-space size: 64-bit host
     *   - Save/load EFER: Preserve EFER across VM-exit
     */
    uint32_t exit_ctls = SERAPH_EXIT_HOST_LONG_MODE |
                         SERAPH_EXIT_SAVE_EFER |
                         SERAPH_EXIT_LOAD_EFER;
    exit_ctls = adjust_controls(exit_ctls, exit_msr);
    seraph_vmx_vmwrite(SERAPH_VMCS_EXIT_CTLS, exit_ctls);

    /*
     * VM-entry controls:
     *   - IA-32e mode guest: 64-bit guest
     *   - Load EFER: Set guest EFER on entry
     */
    uint32_t entry_ctls = SERAPH_ENTRY_GUEST_LONG_MODE |
                          SERAPH_ENTRY_LOAD_EFER;
    entry_ctls = adjust_controls(entry_ctls, entry_msr);
    seraph_vmx_vmwrite(SERAPH_VMCS_ENTRY_CTLS, entry_ctls);

    /* Exception bitmap: 0 = let guest handle all exceptions */
    seraph_vmx_vmwrite(SERAPH_VMCS_EXCEPTION_BITMAP, 0);

    /* CR0/CR4 guest/host masks and shadows */
    seraph_vmx_vmwrite(SERAPH_VMCS_CR0_GUEST_HOST_MASK, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_CR4_GUEST_HOST_MASK, 0);

    /* Set up EPT pointer if EPT is initialized */
    if (ctx->ept.ept_pml4) {
        seraph_vmx_vmwrite(SERAPH_VMCS_EPTP, ctx->ept.eptp);
    }

    /* MSR counts (no MSR save/load for now) */
    seraph_vmx_vmwrite(SERAPH_VMCS_EXIT_MSR_STORE_COUNT, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_EXIT_MSR_LOAD_COUNT, 0);
    seraph_vmx_vmwrite(SERAPH_VMCS_ENTRY_MSR_LOAD_COUNT, 0);

    return true;
}

/*
 * VM launch/resume assembly stub
 *
 * This is complex because we need to:
 * 1. Save host state (callee-saved registers)
 * 2. Load guest GPRs from context
 * 3. Execute VMLAUNCH or VMRESUME
 * 4. On VM-exit, save guest GPRs to context
 * 5. Restore host state
 * 6. Return exit reason
 */

/**
 * @brief Internal VM entry implementation
 *
 * This function is called with interrupts disabled.
 *
 * @param ctx VMX context with guest registers
 * @param launch true for VMLAUNCH, false for VMRESUME
 * @return VM-exit reason, or -1 on failure
 */
static uint32_t vmx_enter_guest(Seraph_VMX_Context* ctx, bool launch) {
    uint32_t exit_reason = 0;

    /*
     * Assembly stub for VM entry
     *
     * We save all callee-saved registers, set up guest RSP/RIP in VMCS,
     * load guest GPRs, and execute VMLAUNCH/VMRESUME.
     *
     * On VM-exit, we save guest GPRs and restore host state.
     */
    __asm__ __volatile__(
        /* Save callee-saved registers */
        "pushq %%rbp\n\t"
        "pushq %%rbx\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"

        /* Save host RSP for VM-exit */
        "movq %%rsp, %[host_rsp]\n\t"

        /* Load guest GPRs from context */
        "movq %[ctx], %%rdi\n\t"           /* RDI = ctx (for later) */
        "movq 0x00(%%rdi), %%rax\n\t"      /* guest_regs.rax */
        "movq 0x08(%%rdi), %%rbx\n\t"      /* guest_regs.rbx */
        "movq 0x10(%%rdi), %%rcx\n\t"      /* guest_regs.rcx */
        "movq 0x18(%%rdi), %%rdx\n\t"      /* guest_regs.rdx */
        "movq 0x20(%%rdi), %%rsi\n\t"      /* guest_regs.rsi */
        /* RDI loaded last as we're using it */
        "movq 0x30(%%rdi), %%rbp\n\t"      /* guest_regs.rbp */
        "movq 0x38(%%rdi), %%r8\n\t"       /* guest_regs.r8 */
        "movq 0x40(%%rdi), %%r9\n\t"       /* guest_regs.r9 */
        "movq 0x48(%%rdi), %%r10\n\t"      /* guest_regs.r10 */
        "movq 0x50(%%rdi), %%r11\n\t"      /* guest_regs.r11 */
        "movq 0x58(%%rdi), %%r12\n\t"      /* guest_regs.r12 */
        "movq 0x60(%%rdi), %%r13\n\t"      /* guest_regs.r13 */
        "movq 0x68(%%rdi), %%r14\n\t"      /* guest_regs.r14 */
        "movq 0x70(%%rdi), %%r15\n\t"      /* guest_regs.r15 */
        "movq 0x28(%%rdi), %%rdi\n\t"      /* guest_regs.rdi (last!) */

        /* Execute VMLAUNCH or VMRESUME based on launch flag */
        "testb %[launch], %[launch]\n\t"
        "jz 1f\n\t"
        "vmlaunch\n\t"
        "jmp 2f\n\t"
        "1: vmresume\n\t"
        "2:\n\t"

        /*
         * VM-exit lands here (or VMLAUNCH/VMRESUME failed)
         *
         * Save guest GPRs to context. We need to reload the context pointer.
         */
        "pushq %%rdi\n\t"                  /* Save guest RDI temporarily */
        "movq %[ctx], %%rdi\n\t"           /* Reload context pointer */
        "movq %%rax, 0x00(%%rdi)\n\t"      /* Save guest_regs.rax */
        "movq %%rbx, 0x08(%%rdi)\n\t"
        "movq %%rcx, 0x10(%%rdi)\n\t"
        "movq %%rdx, 0x18(%%rdi)\n\t"
        "movq %%rsi, 0x20(%%rdi)\n\t"
        "popq %%rax\n\t"                   /* Get saved guest RDI */
        "movq %%rax, 0x28(%%rdi)\n\t"      /* Save guest_regs.rdi */
        "movq %%rbp, 0x30(%%rdi)\n\t"
        "movq %%r8,  0x38(%%rdi)\n\t"
        "movq %%r9,  0x40(%%rdi)\n\t"
        "movq %%r10, 0x48(%%rdi)\n\t"
        "movq %%r11, 0x50(%%rdi)\n\t"
        "movq %%r12, 0x58(%%rdi)\n\t"
        "movq %%r13, 0x60(%%rdi)\n\t"
        "movq %%r14, 0x68(%%rdi)\n\t"
        "movq %%r15, 0x70(%%rdi)\n\t"

        /* Restore callee-saved registers */
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%rbx\n\t"
        "popq %%rbp\n\t"

        : [host_rsp] "=m"(ctx->host_rsp)
        : [ctx] "r"(&ctx->guest_regs), [launch] "r"((uint8_t)launch)
        : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "cc", "memory"
    );

    /* Read exit reason from VMCS */
    uint64_t reason64;
    if (seraph_vmx_vmread(SERAPH_VMCS_EXIT_REASON, &reason64)) {
        exit_reason = (uint32_t)reason64;
    } else {
        exit_reason = (uint32_t)-1;  /* Failed to read */
    }

    /* Read exit qualification */
    uint64_t qual64;
    if (seraph_vmx_vmread(SERAPH_VMCS_EXIT_QUALIFICATION, &qual64)) {
        ctx->exit_qual = qual64;
    }

    ctx->exit_reason = exit_reason;
    return exit_reason;
}

uint32_t seraph_vmx_launch(Seraph_VMX_Context* ctx) {
    if (!ctx) {
        return (uint32_t)-1;
    }

    ctx->guest_running = true;
    uint32_t reason = vmx_enter_guest(ctx, true);
    ctx->guest_running = false;

    return reason;
}

uint32_t seraph_vmx_resume(Seraph_VMX_Context* ctx) {
    if (!ctx) {
        return (uint32_t)-1;
    }

    ctx->guest_running = true;
    uint32_t reason = vmx_enter_guest(ctx, false);
    ctx->guest_running = false;

    return reason;
}

/*============================================================================
 * EPT Implementation
 *============================================================================*/

/**
 * @brief Get or create EPT entry at a given level
 *
 * @param table Current table (PML4, PDPT, PD, or PT)
 * @param index Index within table
 * @param create If true, create missing tables
 * @return Pointer to next-level table, or NULL
 */
static uint64_t* ept_get_or_create_entry(uint64_t* table, uint64_t index, bool create) {
    uint64_t entry = table[index];

    if (entry & SERAPH_EPT_READ) {
        /* Entry exists, return pointer to next level */
        uint64_t phys = entry & SERAPH_EPT_ADDR_MASK;
        return (uint64_t*)seraph_phys_to_virt(phys);
    }

    if (!create) {
        return NULL;
    }

    /* Allocate new page table */
    void* new_table = seraph_kmalloc_pages(1);
    if (!new_table) {
        return NULL;
    }

    memset(new_table, 0, 4096);

    uint64_t new_phys = seraph_virt_to_phys_direct(new_table);

    /* Create entry with RWX permissions for non-leaf */
    table[index] = (new_phys & SERAPH_EPT_ADDR_MASK) | SERAPH_EPT_RWX;

    return (uint64_t*)new_table;
}

bool seraph_vmx_ept_init(Seraph_VMX_EPT* ept, uint64_t guest_memory_size, bool identity_map) {
    if (!ept) {
        return false;
    }

    /* Allocate EPT PML4 */
    ept->ept_pml4 = seraph_kmalloc_pages(1);
    if (!ept->ept_pml4) {
        return false;
    }

    memset(ept->ept_pml4, 0, 4096);
    ept->ept_pml4_phys = seraph_virt_to_phys_direct(ept->ept_pml4);
    ept->guest_phys_limit = guest_memory_size;
    ept->mapped_pages = 0;

    /* Build EPTP */
    ept->eptp = seraph_vmx_make_eptp(ept->ept_pml4_phys);

    /* Create identity mapping if requested */
    if (identity_map && guest_memory_size > 0) {
        /*
         * Map guest physical == host physical for the specified range.
         * This is useful for simple guests or early boot.
         */
        if (!seraph_vmx_ept_map(ept, 0, 0, guest_memory_size,
                                 SERAPH_EPT_RWX | SERAPH_EPT_MT_WB)) {
            seraph_vmx_ept_destroy(ept);
            return false;
        }
    }

    return true;
}

bool seraph_vmx_ept_map(Seraph_VMX_EPT* ept, uint64_t guest_phys,
                         uint64_t host_phys, uint64_t size, uint64_t flags) {
    if (!ept || !ept->ept_pml4) {
        return false;
    }

    /* Align addresses and size to page boundaries */
    guest_phys &= ~0xFFFULL;
    host_phys &= ~0xFFFULL;
    size = (size + 0xFFF) & ~0xFFFULL;

    while (size > 0) {
        /* Calculate indices for 4-level walk */
        uint64_t pml4_idx = (guest_phys >> 39) & 0x1FF;
        uint64_t pdpt_idx = (guest_phys >> 30) & 0x1FF;
        uint64_t pd_idx   = (guest_phys >> 21) & 0x1FF;
        uint64_t pt_idx   = (guest_phys >> 12) & 0x1FF;

        /* Walk/create EPT hierarchy */
        uint64_t* pml4 = (uint64_t*)ept->ept_pml4;
        uint64_t* pdpt = ept_get_or_create_entry(pml4, pml4_idx, true);
        if (!pdpt) return false;

        uint64_t* pd = ept_get_or_create_entry(pdpt, pdpt_idx, true);
        if (!pd) return false;

        uint64_t* pt = ept_get_or_create_entry(pd, pd_idx, true);
        if (!pt) return false;

        /* Create leaf entry */
        pt[pt_idx] = (host_phys & SERAPH_EPT_ADDR_MASK) | flags;

        guest_phys += 0x1000;
        host_phys += 0x1000;
        size -= 0x1000;
        ept->mapped_pages++;
    }

    return true;
}

void seraph_vmx_ept_unmap(Seraph_VMX_EPT* ept, uint64_t guest_phys, uint64_t size) {
    if (!ept || !ept->ept_pml4) {
        return;
    }

    guest_phys &= ~0xFFFULL;
    size = (size + 0xFFF) & ~0xFFFULL;

    while (size > 0) {
        uint64_t pml4_idx = (guest_phys >> 39) & 0x1FF;
        uint64_t pdpt_idx = (guest_phys >> 30) & 0x1FF;
        uint64_t pd_idx   = (guest_phys >> 21) & 0x1FF;
        uint64_t pt_idx   = (guest_phys >> 12) & 0x1FF;

        uint64_t* pml4 = (uint64_t*)ept->ept_pml4;
        uint64_t* pdpt = ept_get_or_create_entry(pml4, pml4_idx, false);
        if (!pdpt) goto next;

        uint64_t* pd = ept_get_or_create_entry(pdpt, pdpt_idx, false);
        if (!pd) goto next;

        uint64_t* pt = ept_get_or_create_entry(pd, pd_idx, false);
        if (!pt) goto next;

        if (pt[pt_idx] & SERAPH_EPT_READ) {
            pt[pt_idx] = 0;
            ept->mapped_pages--;
        }

    next:
        guest_phys += 0x1000;
        size -= 0x1000;
    }
}

uint64_t seraph_vmx_ept_translate(const Seraph_VMX_EPT* ept, uint64_t guest_phys) {
    if (!ept || !ept->ept_pml4) {
        return 0;
    }

    uint64_t pml4_idx = (guest_phys >> 39) & 0x1FF;
    uint64_t pdpt_idx = (guest_phys >> 30) & 0x1FF;
    uint64_t pd_idx   = (guest_phys >> 21) & 0x1FF;
    uint64_t pt_idx   = (guest_phys >> 12) & 0x1FF;
    uint64_t offset   = guest_phys & 0xFFF;

    uint64_t* pml4 = (uint64_t*)ept->ept_pml4;
    uint64_t entry = pml4[pml4_idx];
    if (!(entry & SERAPH_EPT_READ)) return 0;

    uint64_t* pdpt = (uint64_t*)seraph_phys_to_virt(entry & SERAPH_EPT_ADDR_MASK);
    entry = pdpt[pdpt_idx];
    if (!(entry & SERAPH_EPT_READ)) return 0;

    /* Check for 1GB page */
    if (entry & SERAPH_EPT_LARGE_PAGE) {
        return (entry & SERAPH_EPT_ADDR_MASK) | (guest_phys & 0x3FFFFFFF);
    }

    uint64_t* pd = (uint64_t*)seraph_phys_to_virt(entry & SERAPH_EPT_ADDR_MASK);
    entry = pd[pd_idx];
    if (!(entry & SERAPH_EPT_READ)) return 0;

    /* Check for 2MB page */
    if (entry & SERAPH_EPT_LARGE_PAGE) {
        return (entry & SERAPH_EPT_ADDR_MASK) | (guest_phys & 0x1FFFFF);
    }

    uint64_t* pt = (uint64_t*)seraph_phys_to_virt(entry & SERAPH_EPT_ADDR_MASK);
    entry = pt[pt_idx];
    if (!(entry & SERAPH_EPT_READ)) return 0;

    return (entry & SERAPH_EPT_ADDR_MASK) | offset;
}

/**
 * @brief Recursively free EPT tables
 */
static void ept_free_table(uint64_t* table, int level) {
    if (!table || level < 0) {
        return;
    }

    if (level > 0) {
        for (int i = 0; i < 512; i++) {
            uint64_t entry = table[i];
            if ((entry & SERAPH_EPT_READ) && !(entry & SERAPH_EPT_LARGE_PAGE)) {
                uint64_t* child = (uint64_t*)seraph_phys_to_virt(entry & SERAPH_EPT_ADDR_MASK);
                ept_free_table(child, level - 1);
            }
        }
    }

    seraph_kfree_pages(table, 1);
}

void seraph_vmx_ept_destroy(Seraph_VMX_EPT* ept) {
    if (!ept) {
        return;
    }

    if (ept->ept_pml4) {
        ept_free_table((uint64_t*)ept->ept_pml4, 3);
        ept->ept_pml4 = NULL;
    }

    ept->ept_pml4_phys = 0;
    ept->eptp = 0;
    ept->mapped_pages = 0;
}

void seraph_vmx_ept_invalidate(const Seraph_VMX_EPT* ept) {
    if (!ept) {
        return;
    }

    /* INVEPT type 1: single-context invalidation */
    invept(1, ept->eptp);
}

/*============================================================================
 * VM-Exit Handling
 *============================================================================*/

void seraph_vmx_register_exit_handler(Seraph_VMX_ExitReason reason,
                                       Seraph_VMX_ExitHandler handler) {
    if (reason < SERAPH_EXIT_REASON_MAX) {
        g_exit_handlers[reason] = handler;
    }
}

bool seraph_vmx_handle_exit(Seraph_VMX_Context* ctx) {
    if (!ctx) {
        return false;
    }

    uint32_t reason = ctx->exit_reason & 0xFFFF;  /* Basic exit reason */
    uint64_t qualification = ctx->exit_qual;

    if (reason < SERAPH_EXIT_REASON_MAX && g_exit_handlers[reason]) {
        return g_exit_handlers[reason](ctx, qualification);
    }

    /* No handler registered - return false to stop guest */
    return false;
}

bool seraph_vmx_handle_cpuid(Seraph_VMX_Context* ctx, uint64_t qualification) {
    (void)qualification;

    if (!ctx) {
        return false;
    }

    uint32_t leaf = (uint32_t)ctx->guest_regs.rax;
    uint32_t subleaf = (uint32_t)ctx->guest_regs.rcx;
    uint32_t eax, ebx, ecx, edx;

    /* Execute real CPUID */
    cpuid_count(leaf, subleaf, &eax, &ebx, &ecx, &edx);

    /*
     * Optionally hide VMX from guest by clearing CPUID.1:ECX[5]
     * This prevents nested virtualization attempts.
     */
    if (leaf == 1) {
        ecx &= ~SERAPH_CPUID_VMX_BIT;
    }

    /* Also hide hypervisor present bit if desired */
    if (leaf == 1) {
        ecx &= ~(1U << 31);  /* Clear hypervisor bit */
    }

    ctx->guest_regs.rax = eax;
    ctx->guest_regs.rbx = ebx;
    ctx->guest_regs.rcx = ecx;
    ctx->guest_regs.rdx = edx;

    /* Advance RIP past CPUID instruction (2 bytes: 0F A2) */
    seraph_vmx_advance_rip(ctx);

    return true;  /* Resume guest */
}

bool seraph_vmx_handle_hlt(Seraph_VMX_Context* ctx, uint64_t qualification) {
    (void)qualification;

    if (!ctx) {
        return false;
    }

    /*
     * HLT instruction - guest wants to idle.
     *
     * In a real hypervisor, we would:
     * 1. Check for pending interrupts
     * 2. If interrupts pending, inject and resume
     * 3. If not, yield CPU or wait for interrupt
     *
     * For now, we just advance RIP and resume.
     * A real implementation would need proper interrupt handling.
     */

    seraph_vmx_advance_rip(ctx);

    /* Return false to indicate guest is halted */
    return false;
}

bool seraph_vmx_handle_vmcall(Seraph_VMX_Context* ctx, uint64_t qualification) {
    (void)qualification;

    if (!ctx) {
        return false;
    }

    /*
     * VMCALL (hypercall) handling
     *
     * Register convention:
     *   RAX: Hypercall number
     *   RBX: Parameter 1
     *   RCX: Parameter 2
     *   RDX: Parameter 3
     *   RSI: Parameter 4
     *   RDI: Parameter 5
     *
     * Return value in RAX
     */

    uint64_t hc_num = ctx->guest_regs.rax;
    int64_t result = SERAPH_HC_SUCCESS;

    switch (hc_num) {
        case SERAPH_HC_NOP:
            /* No operation - just return success */
            break;

        case SERAPH_HC_VERSION:
            /* Return SERAPH hypervisor version */
            result = 0x00010000;  /* Version 1.0.0 */
            break;

        case SERAPH_HC_FEATURES:
            /* Return supported features bitmap */
            result = 0x0001;  /* Basic hypercalls supported */
            break;

        case SERAPH_HC_SHUTDOWN:
            /* Guest requests shutdown */
            ctx->guest_regs.rax = SERAPH_HC_SUCCESS;
            seraph_vmx_advance_rip(ctx);
            return false;  /* Stop guest */

        case SERAPH_HC_YIELD:
            /* Guest yields CPU - would trigger scheduler */
            break;

        case SERAPH_HC_DEBUG_PRINT:
            /* Debug print - RBX points to string in guest memory */
            /* Would need to translate address and print */
            break;

        default:
            result = SERAPH_HC_INVALID_CALL;
            break;
    }

    ctx->guest_regs.rax = (uint64_t)result;
    seraph_vmx_advance_rip(ctx);

    return true;  /* Resume guest */
}

bool seraph_vmx_handle_io(Seraph_VMX_Context* ctx, uint64_t qualification) {
    if (!ctx) {
        return false;
    }

    /*
     * I/O instruction exit qualification:
     *   Bits 2:0: Size (0=1 byte, 1=2 bytes, 3=4 bytes)
     *   Bit 3: Direction (0=OUT, 1=IN)
     *   Bit 4: String instruction
     *   Bit 5: REP prefix
     *   Bit 6: Operand encoding (0=DX, 1=immediate)
     *   Bits 31:16: Port number
     */

    uint16_t port = (qualification >> 16) & 0xFFFF;
    bool is_in = (qualification >> 3) & 1;
    uint8_t size = (qualification & 7) + 1;  /* 1, 2, or 4 bytes */

    (void)port;
    (void)is_in;
    (void)size;

    /*
     * For now, just return 0xFF for IN and ignore OUT.
     * A real implementation would route to Foreign Substrate
     * or emulate specific devices.
     */

    if (is_in) {
        uint32_t value = 0xFFFFFFFF;  /* Return all 1s */

        /* Mask to requested size */
        if (size == 1) value &= 0xFF;
        else if (size == 2) value &= 0xFFFF;

        ctx->guest_regs.rax = (ctx->guest_regs.rax & ~((1ULL << (size * 8)) - 1)) | value;
    }

    seraph_vmx_advance_rip(ctx);
    return true;
}

bool seraph_vmx_handle_ept_violation(Seraph_VMX_Context* ctx, uint64_t qualification) {
    if (!ctx) {
        return false;
    }

    /*
     * EPT violation - guest accessed memory not mapped in EPT
     *
     * This could be:
     * 1. Legitimate MMIO access (route to device)
     * 2. Invalid memory access (fault)
     * 3. Demand paging (allocate and map)
     */

    uint64_t guest_phys;
    seraph_vmx_vmread(SERAPH_VMCS_GUEST_PHYS_ADDR, &guest_phys);

    bool read_access = qualification & SERAPH_EPT_VIOL_READ;
    bool write_access = qualification & SERAPH_EPT_VIOL_WRITE;
    bool exec_access = qualification & SERAPH_EPT_VIOL_EXEC;

    (void)read_access;
    (void)write_access;
    (void)exec_access;
    (void)guest_phys;

    /*
     * For now, return false to stop guest on EPT violation.
     * A real implementation would handle MMIO or demand paging.
     */

    return false;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

const char* seraph_vmx_exit_reason_str(uint32_t reason) {
    static const char* names[] = {
        [SERAPH_EXIT_REASON_EXCEPTION_NMI]     = "Exception or NMI",
        [SERAPH_EXIT_REASON_EXTERNAL_INTR]     = "External Interrupt",
        [SERAPH_EXIT_REASON_TRIPLE_FAULT]      = "Triple Fault",
        [SERAPH_EXIT_REASON_INIT_SIGNAL]       = "INIT Signal",
        [SERAPH_EXIT_REASON_SIPI]              = "SIPI",
        [SERAPH_EXIT_REASON_IO_SMI]            = "I/O SMI",
        [SERAPH_EXIT_REASON_OTHER_SMI]         = "Other SMI",
        [SERAPH_EXIT_REASON_INTR_WINDOW]       = "Interrupt Window",
        [SERAPH_EXIT_REASON_NMI_WINDOW]        = "NMI Window",
        [SERAPH_EXIT_REASON_TASK_SWITCH]       = "Task Switch",
        [SERAPH_EXIT_REASON_CPUID]             = "CPUID",
        [SERAPH_EXIT_REASON_GETSEC]            = "GETSEC",
        [SERAPH_EXIT_REASON_HLT]               = "HLT",
        [SERAPH_EXIT_REASON_INVD]              = "INVD",
        [SERAPH_EXIT_REASON_INVLPG]            = "INVLPG",
        [SERAPH_EXIT_REASON_RDPMC]             = "RDPMC",
        [SERAPH_EXIT_REASON_RDTSC]             = "RDTSC",
        [SERAPH_EXIT_REASON_RSM]               = "RSM",
        [SERAPH_EXIT_REASON_VMCALL]            = "VMCALL (Hypercall)",
        [SERAPH_EXIT_REASON_VMCLEAR]           = "VMCLEAR",
        [SERAPH_EXIT_REASON_VMLAUNCH]          = "VMLAUNCH",
        [SERAPH_EXIT_REASON_VMPTRLD]           = "VMPTRLD",
        [SERAPH_EXIT_REASON_VMPTRST]           = "VMPTRST",
        [SERAPH_EXIT_REASON_VMREAD]            = "VMREAD",
        [SERAPH_EXIT_REASON_VMRESUME]          = "VMRESUME",
        [SERAPH_EXIT_REASON_VMWRITE]           = "VMWRITE",
        [SERAPH_EXIT_REASON_VMXOFF]            = "VMXOFF",
        [SERAPH_EXIT_REASON_VMXON]             = "VMXON",
        [SERAPH_EXIT_REASON_CR_ACCESS]         = "CR Access",
        [SERAPH_EXIT_REASON_MOV_DR]            = "MOV DR",
        [SERAPH_EXIT_REASON_IO]                = "I/O Instruction",
        [SERAPH_EXIT_REASON_RDMSR]             = "RDMSR",
        [SERAPH_EXIT_REASON_WRMSR]             = "WRMSR",
        [SERAPH_EXIT_REASON_INVALID_GUEST_STATE] = "Invalid Guest State",
        [SERAPH_EXIT_REASON_MSR_LOADING]       = "MSR Loading Failure",
        [SERAPH_EXIT_REASON_MWAIT]             = "MWAIT",
        [SERAPH_EXIT_REASON_MONITOR_TRAP]      = "Monitor Trap Flag",
        [SERAPH_EXIT_REASON_MONITOR]           = "MONITOR",
        [SERAPH_EXIT_REASON_PAUSE]             = "PAUSE",
        [SERAPH_EXIT_REASON_MCE_DURING_ENTRY]  = "Machine Check During Entry",
        [SERAPH_EXIT_REASON_TPR_BELOW_THRESHOLD] = "TPR Below Threshold",
        [SERAPH_EXIT_REASON_APIC_ACCESS]       = "APIC Access",
        [SERAPH_EXIT_REASON_VIRT_EOI]          = "Virtualized EOI",
        [SERAPH_EXIT_REASON_GDTR_IDTR_ACCESS]  = "GDTR/IDTR Access",
        [SERAPH_EXIT_REASON_LDTR_TR_ACCESS]    = "LDTR/TR Access",
        [SERAPH_EXIT_REASON_EPT_VIOLATION]     = "EPT Violation",
        [SERAPH_EXIT_REASON_EPT_MISCONFIG]     = "EPT Misconfiguration",
        [SERAPH_EXIT_REASON_INVEPT]            = "INVEPT",
        [SERAPH_EXIT_REASON_RDTSCP]            = "RDTSCP",
        [SERAPH_EXIT_REASON_PREEMPTION_TIMER]  = "Preemption Timer",
        [SERAPH_EXIT_REASON_INVVPID]           = "INVVPID",
        [SERAPH_EXIT_REASON_WBINVD]            = "WBINVD",
        [SERAPH_EXIT_REASON_XSETBV]            = "XSETBV",
        [SERAPH_EXIT_REASON_APIC_WRITE]        = "APIC Write",
        [SERAPH_EXIT_REASON_RDRAND]            = "RDRAND",
        [SERAPH_EXIT_REASON_INVPCID]           = "INVPCID",
        [SERAPH_EXIT_REASON_VMFUNC]            = "VMFUNC",
        [SERAPH_EXIT_REASON_ENCLS]             = "ENCLS",
        [SERAPH_EXIT_REASON_RDSEED]            = "RDSEED",
        [SERAPH_EXIT_REASON_PML_FULL]          = "PML Full",
        [SERAPH_EXIT_REASON_XSAVES]            = "XSAVES",
        [SERAPH_EXIT_REASON_XRSTORS]           = "XRSTORS",
    };

    uint32_t basic = reason & 0xFFFF;
    if (basic < SERAPH_EXIT_REASON_MAX && names[basic]) {
        return names[basic];
    }
    return "Unknown";
}

void seraph_vmx_advance_rip(Seraph_VMX_Context* ctx) {
    if (!ctx) {
        return;
    }

    uint64_t instr_len;
    if (seraph_vmx_vmread(SERAPH_VMCS_EXIT_INSTR_LENGTH, &instr_len)) {
        uint64_t rip;
        if (seraph_vmx_vmread(SERAPH_VMCS_GUEST_RIP, &rip)) {
            seraph_vmx_vmwrite(SERAPH_VMCS_GUEST_RIP, rip + instr_len);
        }
    }
}

bool seraph_vmx_inject_event(uint8_t vector, uint8_t type,
                              uint32_t error_code, bool has_error_code) {
    /*
     * VM-entry interruption-information field format:
     *   Bits 7:0: Vector
     *   Bits 10:8: Type (0=external, 2=NMI, 3=exception, 4=software int, 6=software exception)
     *   Bit 11: Deliver error code
     *   Bit 31: Valid
     */
    uint32_t info = vector |
                    ((uint32_t)type << 8) |
                    (has_error_code ? (1U << 11) : 0) |
                    (1U << 31);  /* Valid bit */

    if (!seraph_vmx_vmwrite(SERAPH_VMCS_ENTRY_INTR_INFO, info)) {
        return false;
    }

    if (has_error_code) {
        if (!seraph_vmx_vmwrite(SERAPH_VMCS_ENTRY_EXCEPTION_ERRCODE, error_code)) {
            return false;
        }
    }

    return true;
}

#endif /* SERAPH_KERNEL */
