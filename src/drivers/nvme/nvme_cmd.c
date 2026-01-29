/**
 * @file nvme_cmd.c
 * @brief MC24: The Infinite Drive - NVMe Command Construction
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * This module provides helper functions to construct NVMe commands.
 * Each NVMe command is 64 bytes with a common header and command-specific
 * fields in CDW10-CDW15.
 *
 * COMMAND STRUCTURE:
 *
 *   Dword 0: Opcode (8), Flags (8), CID (16)
 *   Dword 1: NSID (32)
 *   Dword 2-3: Reserved (64)
 *   Dword 4-5: MPTR - Metadata Pointer (64)
 *   Dword 6-7: PRP1 - Data pointer 1 (64)
 *   Dword 8-9: PRP2 - Data pointer 2 or PRP list (64)
 *   Dword 10-15: Command-specific (192)
 *
 * PRP (Physical Region Page) ADDRESSING:
 *
 *   For data < 1 page: PRP1 only
 *   For data 1-2 pages: PRP1 + PRP2
 *   For data > 2 pages: PRP1 + PRP2 points to PRP list
 *
 *   PRP entries must be page-aligned (except first entry which can
 *   have non-zero offset).
 */

#include "seraph/drivers/nvme.h"
#include <string.h>

/*============================================================================
 * Admin Command Construction
 *============================================================================*/

/**
 * @brief Build Identify Controller command
 *
 * CDW10.CNS = 0x01 (Identify Controller)
 */
void seraph_nvme_cmd_identify_ctrl(Seraph_NVMe_Cmd* cmd, uint64_t prp) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_IDENTIFY;
    cmd->nsid = 0;
    cmd->prp1 = prp;
    cmd->cdw10 = 0x01;  /* CNS = Identify Controller */
}

/**
 * @brief Build Identify Namespace command
 *
 * CDW10.CNS = 0x00 (Identify Namespace)
 */
void seraph_nvme_cmd_identify_ns(Seraph_NVMe_Cmd* cmd, uint32_t nsid, uint64_t prp) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_IDENTIFY;
    cmd->nsid = nsid;
    cmd->prp1 = prp;
    cmd->cdw10 = 0x00;  /* CNS = Identify Namespace */
}

/**
 * @brief Build Create I/O Completion Queue command
 *
 * CDW10: QSIZE[31:16] | QID[15:0]
 * CDW11: IV[31:16] | IEN[1] | PC[0]
 *
 * @param cmd Output command
 * @param qid Queue ID (1-65535)
 * @param prp Physical address of CQ
 * @param size Queue size minus 1 (QSIZE field)
 * @param vector Interrupt vector (0 for polling)
 */
void seraph_nvme_cmd_create_cq(Seraph_NVMe_Cmd* cmd, uint16_t qid,
                                uint64_t prp, uint16_t size, uint16_t vector) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_CREATE_CQ;
    cmd->prp1 = prp;

    /* CDW10: Queue Size (31:16) | Queue ID (15:0) */
    cmd->cdw10 = ((uint32_t)size << 16) | qid;

    /* CDW11: Interrupt Vector (31:16) | IEN (1) | PC (0)
     * PC=1: Physically Contiguous
     * IEN=0: Interrupts disabled (polling mode) */
    cmd->cdw11 = ((uint32_t)vector << 16) | 0x01;  /* PC=1, IEN=0 */
}

/**
 * @brief Build Create I/O Submission Queue command
 *
 * CDW10: QSIZE[31:16] | QID[15:0]
 * CDW11: CQID[31:16] | QPRIO[2:1] | PC[0]
 *
 * @param cmd Output command
 * @param qid Queue ID (1-65535)
 * @param prp Physical address of SQ
 * @param size Queue size minus 1 (QSIZE field)
 * @param cqid Associated Completion Queue ID
 */
void seraph_nvme_cmd_create_sq(Seraph_NVMe_Cmd* cmd, uint16_t qid,
                                uint64_t prp, uint16_t size, uint16_t cqid) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_CREATE_SQ;
    cmd->prp1 = prp;

    /* CDW10: Queue Size (31:16) | Queue ID (15:0) */
    cmd->cdw10 = ((uint32_t)size << 16) | qid;

    /* CDW11: CQ ID (31:16) | QPRIO (2:1) | PC (0)
     * QPRIO=0: Urgent priority
     * PC=1: Physically Contiguous */
    cmd->cdw11 = ((uint32_t)cqid << 16) | 0x01;  /* PC=1, QPRIO=0 */
}

/**
 * @brief Build Delete I/O Submission Queue command
 *
 * CDW10: QID[15:0]
 */
void seraph_nvme_cmd_delete_sq(Seraph_NVMe_Cmd* cmd, uint16_t qid) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_DELETE_SQ;
    cmd->cdw10 = qid;
}

/**
 * @brief Build Delete I/O Completion Queue command
 *
 * CDW10: QID[15:0]
 */
void seraph_nvme_cmd_delete_cq(Seraph_NVMe_Cmd* cmd, uint16_t qid) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_DELETE_CQ;
    cmd->cdw10 = qid;
}

/**
 * @brief Build Get Log Page command
 *
 * CDW10: NUMDL[31:16] | LID[7:0]
 * CDW11: NUMDU[15:0]
 * CDW12: LPOL
 * CDW13: LPOU
 */
void seraph_nvme_cmd_get_log(Seraph_NVMe_Cmd* cmd, uint8_t log_id,
                              uint64_t prp, uint32_t size) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_GET_LOG;
    cmd->prp1 = prp;

    /* Number of dwords minus 1 */
    uint32_t numd = (size / 4) - 1;

    cmd->cdw10 = ((numd & 0xFFFF) << 16) | log_id;
    cmd->cdw11 = (numd >> 16) & 0xFFFF;
    cmd->cdw12 = 0;  /* Log Page Offset Lower */
    cmd->cdw13 = 0;  /* Log Page Offset Upper */
}

/**
 * @brief Build Set Features command
 *
 * CDW10: SV[31] | FID[7:0]
 * CDW11-14: Feature-specific
 */
void seraph_nvme_cmd_set_features(Seraph_NVMe_Cmd* cmd, uint8_t feature_id,
                                   uint32_t cdw11, uint64_t prp) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_SET_FEATURES;
    cmd->prp1 = prp;
    cmd->cdw10 = feature_id;
    cmd->cdw11 = cdw11;
}

/**
 * @brief Build Get Features command
 */
void seraph_nvme_cmd_get_features(Seraph_NVMe_Cmd* cmd, uint8_t feature_id,
                                   uint64_t prp) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_ADMIN_GET_FEATURES;
    cmd->prp1 = prp;
    cmd->cdw10 = feature_id;
}

/*============================================================================
 * NVM (I/O) Command Construction
 *============================================================================*/

/**
 * @brief Build Read command
 *
 * CDW10: SLBA[31:0] (lower 32 bits of starting LBA)
 * CDW11: SLBA[63:32] (upper 32 bits of starting LBA)
 * CDW12: NLB[15:0] (number of logical blocks minus 1)
 *
 * @param cmd Output command
 * @param nsid Namespace ID
 * @param lba Starting Logical Block Address
 * @param blocks Number of blocks minus 1 (NLB field)
 * @param prp1 First PRP entry (or only PRP for single page)
 * @param prp2 Second PRP entry (or PRP list for >2 pages)
 */
void seraph_nvme_cmd_read(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                           uint64_t lba, uint16_t blocks,
                           uint64_t prp1, uint64_t prp2) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_CMD_READ;
    cmd->nsid = nsid;
    cmd->prp1 = prp1;
    cmd->prp2 = prp2;
    cmd->cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd->cdw11 = (uint32_t)(lba >> 32);
    cmd->cdw12 = blocks;  /* NLB = number of blocks minus 1 */
}

/**
 * @brief Build Write command
 *
 * Same structure as Read command.
 */
void seraph_nvme_cmd_write(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                            uint64_t lba, uint16_t blocks,
                            uint64_t prp1, uint64_t prp2) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_CMD_WRITE;
    cmd->nsid = nsid;
    cmd->prp1 = prp1;
    cmd->prp2 = prp2;
    cmd->cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd->cdw11 = (uint32_t)(lba >> 32);
    cmd->cdw12 = blocks;
}

/**
 * @brief Build Flush command
 *
 * Ensures all previously written data is persisted.
 * No data transfer, so no PRPs needed.
 */
void seraph_nvme_cmd_flush(Seraph_NVMe_Cmd* cmd, uint32_t nsid) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_CMD_FLUSH;
    cmd->nsid = nsid;
}

/**
 * @brief Build Write Zeros command
 *
 * CDW10-11: SLBA
 * CDW12: NLB[15:0] | DEAC[25] | ...
 *
 * Deallocates blocks and sets them to zero.
 */
void seraph_nvme_cmd_write_zeros(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                                  uint64_t lba, uint16_t blocks) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_CMD_WRITE_ZEROS;
    cmd->nsid = nsid;
    cmd->cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd->cdw11 = (uint32_t)(lba >> 32);
    cmd->cdw12 = blocks;
}

/**
 * @brief Build Dataset Management command (for TRIM/Deallocate)
 *
 * CDW10: NR[7:0] (number of ranges minus 1)
 * CDW11: AD[2] (Attribute - Deallocate)
 *
 * PRP points to an array of range descriptors.
 */
void seraph_nvme_cmd_dsm(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                          uint8_t num_ranges, uint64_t prp, bool deallocate) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_CMD_DATASET_MGMT;
    cmd->nsid = nsid;
    cmd->prp1 = prp;
    cmd->cdw10 = num_ranges;  /* NR = number of ranges minus 1 */
    cmd->cdw11 = deallocate ? (1u << 2) : 0;  /* AD bit */
}

/**
 * @brief Build Compare command
 *
 * Compares data buffer with on-device data.
 * Same structure as Read/Write commands.
 */
void seraph_nvme_cmd_compare(Seraph_NVMe_Cmd* cmd, uint32_t nsid,
                              uint64_t lba, uint16_t blocks,
                              uint64_t prp1, uint64_t prp2) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->opc = SERAPH_NVME_CMD_COMPARE;
    cmd->nsid = nsid;
    cmd->prp1 = prp1;
    cmd->prp2 = prp2;
    cmd->cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd->cdw11 = (uint32_t)(lba >> 32);
    cmd->cdw12 = blocks;
}

/*============================================================================
 * PRP List Helpers
 *============================================================================*/

/**
 * @brief Calculate number of PRPs needed for a transfer
 *
 * @param offset Starting offset within first page
 * @param length Total transfer length
 * @param page_size Page size (usually 4096)
 * @return Number of PRP entries needed
 */
uint32_t seraph_nvme_prp_count(uint64_t offset, size_t length, size_t page_size) {
    if (length == 0) {
        return 0;
    }

    /* First PRP covers from offset to end of first page */
    size_t first_page_len = page_size - (offset % page_size);
    if (first_page_len >= length) {
        return 1;  /* Single PRP sufficient */
    }

    /* Remaining pages after first */
    size_t remaining = length - first_page_len;
    uint32_t additional = (uint32_t)((remaining + page_size - 1) / page_size);

    return 1 + additional;
}

/**
 * @brief Build PRP list for large transfers
 *
 * @param prp_list Output PRP list (must be page-aligned)
 * @param buffer Data buffer
 * @param length Transfer length
 * @param page_size Page size
 * @return Number of PRPs written
 */
uint32_t seraph_nvme_build_prp_list(uint64_t* prp_list, void* buffer,
                                     size_t length, size_t page_size) {
    if (prp_list == NULL || buffer == NULL || length == 0) {
        return 0;
    }

    uint8_t* ptr = (uint8_t*)buffer;
    size_t offset = (uintptr_t)ptr % page_size;
    uint32_t count = 0;

    /* First PRP entry */
    prp_list[count++] = (uint64_t)(uintptr_t)ptr;

    /* First page remaining */
    size_t first_page_remaining = page_size - offset;
    if (first_page_remaining >= length) {
        return count;
    }

    ptr += first_page_remaining;
    length -= first_page_remaining;

    /* Subsequent pages (must be page-aligned) */
    while (length > 0) {
        prp_list[count++] = (uint64_t)(uintptr_t)ptr;
        size_t this_page = (length > page_size) ? page_size : length;
        ptr += this_page;
        length -= this_page;
    }

    return count;
}
