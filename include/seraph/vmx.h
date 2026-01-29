/**
 * @file vmx.h
 * @brief VMX (Virtual Machine Extensions) - Intel VT-x Hypervisor Support
 *
 * This header provides complete Intel VMX support for SERAPH's Foreign Substrate
 * layer, enabling Linux to run as a guest VM to handle hardware drivers.
 *
 * VMX Architecture Overview:
 *   - VMXON region: 4KB-aligned memory to enable VMX operation
 *   - VMCS (Virtual Machine Control Structure): Controls VM execution
 *   - EPT (Extended Page Tables): Hardware-assisted nested paging
 *   - VM-exits: Events that transfer control from guest to host
 *   - Hypercalls: Guest-to-host communication via VMCALL instruction
 *
 * Memory Requirements:
 *   - VMXON region: 4KB aligned
 *   - VMCS: 4KB aligned (one per vCPU)
 *   - EPT tables: 4KB aligned (hierarchical like page tables)
 *
 * Reference: Intel SDM Volume 3C, Chapters 23-33
 */

#ifndef SERAPH_VMX_H
#define SERAPH_VMX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * VMX Capability Detection
 *============================================================================*/

/**
 * @brief CPUID feature bits for VMX detection
 *
 * VMX availability is indicated by CPUID.1:ECX[bit 5]
 */
#define SERAPH_CPUID_VMX_BIT            (1U << 5)

/**
 * @brief CR4 bit to enable VMX operation
 *
 * Must be set before executing VMXON instruction.
 */
#define SERAPH_CR4_VMXE                 (1ULL << 13)

/**
 * @brief CR0 bits required for VMX operation
 *
 * VMX requires PE (Protection Enable) and NE (Numeric Error)
 */
#define SERAPH_CR0_PE                   (1ULL << 0)
#define SERAPH_CR0_NE                   (1ULL << 5)

/*============================================================================
 * IA32_VMX_BASIC MSR (0x480)
 *
 * This MSR provides fundamental VMX capability information.
 *============================================================================*/

#define SERAPH_MSR_VMX_BASIC            0x480
#define SERAPH_MSR_VMX_PINBASED_CTLS    0x481
#define SERAPH_MSR_VMX_PROCBASED_CTLS   0x482
#define SERAPH_MSR_VMX_EXIT_CTLS        0x483
#define SERAPH_MSR_VMX_ENTRY_CTLS       0x484
#define SERAPH_MSR_VMX_MISC             0x485
#define SERAPH_MSR_VMX_CR0_FIXED0       0x486
#define SERAPH_MSR_VMX_CR0_FIXED1       0x487
#define SERAPH_MSR_VMX_CR4_FIXED0       0x488
#define SERAPH_MSR_VMX_CR4_FIXED1       0x489
#define SERAPH_MSR_VMX_VMCS_ENUM        0x48A
#define SERAPH_MSR_VMX_PROCBASED_CTLS2  0x48B
#define SERAPH_MSR_VMX_EPT_VPID_CAP     0x48C
#define SERAPH_MSR_VMX_TRUE_PINBASED    0x48D
#define SERAPH_MSR_VMX_TRUE_PROCBASED   0x48E
#define SERAPH_MSR_VMX_TRUE_EXIT        0x48F
#define SERAPH_MSR_VMX_TRUE_ENTRY       0x490

/**
 * @brief IA32_FEATURE_CONTROL MSR for VMX enable
 */
#define SERAPH_MSR_IA32_FEATURE_CONTROL 0x3A

/** Feature control bits */
#define SERAPH_FEATURE_CONTROL_LOCK     (1ULL << 0)
#define SERAPH_FEATURE_CONTROL_VMXON    (1ULL << 2)

/**
 * @brief VMX Basic Information structure
 *
 * Parsed from IA32_VMX_BASIC MSR
 */
typedef struct {
    uint32_t vmcs_revision;      /**< VMCS revision identifier (bits 30:0) */
    uint32_t vmcs_region_size;   /**< VMCS region size in bytes (bits 44:32) */
    bool     physaddr_32bit;     /**< If true, addresses limited to 32 bits */
    bool     dual_monitor;       /**< Dual-monitor SMM supported */
    uint8_t  memory_type;        /**< Memory type for VMCS access (bits 53:50) */
    bool     ins_outs_info;      /**< INS/OUTS info reported on VM-exit */
    bool     true_ctls;          /**< True controls MSRs available */
} Seraph_VMX_Basic;

/*============================================================================
 * VMCS Field Encoding
 *
 * VMCS fields are accessed via 32-bit encodings:
 *   Bits 0:     Access type (0=full, 1=high 32 bits of 64-bit field)
 *   Bits 9:1:   Index
 *   Bits 11:10: Type (0=control, 1=VM-exit info, 2=guest state, 3=host state)
 *   Bits 14:13: Width (0=16-bit, 1=64-bit, 2=32-bit, 3=natural width)
 *
 * We define all fields as constants for clarity.
 *============================================================================*/

/**
 * @brief VMCS field width encodings
 */
#define SERAPH_VMCS_WIDTH_16            (0 << 13)
#define SERAPH_VMCS_WIDTH_64            (1 << 13)
#define SERAPH_VMCS_WIDTH_32            (2 << 13)
#define SERAPH_VMCS_WIDTH_NATURAL       (3 << 13)

/**
 * @brief VMCS field type encodings
 */
#define SERAPH_VMCS_TYPE_CONTROL        (0 << 10)
#define SERAPH_VMCS_TYPE_VMEXIT_INFO    (1 << 10)
#define SERAPH_VMCS_TYPE_GUEST_STATE    (2 << 10)
#define SERAPH_VMCS_TYPE_HOST_STATE     (3 << 10)

/*----------------------------------------------------------------------------
 * 16-bit Control Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_VPID                        0x0000  /**< Virtual Processor ID */
#define SERAPH_VMCS_POSTED_INT_NOTIFY           0x0002  /**< Posted-interrupt notification vector */
#define SERAPH_VMCS_EPTP_INDEX                  0x0004  /**< EPTP index */

/*----------------------------------------------------------------------------
 * 16-bit Guest-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_GUEST_ES_SEL                0x0800  /**< ES selector */
#define SERAPH_VMCS_GUEST_CS_SEL                0x0802  /**< CS selector */
#define SERAPH_VMCS_GUEST_SS_SEL                0x0804  /**< SS selector */
#define SERAPH_VMCS_GUEST_DS_SEL                0x0806  /**< DS selector */
#define SERAPH_VMCS_GUEST_FS_SEL                0x0808  /**< FS selector */
#define SERAPH_VMCS_GUEST_GS_SEL                0x080A  /**< GS selector */
#define SERAPH_VMCS_GUEST_LDTR_SEL              0x080C  /**< LDTR selector */
#define SERAPH_VMCS_GUEST_TR_SEL                0x080E  /**< TR selector */
#define SERAPH_VMCS_GUEST_INTR_STATUS           0x0810  /**< Guest interrupt status */
#define SERAPH_VMCS_GUEST_PML_INDEX             0x0812  /**< PML index */

/*----------------------------------------------------------------------------
 * 16-bit Host-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_HOST_ES_SEL                 0x0C00  /**< ES selector */
#define SERAPH_VMCS_HOST_CS_SEL                 0x0C02  /**< CS selector */
#define SERAPH_VMCS_HOST_SS_SEL                 0x0C04  /**< SS selector */
#define SERAPH_VMCS_HOST_DS_SEL                 0x0C06  /**< DS selector */
#define SERAPH_VMCS_HOST_FS_SEL                 0x0C08  /**< FS selector */
#define SERAPH_VMCS_HOST_GS_SEL                 0x0C0A  /**< GS selector */
#define SERAPH_VMCS_HOST_TR_SEL                 0x0C0C  /**< TR selector */

/*----------------------------------------------------------------------------
 * 64-bit Control Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_IO_BITMAP_A                 0x2000  /**< I/O bitmap A address */
#define SERAPH_VMCS_IO_BITMAP_A_HIGH            0x2001
#define SERAPH_VMCS_IO_BITMAP_B                 0x2002  /**< I/O bitmap B address */
#define SERAPH_VMCS_IO_BITMAP_B_HIGH            0x2003
#define SERAPH_VMCS_MSR_BITMAP                  0x2004  /**< MSR bitmap address */
#define SERAPH_VMCS_MSR_BITMAP_HIGH             0x2005
#define SERAPH_VMCS_EXIT_MSR_STORE              0x2006  /**< VM-exit MSR-store address */
#define SERAPH_VMCS_EXIT_MSR_STORE_HIGH         0x2007
#define SERAPH_VMCS_EXIT_MSR_LOAD               0x2008  /**< VM-exit MSR-load address */
#define SERAPH_VMCS_EXIT_MSR_LOAD_HIGH          0x2009
#define SERAPH_VMCS_ENTRY_MSR_LOAD              0x200A  /**< VM-entry MSR-load address */
#define SERAPH_VMCS_ENTRY_MSR_LOAD_HIGH         0x200B
#define SERAPH_VMCS_EXECUTIVE_VMCS              0x200C  /**< Executive-VMCS pointer */
#define SERAPH_VMCS_EXECUTIVE_VMCS_HIGH         0x200D
#define SERAPH_VMCS_PML_ADDRESS                 0x200E  /**< PML address */
#define SERAPH_VMCS_PML_ADDRESS_HIGH            0x200F
#define SERAPH_VMCS_TSC_OFFSET                  0x2010  /**< TSC offset */
#define SERAPH_VMCS_TSC_OFFSET_HIGH             0x2011
#define SERAPH_VMCS_VIRTUAL_APIC                0x2012  /**< Virtual-APIC address */
#define SERAPH_VMCS_VIRTUAL_APIC_HIGH           0x2013
#define SERAPH_VMCS_APIC_ACCESS                 0x2014  /**< APIC-access address */
#define SERAPH_VMCS_APIC_ACCESS_HIGH            0x2015
#define SERAPH_VMCS_POSTED_INT_DESC             0x2016  /**< Posted-interrupt descriptor */
#define SERAPH_VMCS_POSTED_INT_DESC_HIGH        0x2017
#define SERAPH_VMCS_VM_FUNCTION_CTRL            0x2018  /**< VM-function controls */
#define SERAPH_VMCS_VM_FUNCTION_CTRL_HIGH       0x2019
#define SERAPH_VMCS_EPTP                        0x201A  /**< EPT pointer */
#define SERAPH_VMCS_EPTP_HIGH                   0x201B
#define SERAPH_VMCS_EOI_EXIT_BITMAP0            0x201C  /**< EOI-exit bitmap 0 */
#define SERAPH_VMCS_EOI_EXIT_BITMAP0_HIGH       0x201D
#define SERAPH_VMCS_EOI_EXIT_BITMAP1            0x201E  /**< EOI-exit bitmap 1 */
#define SERAPH_VMCS_EOI_EXIT_BITMAP1_HIGH       0x201F
#define SERAPH_VMCS_EOI_EXIT_BITMAP2            0x2020  /**< EOI-exit bitmap 2 */
#define SERAPH_VMCS_EOI_EXIT_BITMAP2_HIGH       0x2021
#define SERAPH_VMCS_EOI_EXIT_BITMAP3            0x2022  /**< EOI-exit bitmap 3 */
#define SERAPH_VMCS_EOI_EXIT_BITMAP3_HIGH       0x2023
#define SERAPH_VMCS_EPTP_LIST                   0x2024  /**< EPTP-list address */
#define SERAPH_VMCS_EPTP_LIST_HIGH              0x2025
#define SERAPH_VMCS_VMREAD_BITMAP               0x2026  /**< VMREAD bitmap address */
#define SERAPH_VMCS_VMREAD_BITMAP_HIGH          0x2027
#define SERAPH_VMCS_VMWRITE_BITMAP              0x2028  /**< VMWRITE bitmap address */
#define SERAPH_VMCS_VMWRITE_BITMAP_HIGH         0x2029
#define SERAPH_VMCS_VIRT_EXCEPTION_INFO         0x202A  /**< Virtualization-exception info */
#define SERAPH_VMCS_VIRT_EXCEPTION_INFO_HIGH    0x202B
#define SERAPH_VMCS_XSS_EXIT_BITMAP             0x202C  /**< XSS-exiting bitmap */
#define SERAPH_VMCS_XSS_EXIT_BITMAP_HIGH        0x202D
#define SERAPH_VMCS_ENCLS_EXIT_BITMAP           0x202E  /**< ENCLS-exiting bitmap */
#define SERAPH_VMCS_ENCLS_EXIT_BITMAP_HIGH      0x202F
#define SERAPH_VMCS_TSC_MULTIPLIER              0x2032  /**< TSC multiplier */
#define SERAPH_VMCS_TSC_MULTIPLIER_HIGH         0x2033

/*----------------------------------------------------------------------------
 * 64-bit Read-Only Data Fields (VM-exit information)
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_GUEST_PHYS_ADDR             0x2400  /**< Guest-physical address */
#define SERAPH_VMCS_GUEST_PHYS_ADDR_HIGH        0x2401

/*----------------------------------------------------------------------------
 * 64-bit Guest-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_VMCS_LINK_PTR               0x2800  /**< VMCS link pointer */
#define SERAPH_VMCS_VMCS_LINK_PTR_HIGH          0x2801
#define SERAPH_VMCS_GUEST_DEBUGCTL              0x2802  /**< Guest IA32_DEBUGCTL */
#define SERAPH_VMCS_GUEST_DEBUGCTL_HIGH         0x2803
#define SERAPH_VMCS_GUEST_PAT                   0x2804  /**< Guest IA32_PAT */
#define SERAPH_VMCS_GUEST_PAT_HIGH              0x2805
#define SERAPH_VMCS_GUEST_EFER                  0x2806  /**< Guest IA32_EFER */
#define SERAPH_VMCS_GUEST_EFER_HIGH             0x2807
#define SERAPH_VMCS_GUEST_PERF_GLOBAL           0x2808  /**< Guest IA32_PERF_GLOBAL_CTRL */
#define SERAPH_VMCS_GUEST_PERF_GLOBAL_HIGH      0x2809
#define SERAPH_VMCS_GUEST_PDPTE0                0x280A  /**< Guest PDPTE0 (PAE paging) */
#define SERAPH_VMCS_GUEST_PDPTE0_HIGH           0x280B
#define SERAPH_VMCS_GUEST_PDPTE1                0x280C  /**< Guest PDPTE1 */
#define SERAPH_VMCS_GUEST_PDPTE1_HIGH           0x280D
#define SERAPH_VMCS_GUEST_PDPTE2                0x280E  /**< Guest PDPTE2 */
#define SERAPH_VMCS_GUEST_PDPTE2_HIGH           0x280F
#define SERAPH_VMCS_GUEST_PDPTE3                0x2810  /**< Guest PDPTE3 */
#define SERAPH_VMCS_GUEST_PDPTE3_HIGH           0x2811
#define SERAPH_VMCS_GUEST_BNDCFGS               0x2812  /**< Guest IA32_BNDCFGS */
#define SERAPH_VMCS_GUEST_BNDCFGS_HIGH          0x2813

/*----------------------------------------------------------------------------
 * 64-bit Host-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_HOST_PAT                    0x2C00  /**< Host IA32_PAT */
#define SERAPH_VMCS_HOST_PAT_HIGH               0x2C01
#define SERAPH_VMCS_HOST_EFER                   0x2C02  /**< Host IA32_EFER */
#define SERAPH_VMCS_HOST_EFER_HIGH              0x2C03
#define SERAPH_VMCS_HOST_PERF_GLOBAL            0x2C04  /**< Host IA32_PERF_GLOBAL_CTRL */
#define SERAPH_VMCS_HOST_PERF_GLOBAL_HIGH       0x2C05

/*----------------------------------------------------------------------------
 * 32-bit Control Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_PIN_BASED_CTLS              0x4000  /**< Pin-based VM-execution controls */
#define SERAPH_VMCS_PROC_BASED_CTLS             0x4002  /**< Primary processor-based controls */
#define SERAPH_VMCS_EXCEPTION_BITMAP            0x4004  /**< Exception bitmap */
#define SERAPH_VMCS_PAGE_FAULT_ERROR_MASK       0x4006  /**< Page-fault error-code mask */
#define SERAPH_VMCS_PAGE_FAULT_ERROR_MATCH      0x4008  /**< Page-fault error-code match */
#define SERAPH_VMCS_CR3_TARGET_COUNT            0x400A  /**< CR3-target count */
#define SERAPH_VMCS_EXIT_CTLS                   0x400C  /**< VM-exit controls */
#define SERAPH_VMCS_EXIT_MSR_STORE_COUNT        0x400E  /**< VM-exit MSR-store count */
#define SERAPH_VMCS_EXIT_MSR_LOAD_COUNT         0x4010  /**< VM-exit MSR-load count */
#define SERAPH_VMCS_ENTRY_CTLS                  0x4012  /**< VM-entry controls */
#define SERAPH_VMCS_ENTRY_MSR_LOAD_COUNT        0x4014  /**< VM-entry MSR-load count */
#define SERAPH_VMCS_ENTRY_INTR_INFO             0x4016  /**< VM-entry interruption-info field */
#define SERAPH_VMCS_ENTRY_EXCEPTION_ERRCODE     0x4018  /**< VM-entry exception error code */
#define SERAPH_VMCS_ENTRY_INSTR_LENGTH          0x401A  /**< VM-entry instruction length */
#define SERAPH_VMCS_TPR_THRESHOLD               0x401C  /**< TPR threshold */
#define SERAPH_VMCS_PROC_BASED_CTLS2            0x401E  /**< Secondary processor-based controls */
#define SERAPH_VMCS_PLE_GAP                     0x4020  /**< PLE_Gap */
#define SERAPH_VMCS_PLE_WINDOW                  0x4022  /**< PLE_Window */

/*----------------------------------------------------------------------------
 * 32-bit Read-Only Data Fields (VM-exit information)
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_VM_INSTR_ERROR              0x4400  /**< VM-instruction error */
#define SERAPH_VMCS_EXIT_REASON                 0x4402  /**< Exit reason */
#define SERAPH_VMCS_EXIT_INTR_INFO              0x4404  /**< VM-exit interruption information */
#define SERAPH_VMCS_EXIT_INTR_ERROR             0x4406  /**< VM-exit interruption error code */
#define SERAPH_VMCS_IDT_VECTORING_INFO          0x4408  /**< IDT-vectoring information field */
#define SERAPH_VMCS_IDT_VECTORING_ERROR         0x440A  /**< IDT-vectoring error code */
#define SERAPH_VMCS_EXIT_INSTR_LENGTH           0x440C  /**< VM-exit instruction length */
#define SERAPH_VMCS_EXIT_INSTR_INFO             0x440E  /**< VM-exit instruction information */

/*----------------------------------------------------------------------------
 * 32-bit Guest-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_GUEST_ES_LIMIT              0x4800  /**< ES limit */
#define SERAPH_VMCS_GUEST_CS_LIMIT              0x4802  /**< CS limit */
#define SERAPH_VMCS_GUEST_SS_LIMIT              0x4804  /**< SS limit */
#define SERAPH_VMCS_GUEST_DS_LIMIT              0x4806  /**< DS limit */
#define SERAPH_VMCS_GUEST_FS_LIMIT              0x4808  /**< FS limit */
#define SERAPH_VMCS_GUEST_GS_LIMIT              0x480A  /**< GS limit */
#define SERAPH_VMCS_GUEST_LDTR_LIMIT            0x480C  /**< LDTR limit */
#define SERAPH_VMCS_GUEST_TR_LIMIT              0x480E  /**< TR limit */
#define SERAPH_VMCS_GUEST_GDTR_LIMIT            0x4810  /**< GDTR limit */
#define SERAPH_VMCS_GUEST_IDTR_LIMIT            0x4812  /**< IDTR limit */
#define SERAPH_VMCS_GUEST_ES_ACCESS             0x4814  /**< ES access rights */
#define SERAPH_VMCS_GUEST_CS_ACCESS             0x4816  /**< CS access rights */
#define SERAPH_VMCS_GUEST_SS_ACCESS             0x4818  /**< SS access rights */
#define SERAPH_VMCS_GUEST_DS_ACCESS             0x481A  /**< DS access rights */
#define SERAPH_VMCS_GUEST_FS_ACCESS             0x481C  /**< FS access rights */
#define SERAPH_VMCS_GUEST_GS_ACCESS             0x481E  /**< GS access rights */
#define SERAPH_VMCS_GUEST_LDTR_ACCESS           0x4820  /**< LDTR access rights */
#define SERAPH_VMCS_GUEST_TR_ACCESS             0x4822  /**< TR access rights */
#define SERAPH_VMCS_GUEST_INTR_STATE            0x4824  /**< Guest interruptibility state */
#define SERAPH_VMCS_GUEST_ACTIVITY_STATE        0x4826  /**< Guest activity state */
#define SERAPH_VMCS_GUEST_SMBASE                0x4828  /**< Guest SMBASE */
#define SERAPH_VMCS_GUEST_SYSENTER_CS           0x482A  /**< Guest IA32_SYSENTER_CS */
#define SERAPH_VMCS_GUEST_PREEMPTION_TIMER      0x482E  /**< VMX-preemption timer value */

/*----------------------------------------------------------------------------
 * 32-bit Host-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_HOST_SYSENTER_CS            0x4C00  /**< Host IA32_SYSENTER_CS */

/*----------------------------------------------------------------------------
 * Natural-Width Control Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_CR0_GUEST_HOST_MASK         0x6000  /**< CR0 guest/host mask */
#define SERAPH_VMCS_CR4_GUEST_HOST_MASK         0x6002  /**< CR4 guest/host mask */
#define SERAPH_VMCS_CR0_READ_SHADOW             0x6004  /**< CR0 read shadow */
#define SERAPH_VMCS_CR4_READ_SHADOW             0x6006  /**< CR4 read shadow */
#define SERAPH_VMCS_CR3_TARGET_0                0x6008  /**< CR3-target value 0 */
#define SERAPH_VMCS_CR3_TARGET_1                0x600A  /**< CR3-target value 1 */
#define SERAPH_VMCS_CR3_TARGET_2                0x600C  /**< CR3-target value 2 */
#define SERAPH_VMCS_CR3_TARGET_3                0x600E  /**< CR3-target value 3 */

/*----------------------------------------------------------------------------
 * Natural-Width Read-Only Data Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_EXIT_QUALIFICATION          0x6400  /**< Exit qualification */
#define SERAPH_VMCS_IO_RCX                      0x6402  /**< I/O RCX */
#define SERAPH_VMCS_IO_RSI                      0x6404  /**< I/O RSI */
#define SERAPH_VMCS_IO_RDI                      0x6406  /**< I/O RDI */
#define SERAPH_VMCS_IO_RIP                      0x6408  /**< I/O RIP */
#define SERAPH_VMCS_GUEST_LINEAR_ADDR           0x640A  /**< Guest-linear address */

/*----------------------------------------------------------------------------
 * Natural-Width Guest-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_GUEST_CR0                   0x6800  /**< Guest CR0 */
#define SERAPH_VMCS_GUEST_CR3                   0x6802  /**< Guest CR3 */
#define SERAPH_VMCS_GUEST_CR4                   0x6804  /**< Guest CR4 */
#define SERAPH_VMCS_GUEST_ES_BASE               0x6806  /**< ES base */
#define SERAPH_VMCS_GUEST_CS_BASE               0x6808  /**< CS base */
#define SERAPH_VMCS_GUEST_SS_BASE               0x680A  /**< SS base */
#define SERAPH_VMCS_GUEST_DS_BASE               0x680C  /**< DS base */
#define SERAPH_VMCS_GUEST_FS_BASE               0x680E  /**< FS base */
#define SERAPH_VMCS_GUEST_GS_BASE               0x6810  /**< GS base */
#define SERAPH_VMCS_GUEST_LDTR_BASE             0x6812  /**< LDTR base */
#define SERAPH_VMCS_GUEST_TR_BASE               0x6814  /**< TR base */
#define SERAPH_VMCS_GUEST_GDTR_BASE             0x6816  /**< GDTR base */
#define SERAPH_VMCS_GUEST_IDTR_BASE             0x6818  /**< IDTR base */
#define SERAPH_VMCS_GUEST_DR7                   0x681A  /**< Guest DR7 */
#define SERAPH_VMCS_GUEST_RSP                   0x681C  /**< Guest RSP */
#define SERAPH_VMCS_GUEST_RIP                   0x681E  /**< Guest RIP */
#define SERAPH_VMCS_GUEST_RFLAGS                0x6820  /**< Guest RFLAGS */
#define SERAPH_VMCS_GUEST_PENDING_DBG           0x6822  /**< Guest pending debug exceptions */
#define SERAPH_VMCS_GUEST_SYSENTER_ESP          0x6824  /**< Guest IA32_SYSENTER_ESP */
#define SERAPH_VMCS_GUEST_SYSENTER_EIP          0x6826  /**< Guest IA32_SYSENTER_EIP */

/*----------------------------------------------------------------------------
 * Natural-Width Host-State Fields
 *----------------------------------------------------------------------------*/
#define SERAPH_VMCS_HOST_CR0                    0x6C00  /**< Host CR0 */
#define SERAPH_VMCS_HOST_CR3                    0x6C02  /**< Host CR3 */
#define SERAPH_VMCS_HOST_CR4                    0x6C04  /**< Host CR4 */
#define SERAPH_VMCS_HOST_FS_BASE                0x6C06  /**< Host FS base */
#define SERAPH_VMCS_HOST_GS_BASE                0x6C08  /**< Host GS base */
#define SERAPH_VMCS_HOST_TR_BASE                0x6C0A  /**< Host TR base */
#define SERAPH_VMCS_HOST_GDTR_BASE              0x6C0C  /**< Host GDTR base */
#define SERAPH_VMCS_HOST_IDTR_BASE              0x6C0E  /**< Host IDTR base */
#define SERAPH_VMCS_HOST_SYSENTER_ESP           0x6C10  /**< Host IA32_SYSENTER_ESP */
#define SERAPH_VMCS_HOST_SYSENTER_EIP           0x6C12  /**< Host IA32_SYSENTER_EIP */
#define SERAPH_VMCS_HOST_RSP                    0x6C14  /**< Host RSP */
#define SERAPH_VMCS_HOST_RIP                    0x6C16  /**< Host RIP */

/*============================================================================
 * VM-Execution Control Bits
 *============================================================================*/

/**
 * @brief Pin-based VM-execution controls
 */
typedef enum {
    SERAPH_PIN_EXTERNAL_INTR_EXIT   = (1U << 0),  /**< External-interrupt exiting */
    SERAPH_PIN_NMI_EXIT             = (1U << 3),  /**< NMI exiting */
    SERAPH_PIN_VIRTUAL_NMI          = (1U << 5),  /**< Virtual NMIs */
    SERAPH_PIN_PREEMPTION_TIMER     = (1U << 6),  /**< Activate VMX-preemption timer */
    SERAPH_PIN_POSTED_INTERRUPTS    = (1U << 7),  /**< Process posted interrupts */
} Seraph_VMX_PinControls;

/**
 * @brief Primary processor-based VM-execution controls
 */
typedef enum {
    SERAPH_PROC_INTR_WINDOW_EXIT    = (1U << 2),  /**< Interrupt-window exiting */
    SERAPH_PROC_TSC_OFFSET          = (1U << 3),  /**< Use TSC offsetting */
    SERAPH_PROC_HLT_EXIT            = (1U << 7),  /**< HLT exiting */
    SERAPH_PROC_INVLPG_EXIT         = (1U << 9),  /**< INVLPG exiting */
    SERAPH_PROC_MWAIT_EXIT          = (1U << 10), /**< MWAIT exiting */
    SERAPH_PROC_RDPMC_EXIT          = (1U << 11), /**< RDPMC exiting */
    SERAPH_PROC_RDTSC_EXIT          = (1U << 12), /**< RDTSC exiting */
    SERAPH_PROC_CR3_LOAD_EXIT       = (1U << 15), /**< CR3-load exiting */
    SERAPH_PROC_CR3_STORE_EXIT      = (1U << 16), /**< CR3-store exiting */
    SERAPH_PROC_CR8_LOAD_EXIT       = (1U << 19), /**< CR8-load exiting */
    SERAPH_PROC_CR8_STORE_EXIT      = (1U << 20), /**< CR8-store exiting */
    SERAPH_PROC_TPR_SHADOW          = (1U << 21), /**< Use TPR shadow */
    SERAPH_PROC_NMI_WINDOW_EXIT     = (1U << 22), /**< NMI-window exiting */
    SERAPH_PROC_MOV_DR_EXIT         = (1U << 23), /**< MOV-DR exiting */
    SERAPH_PROC_UNCOND_IO_EXIT      = (1U << 24), /**< Unconditional I/O exiting */
    SERAPH_PROC_USE_IO_BITMAPS      = (1U << 25), /**< Use I/O bitmaps */
    SERAPH_PROC_MONITOR_TRAP        = (1U << 27), /**< Monitor trap flag */
    SERAPH_PROC_USE_MSR_BITMAPS     = (1U << 28), /**< Use MSR bitmaps */
    SERAPH_PROC_MONITOR_EXIT        = (1U << 29), /**< MONITOR exiting */
    SERAPH_PROC_PAUSE_EXIT          = (1U << 30), /**< PAUSE exiting */
    SERAPH_PROC_SECONDARY_CTLS      = (1U << 31), /**< Activate secondary controls */
} Seraph_VMX_ProcControls;

/**
 * @brief Secondary processor-based VM-execution controls
 */
typedef enum {
    SERAPH_PROC2_VIRT_APIC_ACCESS   = (1U << 0),  /**< Virtualize APIC accesses */
    SERAPH_PROC2_ENABLE_EPT         = (1U << 1),  /**< Enable EPT */
    SERAPH_PROC2_DESC_TABLE_EXIT    = (1U << 2),  /**< Descriptor-table exiting */
    SERAPH_PROC2_RDTSCP             = (1U << 3),  /**< Enable RDTSCP */
    SERAPH_PROC2_VIRT_X2APIC        = (1U << 4),  /**< Virtualize x2APIC mode */
    SERAPH_PROC2_ENABLE_VPID        = (1U << 5),  /**< Enable VPID */
    SERAPH_PROC2_WBINVD_EXIT        = (1U << 6),  /**< WBINVD exiting */
    SERAPH_PROC2_UNRESTRICTED       = (1U << 7),  /**< Unrestricted guest */
    SERAPH_PROC2_APIC_REG_VIRT      = (1U << 8),  /**< APIC-register virtualization */
    SERAPH_PROC2_VIRT_INTR_DELIVERY = (1U << 9),  /**< Virtual-interrupt delivery */
    SERAPH_PROC2_PAUSE_LOOP_EXIT    = (1U << 10), /**< PAUSE-loop exiting */
    SERAPH_PROC2_RDRAND_EXIT        = (1U << 11), /**< RDRAND exiting */
    SERAPH_PROC2_INVPCID            = (1U << 12), /**< Enable INVPCID */
    SERAPH_PROC2_VMFUNC             = (1U << 13), /**< Enable VM functions */
    SERAPH_PROC2_VMCS_SHADOW        = (1U << 14), /**< VMCS shadowing */
    SERAPH_PROC2_ENCLS_EXIT         = (1U << 15), /**< ENCLS exiting */
    SERAPH_PROC2_RDSEED_EXIT        = (1U << 16), /**< RDSEED exiting */
    SERAPH_PROC2_PML                = (1U << 17), /**< Enable PML */
    SERAPH_PROC2_EPT_VIOLATION_VE   = (1U << 18), /**< EPT-violation #VE */
    SERAPH_PROC2_CONCEAL_VMX        = (1U << 19), /**< Conceal VMX from PT */
    SERAPH_PROC2_XSAVES             = (1U << 20), /**< Enable XSAVES/XRSTORS */
    SERAPH_PROC2_MODE_BASED_EPT     = (1U << 22), /**< Mode-based execute control */
    SERAPH_PROC2_TSC_SCALING        = (1U << 25), /**< Use TSC scaling */
} Seraph_VMX_Proc2Controls;

/**
 * @brief VM-exit controls
 */
typedef enum {
    SERAPH_EXIT_SAVE_DEBUG_CTLS     = (1U << 2),  /**< Save debug controls */
    SERAPH_EXIT_HOST_LONG_MODE      = (1U << 9),  /**< Host address-space size (64-bit) */
    SERAPH_EXIT_LOAD_PERF_GLOBAL    = (1U << 12), /**< Load IA32_PERF_GLOBAL_CTRL */
    SERAPH_EXIT_ACK_INTR_ON_EXIT    = (1U << 15), /**< Acknowledge interrupt on exit */
    SERAPH_EXIT_SAVE_PAT            = (1U << 18), /**< Save IA32_PAT */
    SERAPH_EXIT_LOAD_PAT            = (1U << 19), /**< Load IA32_PAT */
    SERAPH_EXIT_SAVE_EFER           = (1U << 20), /**< Save IA32_EFER */
    SERAPH_EXIT_LOAD_EFER           = (1U << 21), /**< Load IA32_EFER */
    SERAPH_EXIT_SAVE_PREEMPT_TIMER  = (1U << 22), /**< Save VMX-preemption timer value */
    SERAPH_EXIT_CLEAR_BNDCFGS       = (1U << 23), /**< Clear IA32_BNDCFGS */
    SERAPH_EXIT_CONCEAL_VMX_FROM_PT = (1U << 24), /**< Conceal VMX from PT */
} Seraph_VMX_ExitControls;

/**
 * @brief VM-entry controls
 */
typedef enum {
    SERAPH_ENTRY_LOAD_DEBUG_CTLS    = (1U << 2),  /**< Load debug controls */
    SERAPH_ENTRY_GUEST_LONG_MODE    = (1U << 9),  /**< IA-32e mode guest */
    SERAPH_ENTRY_SMM                = (1U << 10), /**< Entry to SMM */
    SERAPH_ENTRY_DEACT_DUAL_MONITOR = (1U << 11), /**< Deactivate dual-monitor treatment */
    SERAPH_ENTRY_LOAD_PERF_GLOBAL   = (1U << 13), /**< Load IA32_PERF_GLOBAL_CTRL */
    SERAPH_ENTRY_LOAD_PAT           = (1U << 14), /**< Load IA32_PAT */
    SERAPH_ENTRY_LOAD_EFER          = (1U << 15), /**< Load IA32_EFER */
    SERAPH_ENTRY_LOAD_BNDCFGS       = (1U << 16), /**< Load IA32_BNDCFGS */
    SERAPH_ENTRY_CONCEAL_VMX_FROM_PT= (1U << 17), /**< Conceal VMX from PT */
} Seraph_VMX_EntryControls;

/*============================================================================
 * VM-Exit Reasons
 *
 * When a VM-exit occurs, the exit reason is stored in the VMCS.
 * The basic exit reason is in bits 15:0 of the exit reason field.
 *============================================================================*/

typedef enum {
    SERAPH_EXIT_REASON_EXCEPTION_NMI        = 0,   /**< Exception or NMI */
    SERAPH_EXIT_REASON_EXTERNAL_INTR        = 1,   /**< External interrupt */
    SERAPH_EXIT_REASON_TRIPLE_FAULT         = 2,   /**< Triple fault */
    SERAPH_EXIT_REASON_INIT_SIGNAL          = 3,   /**< INIT signal */
    SERAPH_EXIT_REASON_SIPI                 = 4,   /**< Start-up IPI (SIPI) */
    SERAPH_EXIT_REASON_IO_SMI               = 5,   /**< I/O system-management interrupt */
    SERAPH_EXIT_REASON_OTHER_SMI            = 6,   /**< Other SMI */
    SERAPH_EXIT_REASON_INTR_WINDOW          = 7,   /**< Interrupt window */
    SERAPH_EXIT_REASON_NMI_WINDOW           = 8,   /**< NMI window */
    SERAPH_EXIT_REASON_TASK_SWITCH          = 9,   /**< Task switch */
    SERAPH_EXIT_REASON_CPUID                = 10,  /**< CPUID instruction */
    SERAPH_EXIT_REASON_GETSEC               = 11,  /**< GETSEC instruction */
    SERAPH_EXIT_REASON_HLT                  = 12,  /**< HLT instruction */
    SERAPH_EXIT_REASON_INVD                 = 13,  /**< INVD instruction */
    SERAPH_EXIT_REASON_INVLPG               = 14,  /**< INVLPG instruction */
    SERAPH_EXIT_REASON_RDPMC                = 15,  /**< RDPMC instruction */
    SERAPH_EXIT_REASON_RDTSC                = 16,  /**< RDTSC instruction */
    SERAPH_EXIT_REASON_RSM                  = 17,  /**< RSM instruction */
    SERAPH_EXIT_REASON_VMCALL               = 18,  /**< VMCALL instruction (hypercall) */
    SERAPH_EXIT_REASON_VMCLEAR              = 19,  /**< VMCLEAR instruction */
    SERAPH_EXIT_REASON_VMLAUNCH             = 20,  /**< VMLAUNCH instruction */
    SERAPH_EXIT_REASON_VMPTRLD              = 21,  /**< VMPTRLD instruction */
    SERAPH_EXIT_REASON_VMPTRST              = 22,  /**< VMPTRST instruction */
    SERAPH_EXIT_REASON_VMREAD               = 23,  /**< VMREAD instruction */
    SERAPH_EXIT_REASON_VMRESUME             = 24,  /**< VMRESUME instruction */
    SERAPH_EXIT_REASON_VMWRITE              = 25,  /**< VMWRITE instruction */
    SERAPH_EXIT_REASON_VMXOFF               = 26,  /**< VMXOFF instruction */
    SERAPH_EXIT_REASON_VMXON                = 27,  /**< VMXON instruction */
    SERAPH_EXIT_REASON_CR_ACCESS            = 28,  /**< Control-register access */
    SERAPH_EXIT_REASON_MOV_DR               = 29,  /**< MOV DR instruction */
    SERAPH_EXIT_REASON_IO                   = 30,  /**< I/O instruction */
    SERAPH_EXIT_REASON_RDMSR                = 31,  /**< RDMSR instruction */
    SERAPH_EXIT_REASON_WRMSR                = 32,  /**< WRMSR instruction */
    SERAPH_EXIT_REASON_INVALID_GUEST_STATE  = 33,  /**< VM-entry failure: invalid guest state */
    SERAPH_EXIT_REASON_MSR_LOADING          = 34,  /**< VM-entry failure: MSR loading */
    SERAPH_EXIT_REASON_MWAIT                = 36,  /**< MWAIT instruction */
    SERAPH_EXIT_REASON_MONITOR_TRAP         = 37,  /**< Monitor trap flag */
    SERAPH_EXIT_REASON_MONITOR              = 39,  /**< MONITOR instruction */
    SERAPH_EXIT_REASON_PAUSE                = 40,  /**< PAUSE instruction */
    SERAPH_EXIT_REASON_MCE_DURING_ENTRY     = 41,  /**< VM-entry failure: machine-check */
    SERAPH_EXIT_REASON_TPR_BELOW_THRESHOLD  = 43,  /**< TPR below threshold */
    SERAPH_EXIT_REASON_APIC_ACCESS          = 44,  /**< APIC access */
    SERAPH_EXIT_REASON_VIRT_EOI             = 45,  /**< Virtualized EOI */
    SERAPH_EXIT_REASON_GDTR_IDTR_ACCESS     = 46,  /**< Access to GDTR or IDTR */
    SERAPH_EXIT_REASON_LDTR_TR_ACCESS       = 47,  /**< Access to LDTR or TR */
    SERAPH_EXIT_REASON_EPT_VIOLATION        = 48,  /**< EPT violation */
    SERAPH_EXIT_REASON_EPT_MISCONFIG        = 49,  /**< EPT misconfiguration */
    SERAPH_EXIT_REASON_INVEPT               = 50,  /**< INVEPT instruction */
    SERAPH_EXIT_REASON_RDTSCP               = 51,  /**< RDTSCP instruction */
    SERAPH_EXIT_REASON_PREEMPTION_TIMER     = 52,  /**< VMX-preemption timer expired */
    SERAPH_EXIT_REASON_INVVPID              = 53,  /**< INVVPID instruction */
    SERAPH_EXIT_REASON_WBINVD               = 54,  /**< WBINVD instruction */
    SERAPH_EXIT_REASON_XSETBV               = 55,  /**< XSETBV instruction */
    SERAPH_EXIT_REASON_APIC_WRITE           = 56,  /**< APIC write */
    SERAPH_EXIT_REASON_RDRAND               = 57,  /**< RDRAND instruction */
    SERAPH_EXIT_REASON_INVPCID              = 58,  /**< INVPCID instruction */
    SERAPH_EXIT_REASON_VMFUNC               = 59,  /**< VMFUNC instruction */
    SERAPH_EXIT_REASON_ENCLS                = 60,  /**< ENCLS instruction */
    SERAPH_EXIT_REASON_RDSEED               = 61,  /**< RDSEED instruction */
    SERAPH_EXIT_REASON_PML_FULL             = 62,  /**< Page-modification log full */
    SERAPH_EXIT_REASON_XSAVES               = 63,  /**< XSAVES instruction */
    SERAPH_EXIT_REASON_XRSTORS              = 64,  /**< XRSTORS instruction */
    SERAPH_EXIT_REASON_MAX                  = 65   /**< Maximum exit reason value */
} Seraph_VMX_ExitReason;

/** Bit to indicate VM-entry failure in exit reason */
#define SERAPH_EXIT_REASON_ENTRY_FAIL       (1U << 31)

/*============================================================================
 * EPT (Extended Page Tables) Structures
 *
 * EPT provides hardware-assisted nested paging. EPT entries use a similar
 * 4-level hierarchy to regular page tables but with different bit meanings:
 *   - Bits 2:0: Read/Write/Execute permissions
 *   - Bit 7: Large page (like PS bit in regular page tables)
 *   - Bits 5:3: EPT memory type (UC=0, WC=1, WT=4, WP=5, WB=6)
 *============================================================================*/

/**
 * @brief EPT entry permission bits
 */
#define SERAPH_EPT_READ                 (1ULL << 0)   /**< Read access */
#define SERAPH_EPT_WRITE                (1ULL << 1)   /**< Write access */
#define SERAPH_EPT_EXECUTE              (1ULL << 2)   /**< Execute access */
#define SERAPH_EPT_IGNORE_PAT           (1ULL << 6)   /**< Ignore guest PAT memory type */
#define SERAPH_EPT_LARGE_PAGE           (1ULL << 7)   /**< 2MB or 1GB page */
#define SERAPH_EPT_ACCESSED             (1ULL << 8)   /**< Accessed (if enabled) */
#define SERAPH_EPT_DIRTY                (1ULL << 9)   /**< Dirty (if enabled) */
#define SERAPH_EPT_USER_EXECUTE         (1ULL << 10)  /**< User-mode execute (if mode-based) */

/** EPT memory type encodings (bits 5:3) */
#define SERAPH_EPT_MT_UC                (0ULL << 3)   /**< Uncacheable */
#define SERAPH_EPT_MT_WC                (1ULL << 3)   /**< Write Combining */
#define SERAPH_EPT_MT_WT                (4ULL << 3)   /**< Write Through */
#define SERAPH_EPT_MT_WP                (5ULL << 3)   /**< Write Protect */
#define SERAPH_EPT_MT_WB                (6ULL << 3)   /**< Write Back */

/** Physical address mask for EPT entries (bits 51:12) */
#define SERAPH_EPT_ADDR_MASK            0x000FFFFFFFFFF000ULL

/** All RWX permissions */
#define SERAPH_EPT_RWX                  (SERAPH_EPT_READ | SERAPH_EPT_WRITE | SERAPH_EPT_EXECUTE)

/**
 * @brief EPT pointer (EPTP) format
 *
 * The EPTP field in the VMCS specifies the root of the EPT hierarchy.
 * Format:
 *   Bits 2:0:   EPT page-walk length minus 1 (typically 3 for 4-level)
 *   Bit 6:      Enable accessed and dirty flags
 *   Bits 51:12: Physical address of EPT PML4 table
 */
#define SERAPH_EPTP_WL_4                (3ULL << 0)   /**< 4-level page walk */
#define SERAPH_EPTP_MT_WB               (6ULL << 0)   /**< Memory type WB (in bits 2:0) */
#define SERAPH_EPTP_AD_ENABLE           (1ULL << 6)   /**< Enable accessed/dirty */

/**
 * @brief Build EPTP value from physical address of PML4
 *
 * @param pml4_phys Physical address of EPT PML4 (must be 4KB aligned)
 * @return EPTP value for VMCS
 */
static inline uint64_t seraph_vmx_make_eptp(uint64_t pml4_phys) {
    return (pml4_phys & SERAPH_EPT_ADDR_MASK) |
           SERAPH_EPTP_WL_4 |
           SERAPH_EPTP_MT_WB |
           SERAPH_EPTP_AD_ENABLE;
}

/**
 * @brief EPT violation exit qualification bits
 */
#define SERAPH_EPT_VIOL_READ            (1ULL << 0)   /**< Caused by read */
#define SERAPH_EPT_VIOL_WRITE           (1ULL << 1)   /**< Caused by write */
#define SERAPH_EPT_VIOL_EXEC            (1ULL << 2)   /**< Caused by instruction fetch */
#define SERAPH_EPT_VIOL_READABLE        (1ULL << 3)   /**< Entry was readable */
#define SERAPH_EPT_VIOL_WRITABLE        (1ULL << 4)   /**< Entry was writable */
#define SERAPH_EPT_VIOL_EXECUTABLE      (1ULL << 5)   /**< Entry was executable */
#define SERAPH_EPT_VIOL_GPA_VALID       (1ULL << 7)   /**< Guest physical addr is valid */
#define SERAPH_EPT_VIOL_GLA_VALID       (1ULL << 8)   /**< Guest linear addr is valid */

/*============================================================================
 * Hypercall Interface for Foreign Substrate Communication
 *
 * The guest (Linux driver substrate) uses VMCALL to communicate with SERAPH.
 * Register convention:
 *   RAX: Hypercall number
 *   RBX: Parameter 1
 *   RCX: Parameter 2
 *   RDX: Parameter 3
 *   RSI: Parameter 4 (for extended calls)
 *   RDI: Parameter 5 (for extended calls)
 *
 * Return value in RAX (0 = success, negative = error)
 *============================================================================*/

/**
 * @brief Hypercall numbers for Foreign Substrate operations
 */
typedef enum {
    /** System hypercalls (0x0000-0x00FF) */
    SERAPH_HC_NOP               = 0x0000,  /**< No operation (used for probing) */
    SERAPH_HC_VERSION           = 0x0001,  /**< Get hypervisor version */
    SERAPH_HC_FEATURES          = 0x0002,  /**< Query available features */
    SERAPH_HC_SHUTDOWN          = 0x0003,  /**< Request guest shutdown */
    SERAPH_HC_YIELD             = 0x0004,  /**< Yield CPU to host */

    /** Memory hypercalls (0x0100-0x01FF) */
    SERAPH_HC_MAP_MMIO          = 0x0100,  /**< Map MMIO region into guest */
    SERAPH_HC_UNMAP_MMIO        = 0x0101,  /**< Unmap MMIO region */
    SERAPH_HC_SHARE_MEMORY      = 0x0102,  /**< Share memory with host */
    SERAPH_HC_UNSHARE_MEMORY    = 0x0103,  /**< Unshare memory */
    SERAPH_HC_DMA_ALLOC         = 0x0104,  /**< Allocate DMA-capable memory */
    SERAPH_HC_DMA_FREE          = 0x0105,  /**< Free DMA memory */

    /** Device hypercalls (0x0200-0x02FF) */
    SERAPH_HC_DEVICE_PROBE      = 0x0200,  /**< Probe for device */
    SERAPH_HC_DEVICE_READ       = 0x0201,  /**< Read from device register */
    SERAPH_HC_DEVICE_WRITE      = 0x0202,  /**< Write to device register */
    SERAPH_HC_DEVICE_IRQ_ACK    = 0x0203,  /**< Acknowledge device interrupt */
    SERAPH_HC_DEVICE_IRQ_ENABLE = 0x0204,  /**< Enable device interrupt */
    SERAPH_HC_DEVICE_IRQ_DISABLE= 0x0205,  /**< Disable device interrupt */

    /** Ring buffer hypercalls (0x0300-0x03FF) */
    SERAPH_HC_RING_CREATE       = 0x0300,  /**< Create shared ring buffer */
    SERAPH_HC_RING_DESTROY      = 0x0301,  /**< Destroy ring buffer */
    SERAPH_HC_RING_NOTIFY       = 0x0302,  /**< Notify host of ring update */
    SERAPH_HC_RING_WAIT         = 0x0303,  /**< Wait for ring notification */

    /** Debug hypercalls (0xFF00-0xFFFF) */
    SERAPH_HC_DEBUG_PRINT       = 0xFF00,  /**< Print debug message */
    SERAPH_HC_DEBUG_BREAK       = 0xFF01,  /**< Break into debugger */
} Seraph_Hypercall;

/** Hypercall return codes */
#define SERAPH_HC_SUCCESS           0
#define SERAPH_HC_ERROR            -1
#define SERAPH_HC_INVALID_CALL     -2
#define SERAPH_HC_INVALID_PARAM    -3
#define SERAPH_HC_NO_MEMORY        -4
#define SERAPH_HC_NOT_SUPPORTED    -5
#define SERAPH_HC_BUSY             -6

/*============================================================================
 * VMX State Structures
 *============================================================================*/

/**
 * @brief Guest register state saved on VM-exit
 *
 * When a VM-exit occurs, we need to save/restore general-purpose registers.
 * The VMCS only handles some state; GPRs must be saved manually.
 */
typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    /* RSP and RIP are in VMCS */
} Seraph_VMX_GuestRegs;

/**
 * @brief VMCS state for a single vCPU
 */
typedef struct {
    void*    vmcs_region;       /**< Pointer to VMCS (4KB aligned) */
    uint64_t vmcs_phys;         /**< Physical address of VMCS */
    void*    vmxon_region;      /**< Pointer to VMXON region (4KB aligned) */
    uint64_t vmxon_phys;        /**< Physical address of VMXON region */
    bool     vmx_enabled;       /**< Is VMX operation active? */
    bool     vmcs_loaded;       /**< Is VMCS currently loaded? */
    uint32_t vmcs_revision;     /**< VMCS revision from IA32_VMX_BASIC */
} Seraph_VMX_VCPU;

/**
 * @brief EPT context for guest physical address translation
 */
typedef struct {
    void*    ept_pml4;          /**< EPT PML4 table (4KB aligned) */
    uint64_t ept_pml4_phys;     /**< Physical address of EPT PML4 */
    uint64_t eptp;              /**< Full EPTP value for VMCS */
    uint64_t guest_phys_limit;  /**< Maximum guest physical address */
    uint64_t mapped_pages;      /**< Number of mapped guest pages */
} Seraph_VMX_EPT;

/**
 * @brief Complete VMX context for a guest VM
 */
typedef struct {
    Seraph_VMX_VCPU     vcpu;           /**< vCPU state */
    Seraph_VMX_EPT      ept;            /**< EPT state */
    Seraph_VMX_GuestRegs guest_regs;    /**< Saved guest registers */
    uint64_t            host_rsp;       /**< Host RSP for VM-exit */
    uint64_t            host_rip;       /**< Host RIP for VM-exit handler */
    uint32_t            exit_reason;    /**< Last VM-exit reason */
    uint64_t            exit_qual;      /**< Last VM-exit qualification */
    bool                guest_running;  /**< Is guest currently running? */
    uint32_t            guest_id;       /**< Unique guest identifier */
} Seraph_VMX_Context;

/*============================================================================
 * VMX Operations API
 *============================================================================*/

/**
 * @brief Check if CPU supports VMX
 *
 * Checks CPUID.1:ECX[5] for VMX support.
 *
 * @return true if VMX is supported
 */
bool seraph_vmx_supported(void);

/**
 * @brief Read VMX capabilities from MSRs
 *
 * Parses IA32_VMX_BASIC and related MSRs.
 *
 * @param basic Output structure for VMX basic info
 * @return true on success
 */
bool seraph_vmx_read_capabilities(Seraph_VMX_Basic* basic);

/**
 * @brief Initialize VMX operation on current CPU
 *
 * Performs:
 *   1. Check VMX support
 *   2. Enable VMX via CR4.VMXE
 *   3. Allocate and initialize VMXON region
 *   4. Execute VMXON instruction
 *
 * @param vcpu vCPU structure to initialize
 * @return true on success
 */
bool seraph_vmx_enable(Seraph_VMX_VCPU* vcpu);

/**
 * @brief Disable VMX operation on current CPU
 *
 * Executes VMXOFF and clears CR4.VMXE.
 *
 * @param vcpu vCPU structure
 */
void seraph_vmx_disable(Seraph_VMX_VCPU* vcpu);

/**
 * @brief Allocate and clear a VMCS
 *
 * Allocates 4KB-aligned VMCS and writes revision ID.
 *
 * @param vcpu vCPU structure
 * @return true on success
 */
bool seraph_vmx_alloc_vmcs(Seraph_VMX_VCPU* vcpu);

/**
 * @brief Free VMCS memory
 *
 * @param vcpu vCPU structure
 */
void seraph_vmx_free_vmcs(Seraph_VMX_VCPU* vcpu);

/**
 * @brief Load VMCS as current
 *
 * Executes VMPTRLD to make this VMCS current.
 *
 * @param vcpu vCPU structure
 * @return true on success
 */
bool seraph_vmx_load_vmcs(Seraph_VMX_VCPU* vcpu);

/**
 * @brief Clear and deactivate VMCS
 *
 * Executes VMCLEAR to deactivate VMCS.
 *
 * @param vcpu vCPU structure
 * @return true on success
 */
bool seraph_vmx_clear_vmcs(Seraph_VMX_VCPU* vcpu);

/**
 * @brief Read a VMCS field
 *
 * @param field VMCS field encoding
 * @param value Output value
 * @return true on success
 */
bool seraph_vmx_vmread(uint32_t field, uint64_t* value);

/**
 * @brief Write a VMCS field
 *
 * @param field VMCS field encoding
 * @param value Value to write
 * @return true on success
 */
bool seraph_vmx_vmwrite(uint32_t field, uint64_t value);

/**
 * @brief Set up VMCS guest state for 64-bit Linux
 *
 * Configures VMCS with initial guest state for launching Linux.
 *
 * @param ctx VMX context
 * @param entry_point Guest entry point (RIP)
 * @param stack_ptr Guest stack pointer (RSP)
 * @param page_table Guest CR3 (page table root)
 * @return true on success
 */
bool seraph_vmx_setup_guest_state(Seraph_VMX_Context* ctx,
                                   uint64_t entry_point,
                                   uint64_t stack_ptr,
                                   uint64_t page_table);

/**
 * @brief Set up VMCS host state
 *
 * Configures VMCS with host (SERAPH) state for VM-exit handling.
 *
 * @param ctx VMX context
 * @return true on success
 */
bool seraph_vmx_setup_host_state(Seraph_VMX_Context* ctx);

/**
 * @brief Set up VM-execution control fields
 *
 * Configures pin-based, processor-based, and secondary controls.
 *
 * @param ctx VMX context
 * @return true on success
 */
bool seraph_vmx_setup_controls(Seraph_VMX_Context* ctx);

/**
 * @brief Launch guest VM
 *
 * Executes VMLAUNCH to start guest execution.
 * Returns on VM-exit.
 *
 * @param ctx VMX context
 * @return VM-exit reason
 */
uint32_t seraph_vmx_launch(Seraph_VMX_Context* ctx);

/**
 * @brief Resume guest VM
 *
 * Executes VMRESUME to continue guest execution after handling VM-exit.
 *
 * @param ctx VMX context
 * @return VM-exit reason
 */
uint32_t seraph_vmx_resume(Seraph_VMX_Context* ctx);

/*============================================================================
 * EPT Management API
 *============================================================================*/

/**
 * @brief Initialize EPT for a guest
 *
 * Allocates EPT PML4 and optionally sets up identity mapping.
 *
 * @param ept EPT context
 * @param guest_memory_size Size of guest physical memory
 * @param identity_map If true, create 1:1 mapping
 * @return true on success
 */
bool seraph_vmx_ept_init(Seraph_VMX_EPT* ept,
                          uint64_t guest_memory_size,
                          bool identity_map);

/**
 * @brief Map guest physical to host physical address in EPT
 *
 * @param ept EPT context
 * @param guest_phys Guest physical address
 * @param host_phys Host physical address
 * @param size Mapping size in bytes (will be page-aligned)
 * @param flags EPT permission flags
 * @return true on success
 */
bool seraph_vmx_ept_map(Seraph_VMX_EPT* ept,
                         uint64_t guest_phys,
                         uint64_t host_phys,
                         uint64_t size,
                         uint64_t flags);

/**
 * @brief Unmap guest physical address range
 *
 * @param ept EPT context
 * @param guest_phys Guest physical address
 * @param size Size in bytes
 */
void seraph_vmx_ept_unmap(Seraph_VMX_EPT* ept,
                           uint64_t guest_phys,
                           uint64_t size);

/**
 * @brief Translate guest physical to host physical via EPT
 *
 * @param ept EPT context
 * @param guest_phys Guest physical address
 * @return Host physical address, or 0 on failure
 */
uint64_t seraph_vmx_ept_translate(const Seraph_VMX_EPT* ept,
                                   uint64_t guest_phys);

/**
 * @brief Free all EPT tables
 *
 * @param ept EPT context
 */
void seraph_vmx_ept_destroy(Seraph_VMX_EPT* ept);

/**
 * @brief Invalidate EPT TLB entries
 *
 * @param ept EPT context
 */
void seraph_vmx_ept_invalidate(const Seraph_VMX_EPT* ept);

/*============================================================================
 * VM-Exit Handling
 *============================================================================*/

/**
 * @brief VM-exit handler function type
 *
 * @param ctx VMX context
 * @param qualification Exit qualification value
 * @return true to resume guest, false to stop
 */
typedef bool (*Seraph_VMX_ExitHandler)(Seraph_VMX_Context* ctx, uint64_t qualification);

/**
 * @brief Register a VM-exit handler
 *
 * @param reason VM-exit reason to handle
 * @param handler Handler function
 */
void seraph_vmx_register_exit_handler(Seraph_VMX_ExitReason reason,
                                       Seraph_VMX_ExitHandler handler);

/**
 * @brief Dispatch VM-exit to appropriate handler
 *
 * Reads exit reason and calls registered handler.
 *
 * @param ctx VMX context
 * @return true to resume guest, false to stop
 */
bool seraph_vmx_handle_exit(Seraph_VMX_Context* ctx);

/**
 * @brief Default handler for CPUID exit
 *
 * Emulates CPUID instruction, optionally hiding VMX from guest.
 *
 * @param ctx VMX context
 * @param qualification Exit qualification (unused for CPUID)
 * @return true (always resume)
 */
bool seraph_vmx_handle_cpuid(Seraph_VMX_Context* ctx, uint64_t qualification);

/**
 * @brief Default handler for HLT exit
 *
 * @param ctx VMX context
 * @param qualification Exit qualification
 * @return true if guest should resume, false if halted
 */
bool seraph_vmx_handle_hlt(Seraph_VMX_Context* ctx, uint64_t qualification);

/**
 * @brief Default handler for VMCALL (hypercall) exit
 *
 * Dispatches hypercall based on RAX value.
 *
 * @param ctx VMX context
 * @param qualification Exit qualification
 * @return true to resume guest
 */
bool seraph_vmx_handle_vmcall(Seraph_VMX_Context* ctx, uint64_t qualification);

/**
 * @brief Default handler for I/O instruction exit
 *
 * @param ctx VMX context
 * @param qualification I/O exit qualification (port, size, direction)
 * @return true to resume guest
 */
bool seraph_vmx_handle_io(Seraph_VMX_Context* ctx, uint64_t qualification);

/**
 * @brief Default handler for EPT violation
 *
 * @param ctx VMX context
 * @param qualification EPT violation qualification
 * @return true if handled, false if fatal
 */
bool seraph_vmx_handle_ept_violation(Seraph_VMX_Context* ctx, uint64_t qualification);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Get VM-exit reason name as string
 *
 * @param reason VM-exit reason
 * @return String name of exit reason
 */
const char* seraph_vmx_exit_reason_str(uint32_t reason);

/**
 * @brief Advance guest RIP past current instruction
 *
 * Reads VM-exit instruction length and updates guest RIP.
 *
 * @param ctx VMX context
 */
void seraph_vmx_advance_rip(Seraph_VMX_Context* ctx);

/**
 * @brief Inject an interrupt/exception into guest
 *
 * Sets VM-entry interruption-information field.
 *
 * @param vector Interrupt/exception vector
 * @param type Type (0=external, 2=NMI, 3=exception, 4=software int)
 * @param error_code Error code (for exceptions that have one)
 * @param has_error_code Whether to deliver error code
 * @return true on success
 */
bool seraph_vmx_inject_event(uint8_t vector, uint8_t type,
                              uint32_t error_code, bool has_error_code);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_VMX_H */
