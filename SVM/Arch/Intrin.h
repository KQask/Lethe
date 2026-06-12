#pragma once

// Assembly-implemented intrinsics (Entrypoint.asm)
extern "C" unsigned short __read_cs(void);
extern "C" unsigned short __read_ss(void);
extern "C" unsigned short __read_ds(void);
extern "C" unsigned short __read_es(void);
extern "C" unsigned short __read_fs(void);
extern "C" unsigned short __read_gs(void);
extern "C" unsigned short __read_ldtr(void);
extern "C" unsigned short __read_tr(void);
extern "C" uint64_t       __load_ar(unsigned short selector);
extern "C" void           _sgdt(void* target);
extern "C" void           _sidt(void* target);
extern "C" void           vmexit_entrypoint(void);            // #VMEXIT handler loop

// Returns true from the guest side on success (Launch.asm).
extern "C" bool svm_launch(uint64_t vmcb_physical, uint64_t* rsp_target, uint64_t host_stack_top, uint64_t host_save_area_pa);

// Set VMCB.save.rip to this label so the guest starts here on the first VMRUN.
extern "C" void guest_launched(void);

#define SELECTOR_RPL_MASK 0xFFF8    // masks out RPL and TI bits

union __segment_selector_t
{
    struct
    {
        uint16_t rpl   : 2;   // requested privilege level (0 = kernel)
        uint16_t table : 1;   // 0 = GDT, 1 = LDT
        uint16_t index : 13;  // index into GDT/LDT
    } bits;
    uint16_t flags;
};

#pragma pack(push, 1)

// Standard 16-byte system descriptor (TSS, call gate, etc.)
struct __segment_descriptor_64_t
{
    uint16_t limit_low;
    uint16_t base_low;
    union
    {
        struct
        {
            uint32_t base_middle    : 8;
            uint32_t type           : 4;
            uint32_t descriptor_type: 1;  // 0 = system, 1 = code/data
            uint32_t dpl            : 2;
            uint32_t present        : 1;
            uint32_t limit_high     : 4;
            uint32_t avl            : 1;
            uint32_t long_mode      : 1;
            uint32_t default_big    : 1;
            uint32_t granularity    : 1;
            uint32_t base_high      : 8;
        };
        uint32_t flags;
    };
    uint32_t base_upper;
    uint32_t reserved;
};

// Standard 8-byte code/data descriptor
struct __segment_descriptor_32_t
{
    uint16_t limit_low;
    uint16_t base_low;
    union
    {
        struct
        {
            uint32_t base_middle    : 8;
            uint32_t type           : 4;
            uint32_t descriptor_type: 1;
            uint32_t dpl            : 2;
            uint32_t present        : 1;
            uint32_t limit_high     : 4;
            uint32_t avl            : 1;
            uint32_t long_mode      : 1;
            uint32_t default_big    : 1;
            uint32_t granularity    : 1;
            uint32_t base_high      : 8;
        };
        uint32_t flags;
    };
};

// GDTR/IDTR pseudo-descriptor
struct __pseudo_descriptor_64_t
{
    uint16_t limit;
    uint64_t base;
};

#pragma pack(pop)

// VMCB attrib: 12-bit packed. bits 7:0 = low byte of GDT flags (type/S/DPL/P),
// bits 11:8 = high nibble (AVL/L/DB/G).
union __segment_access_rights_t
{
    struct
    {
        uint32_t type           : 4;    // segment type (code/data/system)
        uint32_t descriptor_type: 1;    // 0 = system, 1 = code or data
        uint32_t dpl            : 2;    // descriptor privilege level
        uint32_t present        : 1;    // segment present
        uint32_t _r0            : 4;    // reserved
        uint32_t avl            : 1;    // available to OS
        uint32_t long_mode      : 1;    // 64-bit code segment
        uint32_t default_big    : 1;    // 0 = 16-bit, 1 = 32-bit default op size
        uint32_t granularity    : 1;    // 0 = byte granularity, 1 = 4 KB granularity
        uint32_t unusable       : 1;    // set if selector is null/not present
        uint32_t _r1            : 15;
    } bits;
    uint32_t flags;
};

#define SEGMENT_DESCRIPTOR_TYPE_TSS_AVAILABLE   0x9
#define SEGMENT_DESCRIPTOR_TYPE_TSS_BUSY        0xB
