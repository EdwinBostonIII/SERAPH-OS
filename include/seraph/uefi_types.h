/**
 * @file uefi_types.h
 * @brief MC16: NIH UEFI Type Definitions
 *
 * Pure NIH (Not Invented Here) implementation of UEFI types.
 * No EDK2 dependency - we define everything ourselves.
 *
 * This file provides all the UEFI types, constants, and structures
 * needed for a minimal UEFI bootloader without pulling in the
 * massive EDK2 build system.
 */

#ifndef SERAPH_UEFI_TYPES_H
#define SERAPH_UEFI_TYPES_H

#include <stdint.h>

/*============================================================================
 * Basic UEFI Types
 *============================================================================*/

typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef uint8_t   BOOLEAN;
typedef uint16_t  CHAR16;
typedef void      VOID;

typedef UINT64    UINTN;
typedef INT64     INTN;

typedef UINT64    EFI_STATUS;
typedef VOID*     EFI_HANDLE;
typedef VOID*     EFI_EVENT;
typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef UINT64    EFI_VIRTUAL_ADDRESS;
typedef UINT64    EFI_LBA;
typedef UINTN     EFI_TPL;

#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define EFIAPI __attribute__((ms_abi))

#define TRUE  1
#define FALSE 0
#ifndef NULL
#define NULL  ((VOID*)0)
#endif

/*============================================================================
 * EFI Status Codes
 *============================================================================*/

#define EFI_SUCCESS               0ULL
#define EFI_ERROR_MASK            0x8000000000000000ULL

#define EFI_ERROR(status)         (((INTN)(status)) < 0)

#define EFI_LOAD_ERROR            (EFI_ERROR_MASK | 1)
#define EFI_INVALID_PARAMETER     (EFI_ERROR_MASK | 2)
#define EFI_UNSUPPORTED           (EFI_ERROR_MASK | 3)
#define EFI_BAD_BUFFER_SIZE       (EFI_ERROR_MASK | 4)
#define EFI_BUFFER_TOO_SMALL      (EFI_ERROR_MASK | 5)
#define EFI_NOT_READY             (EFI_ERROR_MASK | 6)
#define EFI_DEVICE_ERROR          (EFI_ERROR_MASK | 7)
#define EFI_WRITE_PROTECTED       (EFI_ERROR_MASK | 8)
#define EFI_OUT_OF_RESOURCES      (EFI_ERROR_MASK | 9)
#define EFI_VOLUME_CORRUPTED      (EFI_ERROR_MASK | 10)
#define EFI_VOLUME_FULL           (EFI_ERROR_MASK | 11)
#define EFI_NO_MEDIA              (EFI_ERROR_MASK | 12)
#define EFI_MEDIA_CHANGED         (EFI_ERROR_MASK | 13)
#define EFI_NOT_FOUND             (EFI_ERROR_MASK | 14)
#define EFI_ACCESS_DENIED         (EFI_ERROR_MASK | 15)
#define EFI_NO_RESPONSE           (EFI_ERROR_MASK | 16)
#define EFI_NO_MAPPING            (EFI_ERROR_MASK | 17)
#define EFI_TIMEOUT               (EFI_ERROR_MASK | 18)
#define EFI_NOT_STARTED           (EFI_ERROR_MASK | 19)
#define EFI_ALREADY_STARTED       (EFI_ERROR_MASK | 20)
#define EFI_ABORTED               (EFI_ERROR_MASK | 21)
#define EFI_ICMP_ERROR            (EFI_ERROR_MASK | 22)
#define EFI_TFTP_ERROR            (EFI_ERROR_MASK | 23)
#define EFI_PROTOCOL_ERROR        (EFI_ERROR_MASK | 24)
#define EFI_INCOMPATIBLE_VERSION  (EFI_ERROR_MASK | 25)
#define EFI_SECURITY_VIOLATION    (EFI_ERROR_MASK | 26)
#define EFI_CRC_ERROR             (EFI_ERROR_MASK | 27)
#define EFI_END_OF_MEDIA          (EFI_ERROR_MASK | 28)
#define EFI_END_OF_FILE           (EFI_ERROR_MASK | 31)
#define EFI_INVALID_LANGUAGE      (EFI_ERROR_MASK | 32)
#define EFI_COMPROMISED_DATA      (EFI_ERROR_MASK | 33)

/*============================================================================
 * GUIDs
 *============================================================================*/

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

#define EFI_GUID_DEF(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    { (a), (b), (c), { (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) } }

/** Graphics Output Protocol GUID */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    EFI_GUID_DEF(0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a)

/** Loaded Image Protocol GUID */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    EFI_GUID_DEF(0x5b1b31a1, 0x9562, 0x11d2, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

/** Simple File System Protocol GUID */
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    EFI_GUID_DEF(0x0964e5b22, 0x6459, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

/** File Info GUID */
#define EFI_FILE_INFO_ID \
    EFI_GUID_DEF(0x09576e92, 0x6d3f, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

/** ACPI 2.0 Table GUID */
#define EFI_ACPI_20_TABLE_GUID \
    EFI_GUID_DEF(0x8868e871, 0xe4f1, 0x11d3, 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81)

/** ACPI 1.0 Table GUID */
#define EFI_ACPI_TABLE_GUID \
    EFI_GUID_DEF(0xeb9d2d30, 0x2d88, 0x11d3, 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)

/** SMBIOS Table GUID */
#define SMBIOS_TABLE_GUID \
    EFI_GUID_DEF(0xeb9d2d31, 0x2d88, 0x11d3, 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)

/** SMBIOS3 Table GUID */
#define SMBIOS3_TABLE_GUID \
    EFI_GUID_DEF(0xf2fd1544, 0x9794, 0x4a2c, 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94)

/*============================================================================
 * Memory Types and Allocation
 *============================================================================*/

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

/** Memory descriptor */
typedef struct {
    UINT32              Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64              NumberOfPages;
    UINT64              Attribute;
} EFI_MEMORY_DESCRIPTOR;

/** Memory attributes */
#define EFI_MEMORY_UC            0x0000000000000001ULL
#define EFI_MEMORY_WC            0x0000000000000002ULL
#define EFI_MEMORY_WT            0x0000000000000004ULL
#define EFI_MEMORY_WB            0x0000000000000008ULL
#define EFI_MEMORY_UCE           0x0000000000000010ULL
#define EFI_MEMORY_WP            0x0000000000001000ULL
#define EFI_MEMORY_RP            0x0000000000002000ULL
#define EFI_MEMORY_XP            0x0000000000004000ULL
#define EFI_MEMORY_NV            0x0000000000008000ULL
#define EFI_MEMORY_MORE_RELIABLE 0x0000000000010000ULL
#define EFI_MEMORY_RO            0x0000000000020000ULL
#define EFI_MEMORY_SP            0x0000000000040000ULL
#define EFI_MEMORY_CPU_CRYPTO    0x0000000000080000ULL
#define EFI_MEMORY_RUNTIME       0x8000000000000000ULL

#define EFI_MEMORY_DESCRIPTOR_VERSION 1

/*============================================================================
 * Table Header
 *============================================================================*/

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/*============================================================================
 * Simple Text Input Protocol
 *============================================================================*/

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

/** Keystroke data */
typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(
    IN struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    IN BOOLEAN                                  ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    IN struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
    OUT EFI_INPUT_KEY                         *Key
);

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET     Reset;
    EFI_INPUT_READ_KEY  ReadKeyStroke;
    EFI_EVENT           WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

/*============================================================================
 * Simple Text Output Protocol
 *============================================================================*/

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    IN struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN                                  ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    IN struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CHAR16                                  *String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
    IN struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN                                    Attribute
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    IN struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(
    IN struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN                                    Column,
    IN UINTN                                    Row
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(
    IN struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN                                  Visible
);

typedef struct {
    INT32 MaxMode;
    INT32 Mode;
    INT32 Attribute;
    INT32 CursorColumn;
    INT32 CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET              Reset;
    EFI_TEXT_STRING             OutputString;
    VOID*                       TestString;
    VOID*                       QueryMode;
    VOID*                       SetMode;
    EFI_TEXT_SET_ATTRIBUTE      SetAttribute;
    EFI_TEXT_CLEAR_SCREEN       ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR      EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE     *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/*============================================================================
 * Graphics Output Protocol
 *============================================================================*/

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                             MaxMode;
    UINT32                             Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                              SizeOfInfo;
    EFI_PHYSICAL_ADDRESS               FrameBufferBase;
    UINTN                              FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    IN  struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN  UINT32                                ModeNumber,
    OUT UINTN                                *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    IN struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN UINT32                                ModeNumber
);

typedef struct {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef enum {
    EfiBltVideoFill,
    EfiBltVideoToBltBuffer,
    EfiBltBufferToVideo,
    EfiBltVideoToVideo,
    EfiGraphicsOutputBltOperationMax
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
    IN struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *BltBuffer OPTIONAL,
    IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION    BltOperation,
    IN UINTN                                 SourceX,
    IN UINTN                                 SourceY,
    IN UINTN                                 DestinationX,
    IN UINTN                                 DestinationY,
    IN UINTN                                 Width,
    IN UINTN                                 Height,
    IN UINTN                                 Delta OPTIONAL
);

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT        Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE       *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/*============================================================================
 * File Protocol
 *============================================================================*/

struct _EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    IN struct _EFI_FILE_PROTOCOL *This,
    OUT struct _EFI_FILE_PROTOCOL **NewHandle,
    IN CHAR16                     *FileName,
    IN UINT64                     OpenMode,
    IN UINT64                     Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    IN struct _EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    IN struct _EFI_FILE_PROTOCOL *This,
    IN OUT UINTN                 *BufferSize,
    OUT VOID                     *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    IN struct _EFI_FILE_PROTOCOL *This,
    IN EFI_GUID                  *InformationType,
    IN OUT UINTN                 *BufferSize,
    OUT VOID                     *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(
    IN struct _EFI_FILE_PROTOCOL *This,
    IN UINT64                     Position
);

typedef struct _EFI_FILE_PROTOCOL {
    UINT64                  Revision;
    EFI_FILE_OPEN           Open;
    EFI_FILE_CLOSE          Close;
    VOID*                   Delete;
    EFI_FILE_READ           Read;
    VOID*                   Write;
    VOID*                   GetPosition;
    EFI_FILE_SET_POSITION   SetPosition;
    EFI_FILE_GET_INFO       GetInfo;
    VOID*                   SetInfo;
    VOID*                   Flush;
    VOID*                   OpenEx;
    VOID*                   ReadEx;
    VOID*                   WriteEx;
    VOID*                   FlushEx;
} EFI_FILE_PROTOCOL;

typedef struct {
    UINT64         Size;
    UINT64         FileSize;
    UINT64         PhysicalSize;
    VOID*          CreateTime;
    VOID*          LastAccessTime;
    VOID*          ModificationTime;
    UINT64         Attribute;
    CHAR16         FileName[1];
} EFI_FILE_INFO;

#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE   0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE  0x8000000000000000ULL

/*============================================================================
 * Simple File System Protocol
 *============================================================================*/

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    IN struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    OUT EFI_FILE_PROTOCOL                      **Root
);

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64                                      Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/*============================================================================
 * Loaded Image Protocol
 *============================================================================*/

typedef struct {
    UINT32                  Revision;
    EFI_HANDLE              ParentHandle;
    VOID*                   SystemTable;
    EFI_HANDLE              DeviceHandle;
    VOID*                   FilePath;
    VOID*                   Reserved;
    UINT32                  LoadOptionsSize;
    VOID*                   LoadOptions;
    VOID*                   ImageBase;
    UINT64                  ImageSize;
    EFI_MEMORY_TYPE         ImageCodeType;
    EFI_MEMORY_TYPE         ImageDataType;
    VOID*                   Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/*============================================================================
 * Configuration Table
 *============================================================================*/

typedef struct {
    EFI_GUID  VendorGuid;
    VOID     *VendorTable;
} EFI_CONFIGURATION_TABLE;

/*============================================================================
 * Boot Services
 *============================================================================*/

typedef VOID (EFIAPI *EFI_EVENT_NOTIFY)(
    IN EFI_EVENT Event,
    IN VOID      *Context
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    IN EFI_ALLOCATE_TYPE         Type,
    IN EFI_MEMORY_TYPE           MemoryType,
    IN UINTN                     Pages,
    IN OUT EFI_PHYSICAL_ADDRESS *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    IN EFI_PHYSICAL_ADDRESS Memory,
    IN UINTN                Pages
);

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    IN OUT UINTN               *MemoryMapSize,
    IN OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
    OUT UINTN                  *MapKey,
    OUT UINTN                  *DescriptorSize,
    OUT UINT32                 *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    IN EFI_MEMORY_TYPE PoolType,
    IN UINTN           Size,
    OUT VOID          **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(
    IN VOID *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT)(
    IN UINT32            Type,
    IN EFI_TPL           NotifyTpl,
    IN EFI_EVENT_NOTIFY  NotifyFunction OPTIONAL,
    IN VOID             *NotifyContext OPTIONAL,
    OUT EFI_EVENT       *Event
);

typedef EFI_STATUS (EFIAPI *EFI_SET_TIMER)(
    IN EFI_EVENT Event,
    IN UINTN     Type,
    IN UINT64    TriggerTime
);

typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(
    IN UINTN      NumberOfEvents,
    IN EFI_EVENT *Event,
    OUT UINTN    *Index
);

typedef EFI_STATUS (EFIAPI *EFI_CLOSE_EVENT)(
    IN EFI_EVENT Event
);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    IN EFI_HANDLE Handle,
    IN EFI_GUID  *Protocol,
    OUT VOID    **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    IN EFI_GUID *Protocol,
    IN VOID     *Registration OPTIONAL,
    OUT VOID   **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    IN EFI_HANDLE ImageHandle,
    IN UINTN      MapKey
);

typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(
    IN UINTN   Timeout,
    IN UINT64  WatchdogCode,
    IN UINTN   DataSize,
    IN CHAR16 *WatchdogData OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_STALL)(
    IN UINTN Microseconds
);

typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    VOID*                            RaiseTPL;
    VOID*                            RestoreTPL;
    EFI_ALLOCATE_PAGES               AllocatePages;
    EFI_FREE_PAGES                   FreePages;
    EFI_GET_MEMORY_MAP               GetMemoryMap;
    EFI_ALLOCATE_POOL                AllocatePool;
    EFI_FREE_POOL                    FreePool;
    EFI_CREATE_EVENT                 CreateEvent;
    EFI_SET_TIMER                    SetTimer;
    EFI_WAIT_FOR_EVENT               WaitForEvent;
    VOID*                            SignalEvent;
    EFI_CLOSE_EVENT                  CloseEvent;
    VOID*                            CheckEvent;
    VOID*                            InstallProtocolInterface;
    VOID*                            ReinstallProtocolInterface;
    VOID*                            UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL              HandleProtocol;
    VOID*                            Reserved;
    VOID*                            RegisterProtocolNotify;
    VOID*                            LocateHandle;
    VOID*                            LocateDevicePath;
    VOID*                            InstallConfigurationTable;
    VOID*                            LoadImage;
    VOID*                            StartImage;
    VOID*                            Exit;
    VOID*                            UnloadImage;
    EFI_EXIT_BOOT_SERVICES           ExitBootServices;
    VOID*                            GetNextMonotonicCount;
    EFI_STALL                        Stall;
    EFI_SET_WATCHDOG_TIMER           SetWatchdogTimer;
    VOID*                            ConnectController;
    VOID*                            DisconnectController;
    VOID*                            OpenProtocol;
    VOID*                            CloseProtocol;
    VOID*                            OpenProtocolInformation;
    VOID*                            ProtocolsPerHandle;
    VOID*                            LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL              LocateProtocol;
    VOID*                            InstallMultipleProtocolInterfaces;
    VOID*                            UninstallMultipleProtocolInterfaces;
    VOID*                            CalculateCrc32;
    VOID*                            CopyMem;
    VOID*                            SetMem;
    VOID*                            CreateEventEx;
} EFI_BOOT_SERVICES;

/*============================================================================
 * Runtime Services
 *============================================================================*/

typedef struct {
    UINT16 Year;
    UINT8  Month;
    UINT8  Day;
    UINT8  Hour;
    UINT8  Minute;
    UINT8  Second;
    UINT8  Pad1;
    UINT32 Nanosecond;
    INT16  TimeZone;
    UINT8  Daylight;
    UINT8  Pad2;
} EFI_TIME;

typedef struct {
    UINT32 Resolution;
    UINT32 Accuracy;
    BOOLEAN SetsToZero;
} EFI_TIME_CAPABILITIES;

typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(
    OUT EFI_TIME              *Time,
    OUT EFI_TIME_CAPABILITIES *Capabilities OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_SET_VIRTUAL_ADDRESS_MAP)(
    IN UINTN                 MemoryMapSize,
    IN UINTN                 DescriptorSize,
    IN UINT32                DescriptorVersion,
    IN EFI_MEMORY_DESCRIPTOR *VirtualMap
);

typedef struct {
    EFI_TABLE_HEADER             Hdr;
    EFI_GET_TIME                 GetTime;
    VOID*                        SetTime;
    VOID*                        GetWakeupTime;
    VOID*                        SetWakeupTime;
    EFI_SET_VIRTUAL_ADDRESS_MAP  SetVirtualAddressMap;
    VOID*                        ConvertPointer;
    VOID*                        GetVariable;
    VOID*                        GetNextVariableName;
    VOID*                        SetVariable;
    VOID*                        GetNextHighMonotonicCount;
    VOID*                        ResetSystem;
    VOID*                        UpdateCapsule;
    VOID*                        QueryCapsuleCapabilities;
    VOID*                        QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

/*============================================================================
 * System Table
 *============================================================================*/

#define EFI_SYSTEM_TABLE_SIGNATURE     0x5453595320494249ULL
#define EFI_2_90_SYSTEM_TABLE_REVISION ((2 << 16) | (90))
#define EFI_2_80_SYSTEM_TABLE_REVISION ((2 << 16) | (80))
#define EFI_2_70_SYSTEM_TABLE_REVISION ((2 << 16) | (70))
#define EFI_SYSTEM_TABLE_REVISION      EFI_2_70_SYSTEM_TABLE_REVISION

typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16                          *FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    UINTN                            NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE         *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/*============================================================================
 * GUID Comparison Utility
 *============================================================================*/

static inline BOOLEAN efi_guid_equal(const EFI_GUID* a, const EFI_GUID* b) {
    if (a->Data1 != b->Data1) return FALSE;
    if (a->Data2 != b->Data2) return FALSE;
    if (a->Data3 != b->Data3) return FALSE;
    for (int i = 0; i < 8; i++) {
        if (a->Data4[i] != b->Data4[i]) return FALSE;
    }
    return TRUE;
}

#endif /* SERAPH_UEFI_TYPES_H */
