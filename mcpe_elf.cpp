#include "mcpe_elf.h"

#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/kernel/threadmgr/mutex.h>
#include <psp2/kernel/threadmgr/cond.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/touch.h>
#include <psp2/ctrl.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/audioout.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <wchar.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" long long __aeabi_ldivmod(long long, long long);
extern "C" unsigned long long __aeabi_uldivmod(
    unsigned long long, unsigned long long);

static volatile uint32_t g_mcpe_diag_budget_5AW = 24u * 1024u;

static void mcpe_diag_reset_budget_5AW(uint32_t bytes)
{
    __sync_lock_test_and_set(&g_mcpe_diag_budget_5AW, bytes);
}

static int mcpe_diag_vprintf_5AW(const char *format, va_list args)
{
    if (!format)
        return -1;

    char output[1024];
    va_list copy;
    va_copy(copy, args);
    const int wanted = vsnprintf(output, sizeof(output), format, copy);
    va_end(copy);

    if (wanted <= 0)
        return wanted;

    uint32_t produced = static_cast<uint32_t>(wanted);
    if (produced >= sizeof(output))
        produced = sizeof(output) - 1u;

    uint32_t observed = 0;
    uint32_t allowed = 0;
    for (;;) {
        observed = g_mcpe_diag_budget_5AW;
        if (observed == 0)
            return wanted;

        allowed = produced < observed ? produced : observed;
        if (__sync_bool_compare_and_swap(
                &g_mcpe_diag_budget_5AW,
                observed,
                observed - allowed))
            break;
    }

    const int written = sceIoWrite(1, output, allowed);
    return written < 0 ? written : wanted;
}

static int mcpe_diag_printf_5AW(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = mcpe_diag_vprintf_5AW(format, args);
    va_end(args);
    return result;
}

#define printf(...) mcpe_diag_printf_5AW(__VA_ARGS__)

static void mcpe_hardware_log_5CK(const char *format, ...)
{
    if (!format)
        return;

    sceIoMkdir("ux0:data/mcpe", 0777);
    const SceUID fd = sceIoOpen(
        "ux0:data/mcpe/hardware_boot.log",
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0)
        return;

    char line[384];
    va_list args;
    va_start(args, format);
    const int wanted = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (wanted > 0) {
        const size_t length = static_cast<size_t>(wanted) < sizeof(line)
            ? static_cast<size_t>(wanted) : sizeof(line) - 1u;
        (void)sceIoWrite(fd, line, length);
        (void)sceIoSyncByFd(fd, 0);
    }
    (void)sceIoClose(fd);
}

static void mcpe_hardware_log_reset_5CK(void)
{
    sceIoMkdir("ux0:data/mcpe", 0777);
    const SceUID fd = sceIoOpen(
        "ux0:data/mcpe/hardware_boot.log",
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0)
        (void)sceIoClose(fd);
}

static bool mcpe_running_on_vita3k_5CK(void)
{
    
}

static void mcpe_cap_hardware_render_distance_5CI2(void)
{
    if (mcpe_running_on_vita3k_5CK())
        return;

    static const char options_path[] =
        "ux0:data/mcpe/games/com.mojang/minecraftpe/options.txt";
    static const char option_key[] = "gfx_renderdistance_new:";

    const SceUID fd = sceIoOpen(options_path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=options_missing result=0x%08X\n",
            static_cast<unsigned int>(fd));
        return;
    }

    const SceOff file_size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (file_size <= 0 || file_size >= 4096 ||
        sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
        (void)sceIoClose(fd);
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=invalid_size size=%lld\n",
            static_cast<long long>(file_size));
        return;
    }

    char input[4096];
    const int bytes_read = sceIoRead(
        fd, input, static_cast<unsigned int>(file_size));
    (void)sceIoClose(fd);
    if (bytes_read != static_cast<int>(file_size)) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=read_failed got=%d expected=%lld\n",
            bytes_read, static_cast<long long>(file_size));
        return;
    }
    input[bytes_read] = '\0';

    char *value_begin = strstr(input, option_key);
    if (!value_begin) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=key_missing\n");
        return;
    }
    value_begin += sizeof(option_key) - 1u;

    char *value_end = value_begin;
    int current_value = 0;
    while (*value_end >= '0' && *value_end <= '9') {
        current_value = current_value * 10 + (*value_end - '0');
        ++value_end;
    }

    if (value_end == value_begin || current_value == hardware_cap) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP old=%d new=%d changed=0\n",
            current_value, current_value);
        return;
    }

    char output[4096];
    const size_t prefix_length =
        static_cast<size_t>(value_begin - input);
    const int cap_length = snprintf(
        output + prefix_length,
        sizeof(output) - prefix_length,
        "%d", hardware_cap);
    if (cap_length <= 0) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=format_failed\n");
        return;
    }

    memcpy(output, input, prefix_length);
    const size_t suffix_length =
        static_cast<size_t>((input + bytes_read) - value_end);
    const size_t output_size =
        prefix_length + static_cast<size_t>(cap_length) + suffix_length;
    if (output_size >= sizeof(output)) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=output_too_large\n");
        return;
    }
    memcpy(output + prefix_length + static_cast<size_t>(cap_length),
           value_end, suffix_length);

    const SceUID output_fd = sceIoOpen(
        options_path,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    if (output_fd < 0) {
        mcpe_hardware_log_5CK(
            "RENDER_DISTANCE_CAP status=open_write_failed result=0x%08X\n",
            static_cast<unsigned int>(output_fd));
        return;
    }
    const int bytes_written = sceIoWrite(
        output_fd, output, static_cast<unsigned int>(output_size));
    (void)sceIoClose(output_fd);
    mcpe_hardware_log_5CK(
        "RENDER_DISTANCE_CAP old=%d new=%d changed=%u written=%d expected=%u\n",
        current_value, hardware_cap,
        bytes_written == static_cast<int>(output_size) ? 1u : 0u,
        bytes_written, static_cast<unsigned int>(output_size));
}

struct McpeFakeJniObject5AN {
    uint32_t magic;
};

static McpeFakeJniObject5AN g_mcpe_fake_jni_object_5AN = {
    0x4A4E494Fu
};

static uintptr_t g_mcpe_fake_jni_table_5AN[256];
static uintptr_t g_mcpe_fake_jni_env_object_5AN[1];
static uintptr_t g_mcpe_fake_jvm_table_5AN[8];
static uintptr_t g_mcpe_fake_jvm_object_5AN[1];

static void *mcpe_fake_jni_object_5AN()
{
    return static_cast<void *>(&g_mcpe_fake_jni_object_5AN);
}

static uintptr_t mcpe_fake_jni_generic_5AN(...)
{
    return reinterpret_cast<uintptr_t>(mcpe_fake_jni_object_5AN());
}

static uintptr_t mcpe_fake_jni_bool_5AN(...)
{
    return 0;
}

static uintptr_t mcpe_fake_jni_int_5AN(...)
{
    return 0;
}

static uintptr_t mcpe_fake_jni_ptr_5AN(...)
{
    return reinterpret_cast<uintptr_t>(mcpe_fake_jni_object_5AN());
}

static const char g_mcpe_fake_jni_utf_path_5AX[] = "ux0:data/mcpe";
static volatile uint32_t g_mcpe_fake_jni_utf_calls_5AX = 0;

static int mcpe_fake_jni_get_string_utf_length_5AX(
    void *env,
    void *string_object)
{
    (void)env;
    (void)string_object;
    return static_cast<int>(sizeof(g_mcpe_fake_jni_utf_path_5AX) - 1u);
}

static const char *mcpe_fake_jni_get_string_utf_chars_5AX(
    void *env,
    void *string_object,
    unsigned char *is_copy)
{
    (void)env;
    (void)string_object;
    if (is_copy)
        *is_copy = 0;

    const uint32_t call =
        __sync_add_and_fetch(&g_mcpe_fake_jni_utf_calls_5AX, 1u);
    if (call <= 4u) {
        printf("[MCPE-STAGE5AX] JNI_GET_STRING_UTF_CHARS call=%u path=%s\n",
               call, g_mcpe_fake_jni_utf_path_5AX);
    }

    return g_mcpe_fake_jni_utf_path_5AX;
}

static void mcpe_fake_jni_release_string_utf_chars_5AX(
    void *env,
    void *string_object,
    const char *chars)
{
    (void)env;
    (void)string_object;
    (void)chars;
}

static int mcpe_fake_jvm_attach_current_thread_5AN(
    void *vm, void **penv, void *args)
{
    (void)vm;
    (void)args;
    if (penv)
        *penv = static_cast<void *>(g_mcpe_fake_jni_env_object_5AN);
    return 0;
}

static int mcpe_fake_jvm_detach_current_thread_5AN(void *vm)
{
    (void)vm;
    return 0;
}

static int mcpe_fake_jvm_get_env_5AN(
    void *vm, void **penv, int version)
{
    (void)vm;
    (void)version;
    if (penv)
        *penv = static_cast<void *>(g_mcpe_fake_jni_env_object_5AN);
    return 0;
}

static int mcpe_fake_jvm_destroy_5AN(void *vm)
{
    (void)vm;
    return 0;
}

static void mcpe_fake_jni_init_5AN()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    for (size_t i = 0; i < 256; ++i)
        g_mcpe_fake_jni_table_5AN[i] =
            reinterpret_cast<uintptr_t>(&mcpe_fake_jni_generic_5AN);

    g_mcpe_fake_jni_table_5AN[6]   =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[31]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[33]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[35]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[38]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_bool_5AN);
    g_mcpe_fake_jni_table_5AN[50]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_int_5AN);
    g_mcpe_fake_jni_table_5AN[53]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_int_5AN);
    g_mcpe_fake_jni_table_5AN[56]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_int_5AN);
    g_mcpe_fake_jni_table_5AN[62]  =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_generic_5AN);
    g_mcpe_fake_jni_table_5AN[113] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[114] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[115] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_ptr_5AN);
    g_mcpe_fake_jni_table_5AN[142] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jni_generic_5AN);

    g_mcpe_fake_jni_table_5AN[168] =
        reinterpret_cast<uintptr_t>(
            &mcpe_fake_jni_get_string_utf_length_5AX);
    g_mcpe_fake_jni_table_5AN[169] =
        reinterpret_cast<uintptr_t>(
            &mcpe_fake_jni_get_string_utf_chars_5AX);
    g_mcpe_fake_jni_table_5AN[170] =
        reinterpret_cast<uintptr_t>(
            &mcpe_fake_jni_release_string_utf_chars_5AX);

    g_mcpe_fake_jni_env_object_5AN[0] =
        reinterpret_cast<uintptr_t>(g_mcpe_fake_jni_table_5AN);

    for (size_t i = 0; i < 8; ++i)
        g_mcpe_fake_jvm_table_5AN[i] =
            reinterpret_cast<uintptr_t>(&mcpe_fake_jvm_destroy_5AN);

    g_mcpe_fake_jvm_table_5AN[3] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jvm_destroy_5AN);
    g_mcpe_fake_jvm_table_5AN[4] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jvm_attach_current_thread_5AN);
    g_mcpe_fake_jvm_table_5AN[5] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jvm_detach_current_thread_5AN);
    g_mcpe_fake_jvm_table_5AN[6] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jvm_get_env_5AN);
    g_mcpe_fake_jvm_table_5AN[7] =
        reinterpret_cast<uintptr_t>(&mcpe_fake_jvm_attach_current_thread_5AN);

    g_mcpe_fake_jvm_object_5AN[0] =
        reinterpret_cast<uintptr_t>(g_mcpe_fake_jvm_table_5AN);
}

static void *mcpe_fake_jvm_5AN()
{
    mcpe_fake_jni_init_5AN();
    return static_cast<void *>(g_mcpe_fake_jvm_object_5AN);
}

#ifndef MCPE_VITA_IOVEC_DEFINED
#define MCPE_VITA_IOVEC_DEFINED
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif

typedef unsigned long mcpe_wctype_t;

#include <sys/timeb.h>
#include <zlib.h>
#include <png.h>
#include <psp2/net/net.h>

#define MCPE_KU_PROT_READ  0x40u
#define MCPE_KU_PROT_WRITE 0x20u
#define MCPE_KU_PROT_EXEC  0x10u
extern "C" int kuKernelMemProtect(
    void *addr, SceSize len, SceUInt32 prot);
extern "C" void kuKernelFlushCaches(
    const void *ptr, SceSize len);

#ifdef MCPE_USE_VITAGL
#include <vitaGL.h>
#endif

#if defined(__arm__)
#define MCPE_GUEST_SOFTFP __attribute__((pcs("aapcs")))
#else
#define MCPE_GUEST_SOFTFP
#endif

namespace {

static const uint8_t ELFCLASS32 = 1;
static const uint8_t ELFDATA2LSB = 1;

static const uint16_t ET_DYN = 3;
static const uint16_t EM_ARM = 40;

static const uint32_t PT_LOAD = 1;
static const uint32_t PT_DYNAMIC = 2;

static const int32_t DT_NULL = 0;
static const int32_t DT_NEEDED = 1;
static const int32_t DT_HASH = 4;
static const int32_t DT_STRTAB = 5;
static const int32_t DT_SYMTAB = 6;
static const int32_t DT_STRSZ = 10;
static const int32_t DT_SYMENT = 11;
static const int32_t DT_INIT = 12;
static const int32_t DT_FINI = 13;
static const int32_t DT_REL = 17;
static const int32_t DT_RELSZ = 18;
static const int32_t DT_RELENT = 19;
static const int32_t DT_JMPREL = 23;
static const int32_t DT_PLTRELSZ = 2;
static const int32_t DT_INIT_ARRAY = 25;
static const int32_t DT_FINI_ARRAY = 26;
static const int32_t DT_FINI_ARRAYSZ = 28;
static const int32_t DT_PREINIT_ARRAY = 32;
static const int32_t DT_INIT_ARRAYSZ = 27;

static const uint32_t R_ARM_ABS32 = 2;
static const uint32_t R_ARM_GLOB_DAT = 21;
static const uint32_t R_ARM_JUMP_SLOT = 22;
static const uint32_t R_ARM_RELATIVE = 23;

static const uint32_t SHN_UNDEF = 0;
static const uint8_t STB_LOCAL = 0;
static const uint8_t STT_FUNC = 2;
static const uint32_t PAGE_SIZE = 0x1000;

#ifndef PROT_READ
static const int MCPE_PROT_READ = 0x1;
static const int MCPE_PROT_WRITE = 0x2;
static const int MCPE_PROT_EXEC = 0x4;
static const int MCPE_PROT_NONE = 0x0;
#else
static const int MCPE_PROT_READ = PROT_READ;
static const int MCPE_PROT_WRITE = PROT_WRITE;
static const int MCPE_PROT_EXEC = PROT_EXEC;
static const int MCPE_PROT_NONE = PROT_NONE;
#endif

static const int MCPE_MAP_SHARED = 0x01;
static const int MCPE_MAP_PRIVATE = 0x02;
static const int MCPE_MAP_FIXED = 0x10;
static const int MCPE_MAP_ANONYMOUS = 0x20;
static const int MCPE_MAP_ANON = MCPE_MAP_ANONYMOUS;

static const uintptr_t MCPE_MAP_FAILED_VALUE = static_cast<uintptr_t>(-1);

#pragma pack(push, 1)

struct Elf32_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

struct Elf32_Dyn {
    int32_t d_tag;
    uint32_t d_val;
};

struct Elf32_Rel {
    uint32_t r_offset;
    uint32_t r_info;
};

struct Elf32_Sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
};

#pragma pack(pop)

static uint32_t align_down(uint32_t v, uint32_t a)
{
    return v & ~(a - 1);
}

static uint32_t align_up(uint32_t v, uint32_t a)
{
    if (v > UINT32_MAX - (a - 1))
        return UINT32_MAX;
    return (v + a - 1) & ~(a - 1);
}

static uint32_t sym_bind(uint8_t info)
{
    return static_cast<uint32_t>(info >> 4);
}

static uint32_t sym_type(uint8_t info)
{
    return static_cast<uint32_t>(info & 0x0Fu);
}

static uint32_t sym_index(uint32_t info)
{
    return info >> 8;
}

static bool starts_with(const char *name, const char *prefix)
{
    if (!name || !prefix)
        return false;

    const size_t n = strlen(prefix);
    return strncmp(name, prefix, n) == 0;
}

static const char *safe_string(
    const char *strtab,
    uint32_t strsz,
    uint32_t off)
{
    if (!strtab || off >= strsz)
        return "";

    return strtab + off;
}

static bool read_exact(
    SceUID fd,
    void *dst,
    uint32_t size,
    uint32_t offset)
{
    if (sceIoLseek(fd, offset, SCE_SEEK_SET) < 0)
        return false;

    uint8_t *p = static_cast<uint8_t *>(dst);
    uint32_t done = 0;

    while (done < size) {
        int n = sceIoRead(fd, p + done, size - done);
        if (n <= 0)
            return false;
        done += static_cast<uint32_t>(n);
    }

    return true;
}

static void __attribute__((naked, noinline))
mcpe_kuser_memory_barrier_5BU()
{
    __asm__ volatile(
        "dmb sy\n"
        "bx lr\n");
}

struct McpeGuestHeapBlock5BV {
    uint32_t magic;
    uint32_t total_size;
    uint32_t previous_size;
    uint32_t flags;
    uint32_t next_free;
    uint32_t previous_free;
};

static const uint32_t MCPE_GUEST_HEAP_MAGIC_5BV = 0x4D434850u;
static const uint32_t MCPE_GUEST_HEAP_USED_5BV = 1u;
static const size_t MCPE_GUEST_HEAP_ALIGNMENT_5BV = 8u;
static const size_t MCPE_GUEST_HEAP_MIN_PAYLOAD_5BV = 8u;

static SceUID g_mcpe_guest_heap_memblock_5AR = -1;
static uint8_t *g_mcpe_guest_heap_base_5AR = NULL;
static size_t g_mcpe_guest_heap_size_5AR = 0;
static size_t g_mcpe_guest_heap_used_5AR = 0;
static size_t g_mcpe_guest_heap_peak_5BV = 0;
static uint32_t g_mcpe_guest_heap_failures_5BV = 0u;
static McpeGuestHeapBlock5BV *g_mcpe_guest_free_head_5BW = NULL;
static volatile int g_mcpe_guest_heap_lock_5AR = 0;

static void mcpe_guest_heap_lock_5AR()
{
    while (__sync_lock_test_and_set(&g_mcpe_guest_heap_lock_5AR, 1))
        __asm__ volatile("yield");
    __sync_synchronize();
}

static void mcpe_guest_heap_unlock_5AR()
{
    __sync_synchronize();
    __sync_lock_release(&g_mcpe_guest_heap_lock_5AR);
}

static bool mcpe_guest_heap_init_5AR()
{
    if (g_mcpe_guest_heap_base_5AR)
        return true;

    static const uint32_t emulator_heap_candidates_mib[] = {
        128u, 120u, 112u, 104u, 96u
    };
    static const uint32_t hardware_heap_candidates_mib[] = {
        96u, 88u, 80u, 72u, 68u, 64u, 56u, 48u
    };
    const bool emulator = mcpe_running_on_vita3k_5CK();
    const uint32_t *heap_candidates_mib = emulator
        ? emulator_heap_candidates_mib : hardware_heap_candidates_mib;
    const uint32_t heap_candidate_count = emulator
        ? static_cast<uint32_t>(sizeof(emulator_heap_candidates_mib) /
                                sizeof(emulator_heap_candidates_mib[0]))
        : static_cast<uint32_t>(sizeof(hardware_heap_candidates_mib) /
                                sizeof(hardware_heap_candidates_mib[0]));

    SceKernelFreeMemorySizeInfo free_memory;
    memset(&free_memory, 0, sizeof(free_memory));
    free_memory.size = sizeof(free_memory);
    const int free_memory_result = sceKernelGetFreeMemorySize(&free_memory);
    mcpe_hardware_log_5CK(
        "HEAP_BEGIN host=%s free_result=0x%08X user=%d cdram=%d phycont=%d\n",
        emulator ? "VITA3K" : "VITA",
        static_cast<unsigned int>(free_memory_result),
        free_memory.size_user, free_memory.size_cdram, free_memory.size_phycont);

    size_t heap_size = 0u;
    int last_error = -1;
    for (uint32_t i = 0u; i < heap_candidate_count; ++i) {
        const size_t candidate =
            static_cast<size_t>(heap_candidates_mib[i]) * 1024u * 1024u;
        if (!emulator && free_memory_result >= 0 &&
            free_memory.size_user > 0 &&
            candidate + 12u * 1024u * 1024u >
                static_cast<size_t>(free_memory.size_user)) {
            mcpe_hardware_log_5CK(
                "HEAP_SKIP size_mib=%u reason=graphics_reserve\n",
                heap_candidates_mib[i]);
            continue;
        }
        g_mcpe_guest_heap_memblock_5AR = sceKernelAllocMemBlock(
            "mcpeHeap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
            static_cast<int>(candidate), NULL);
        if (g_mcpe_guest_heap_memblock_5AR >= 0) {
            heap_size = candidate;
            break;
        }
        last_error = static_cast<int>(g_mcpe_guest_heap_memblock_5AR);
        printf("[MCPE-STAGE5AR] GUEST_HEAP_RETRY size_mib=%u uid=0x%08X\n",
               heap_candidates_mib[i],
               static_cast<unsigned int>(last_error));
    }

    if (g_mcpe_guest_heap_memblock_5AR < 0 || heap_size == 0u) {
        printf("[MCPE-STAGE5AR] GUEST_HEAP_ALLOC_FAILED uid=0x%08X\n",
               static_cast<unsigned int>(last_error));
        return false;
    }

    void *base = NULL;
    const int ret =
        sceKernelGetMemBlockBase(
            g_mcpe_guest_heap_memblock_5AR,
            &base);

    if (ret < 0 || !base) {
        printf("[MCPE-STAGE5AR] GUEST_HEAP_BASE_FAILED ret=0x%08X\n",
               static_cast<unsigned int>(ret));
        return false;
    }

    g_mcpe_guest_heap_base_5AR =
        static_cast<uint8_t *>(base);
    g_mcpe_guest_heap_size_5AR = heap_size;
    g_mcpe_guest_heap_used_5AR = 0;
    g_mcpe_guest_heap_peak_5BV = 0;

    McpeGuestHeapBlock5BV *first =
        reinterpret_cast<McpeGuestHeapBlock5BV *>(
            g_mcpe_guest_heap_base_5AR);
    first->magic = MCPE_GUEST_HEAP_MAGIC_5BV;
    first->total_size = static_cast<uint32_t>(heap_size);
    first->previous_size = 0u;
    first->flags = 0u;
    first->next_free = 0u;
    first->previous_free = 0u;
    g_mcpe_guest_free_head_5BW = first;

    printf("[MCPE-STAGE5BW] GUEST_HEAP_READY base=%p size=%u reusable=YES free_list=YES\n",
           static_cast<void *>(g_mcpe_guest_heap_base_5AR),
           static_cast<unsigned int>(g_mcpe_guest_heap_size_5AR));
    mcpe_hardware_log_5CK(
        "HEAP_READY base=%p size_mib=%u\n",
        static_cast<void *>(g_mcpe_guest_heap_base_5AR),
        static_cast<unsigned int>(g_mcpe_guest_heap_size_5AR /
                                  (1024u * 1024u)));

    return true;
}

static uint32_t mcpe_guest_heap_block_address_5BW(
    const McpeGuestHeapBlock5BV *block)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
}

static McpeGuestHeapBlock5BV *mcpe_guest_heap_block_pointer_5BW(
    uint32_t address)
{
    return address
        ? reinterpret_cast<McpeGuestHeapBlock5BV *>(
              static_cast<uintptr_t>(address))
        : NULL;
}

static void mcpe_guest_heap_remove_free_5BW(
    McpeGuestHeapBlock5BV *block)
{
    McpeGuestHeapBlock5BV *previous =
        mcpe_guest_heap_block_pointer_5BW(block->previous_free);
    McpeGuestHeapBlock5BV *next =
        mcpe_guest_heap_block_pointer_5BW(block->next_free);

    if (previous)
        previous->next_free = block->next_free;
    else
        g_mcpe_guest_free_head_5BW = next;
    if (next)
        next->previous_free = block->previous_free;

    block->next_free = 0u;
    block->previous_free = 0u;
}

static void mcpe_guest_heap_insert_free_5BW(
    McpeGuestHeapBlock5BV *block)
{
    block->flags = 0u;
    block->previous_free = 0u;
    block->next_free =
        mcpe_guest_heap_block_address_5BW(g_mcpe_guest_free_head_5BW);
    if (g_mcpe_guest_free_head_5BW)
        g_mcpe_guest_free_head_5BW->previous_free =
            mcpe_guest_heap_block_address_5BW(block);
    g_mcpe_guest_free_head_5BW = block;
}

static bool mcpe_guest_heap_block_valid_5BV(
    const McpeGuestHeapBlock5BV *block)
{
    if (!block || !g_mcpe_guest_heap_base_5AR)
        return false;

    const uintptr_t heap_begin = reinterpret_cast<uintptr_t>(
        g_mcpe_guest_heap_base_5AR);
    const uintptr_t heap_end = heap_begin + g_mcpe_guest_heap_size_5AR;
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);

    if (address < heap_begin ||
        address > heap_end - sizeof(McpeGuestHeapBlock5BV) ||
        block->magic != MCPE_GUEST_HEAP_MAGIC_5BV ||
        block->total_size < sizeof(McpeGuestHeapBlock5BV) ||
        (block->total_size & (MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u)) != 0u ||
        block->total_size > heap_end - address)
        return false;

    return true;
}

static McpeGuestHeapBlock5BV *mcpe_guest_heap_next_5BV(
    McpeGuestHeapBlock5BV *block)
{
    if (!mcpe_guest_heap_block_valid_5BV(block))
        return NULL;

    uint8_t *next_address =
        reinterpret_cast<uint8_t *>(block) + block->total_size;
    uint8_t *heap_end =
        g_mcpe_guest_heap_base_5AR + g_mcpe_guest_heap_size_5AR;
    if (next_address == heap_end)
        return NULL;
    if (next_address > heap_end - sizeof(McpeGuestHeapBlock5BV))
        return NULL;

    McpeGuestHeapBlock5BV *next =
        reinterpret_cast<McpeGuestHeapBlock5BV *>(next_address);
    return mcpe_guest_heap_block_valid_5BV(next) ? next : NULL;
}

static void mcpe_guest_heap_update_following_5BV(
    McpeGuestHeapBlock5BV *block)
{
    McpeGuestHeapBlock5BV *following = mcpe_guest_heap_next_5BV(block);
    if (following)
        following->previous_size = block->total_size;
}

static McpeGuestHeapBlock5BV *mcpe_guest_heap_coalesce_5BV(
    McpeGuestHeapBlock5BV *block)
{
    McpeGuestHeapBlock5BV *next = mcpe_guest_heap_next_5BV(block);
    if (next && !(next->flags & MCPE_GUEST_HEAP_USED_5BV)) {
        mcpe_guest_heap_remove_free_5BW(next);
        block->total_size += next->total_size;
        mcpe_guest_heap_update_following_5BV(block);
    }

    if (block->previous_size >= sizeof(McpeGuestHeapBlock5BV)) {
        const uintptr_t heap_begin = reinterpret_cast<uintptr_t>(
            g_mcpe_guest_heap_base_5AR);
        const uintptr_t address = reinterpret_cast<uintptr_t>(block);
        if (block->previous_size <= address - heap_begin) {
            McpeGuestHeapBlock5BV *previous =
                reinterpret_cast<McpeGuestHeapBlock5BV *>(
                    address - block->previous_size);
            if (mcpe_guest_heap_block_valid_5BV(previous) &&
                !(previous->flags & MCPE_GUEST_HEAP_USED_5BV) &&
                previous->total_size == block->previous_size) {
                mcpe_guest_heap_remove_free_5BW(previous);
                previous->total_size += block->total_size;
                mcpe_guest_heap_update_following_5BV(previous);
                block = previous;
            }
        }
    }

    mcpe_guest_heap_insert_free_5BW(block);
    return block;
}

static void *mcpe_guest_heap_alloc_5AR(size_t size)
{
    if (size == 0)
        size = 1;

    if (!mcpe_guest_heap_init_5AR())
        return NULL;

    if (size > static_cast<size_t>(0xFFFFFFFFu) -
               sizeof(McpeGuestHeapBlock5BV) -
               (MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u))
        return NULL;

    const size_t payload_size =
        (size + (MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u)) &
        ~(MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u);
    const size_t required_size =
        sizeof(McpeGuestHeapBlock5BV) + payload_size;

    mcpe_guest_heap_lock_5AR();

    size_t largest_free = 0u;
    McpeGuestHeapBlock5BV *block = g_mcpe_guest_free_head_5BW;
    while (block) {
        if (!mcpe_guest_heap_block_valid_5BV(block)) {
            printf("[MCPE-STAGE5BW] FREE_LIST_CORRUPT block=%p\n",
                   static_cast<void *>(block));
            break;
        }

        McpeGuestHeapBlock5BV *next_free =
            mcpe_guest_heap_block_pointer_5BW(block->next_free);
        if (block->total_size > largest_free)
            largest_free = block->total_size;
        if (block->total_size >= required_size) {
            mcpe_guest_heap_remove_free_5BW(block);
            const size_t remaining = block->total_size - required_size;
            if (remaining >= sizeof(McpeGuestHeapBlock5BV) +
                             MCPE_GUEST_HEAP_MIN_PAYLOAD_5BV) {
                block->total_size = static_cast<uint32_t>(required_size);
                McpeGuestHeapBlock5BV *split =
                    reinterpret_cast<McpeGuestHeapBlock5BV *>(
                        reinterpret_cast<uint8_t *>(block) + required_size);
                split->magic = MCPE_GUEST_HEAP_MAGIC_5BV;
                split->total_size = static_cast<uint32_t>(remaining);
                split->previous_size = block->total_size;
                split->flags = 0u;
                split->next_free = 0u;
                split->previous_free = 0u;
                mcpe_guest_heap_update_following_5BV(split);
                mcpe_guest_heap_insert_free_5BW(split);
            }

            block->flags = MCPE_GUEST_HEAP_USED_5BV;
            block->next_free = 0u;
            block->previous_free = 0u;
            g_mcpe_guest_heap_used_5AR += block->total_size;
            if (g_mcpe_guest_heap_used_5AR > g_mcpe_guest_heap_peak_5BV)
                g_mcpe_guest_heap_peak_5BV = g_mcpe_guest_heap_used_5AR;

            void *result = static_cast<void *>(block + 1);
            mcpe_guest_heap_unlock_5AR();
            return result;
        }

        block = next_free;
    }

    ++g_mcpe_guest_heap_failures_5BV;
    printf("[MCPE-STAGE5BV] HEAP_EXHAUSTED request=%u used=%u peak=%u total=%u largest=%u failures=%u\n",
           static_cast<unsigned int>(size),
           static_cast<unsigned int>(g_mcpe_guest_heap_used_5AR),
           static_cast<unsigned int>(g_mcpe_guest_heap_peak_5BV),
           static_cast<unsigned int>(g_mcpe_guest_heap_size_5AR),
           static_cast<unsigned int>(largest_free),
           g_mcpe_guest_heap_failures_5BV);

    char failure_line[256];
    const int failure_length = snprintf(
        failure_line, sizeof(failure_line),
        "request=%u used=%u peak=%u total=%u largest=%u failures=%u\n",
        static_cast<unsigned int>(size),
        static_cast<unsigned int>(g_mcpe_guest_heap_used_5AR),
        static_cast<unsigned int>(g_mcpe_guest_heap_peak_5BV),
        static_cast<unsigned int>(g_mcpe_guest_heap_size_5AR),
        static_cast<unsigned int>(largest_free),
        g_mcpe_guest_heap_failures_5BV);
    if (failure_length > 0) {
        const SceUID failure_fd = sceIoOpen(
            "ux0:data/mcpe/heap_failure.log",
            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
            0666);
        if (failure_fd >= 0) {
            const size_t bytes =
                static_cast<size_t>(failure_length) < sizeof(failure_line)
                    ? static_cast<size_t>(failure_length)
                    : sizeof(failure_line) - 1u;
            (void)sceIoWrite(failure_fd, failure_line, bytes);
            (void)sceIoClose(failure_fd);
        }
    }
    mcpe_guest_heap_unlock_5AR();
    return NULL;
}

static size_t mcpe_guest_heap_size_5AR(const void *ptr)
{
    if (!ptr)
        return 0;

    mcpe_guest_heap_lock_5AR();
    McpeGuestHeapBlock5BV *block =
        static_cast<McpeGuestHeapBlock5BV *>(
            const_cast<void *>(ptr)) - 1;
    const size_t size =
        mcpe_guest_heap_block_valid_5BV(block) &&
        (block->flags & MCPE_GUEST_HEAP_USED_5BV)
            ? block->total_size - sizeof(McpeGuestHeapBlock5BV)
            : 0u;
    mcpe_guest_heap_unlock_5AR();
    return size;
}

static uint8_t *g_mcpe_ninecraft_app_5BB = NULL;
static uint32_t g_mcpe_fallback_uv_5BC[8] = {0};

static bool mcpe_gameplay_active_5CJ(void)
{
    if (!g_mcpe_ninecraft_app_5BB)
        return false;
    uint32_t level = 0u;
    uint32_t screen = 0u;
    memcpy(&level, g_mcpe_ninecraft_app_5BB + 3212u, sizeof(level));
    memcpy(&screen, g_mcpe_ninecraft_app_5BB + 3228u, sizeof(screen));
    return level != 0u && screen == 0u;
}

static void *mcpe_texture_atlas_uv_guard_5BC(
    const void *texture_item, int index)
{
    if (!texture_item)
        return g_mcpe_fallback_uv_5BC;

    const uint32_t *fields = static_cast<const uint32_t *>(texture_item);
    const uint32_t begin = fields[1];
    const uint32_t end = fields[2];
    uint32_t count = fields[4];
    if (begin == 0u || end <= begin || ((end - begin) & 31u) != 0u)
        return g_mcpe_fallback_uv_5BC;

    const uint32_t vector_count = (end - begin) / 32u;
    if (count == 0u || count > vector_count)
        count = vector_count;
    if (count == 0u)
        return g_mcpe_fallback_uv_5BC;

    const uint32_t selected =
        index > 0 && static_cast<uint32_t>(index) < count
            ? static_cast<uint32_t>(index)
            : 0u;
    return reinterpret_cast<void *>(
        static_cast<uintptr_t>(begin + selected * 32u));
}

static void *vita_malloc(size_t size)
{
    void *ptr = mcpe_guest_heap_alloc_5AR(size);
    if (size == 3456u && ptr && !g_mcpe_ninecraft_app_5BB) {
        g_mcpe_ninecraft_app_5BB = static_cast<uint8_t *>(ptr);
        printf("[MCPE-STAGE5BB] NINECRAFT_APP_CAPTURED=%p size=%u\n",
               ptr, static_cast<unsigned>(size));
    }
    return ptr;
}

static void vita_free(void *ptr)
{
    if (!ptr || !g_mcpe_guest_heap_base_5AR)
        return;

    mcpe_guest_heap_lock_5AR();
    McpeGuestHeapBlock5BV *block =
        static_cast<McpeGuestHeapBlock5BV *>(ptr) - 1;
    if (!mcpe_guest_heap_block_valid_5BV(block) ||
        !(block->flags & MCPE_GUEST_HEAP_USED_5BV)) {
        mcpe_guest_heap_unlock_5AR();
        return;
    }

    const size_t released = block->total_size;
    block->flags = 0u;
    block->next_free = 0u;
    block->previous_free = 0u;
    if (released <= g_mcpe_guest_heap_used_5AR)
        g_mcpe_guest_heap_used_5AR -= released;
    else
        g_mcpe_guest_heap_used_5AR = 0u;
    mcpe_guest_heap_coalesce_5BV(block);
    mcpe_guest_heap_unlock_5AR();
}

static void *vita_calloc(size_t n, size_t s)
{
    if (n == 0 || s == 0)
        return mcpe_guest_heap_alloc_5AR(1);

    if (n > (static_cast<size_t>(-1) / s))
        return NULL;

    const size_t total = n * s;
    void *p = mcpe_guest_heap_alloc_5AR(total);
    if (p)
        memset(p, 0, total);
    return p;
}

static void *vita_realloc(void *p, size_t s)
{
    if (!p)
        return mcpe_guest_heap_alloc_5AR(s);

    if (s == 0) {
        vita_free(p);
        return NULL;
    }

    if (s > static_cast<size_t>(0xFFFFFFFFu) -
            sizeof(McpeGuestHeapBlock5BV) -
            (MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u))
        return NULL;

    const size_t payload_size =
        (s + (MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u)) &
        ~(MCPE_GUEST_HEAP_ALIGNMENT_5BV - 1u);
    const size_t required_size =
        sizeof(McpeGuestHeapBlock5BV) + payload_size;

    mcpe_guest_heap_lock_5AR();
    McpeGuestHeapBlock5BV *block =
        static_cast<McpeGuestHeapBlock5BV *>(p) - 1;
    if (!mcpe_guest_heap_block_valid_5BV(block) ||
        !(block->flags & MCPE_GUEST_HEAP_USED_5BV)) {
        mcpe_guest_heap_unlock_5AR();
        return NULL;
    }

    const size_t old_total = block->total_size;
    const size_t old_size = old_total - sizeof(McpeGuestHeapBlock5BV);

    if (required_size <= old_total) {
        const size_t remaining = old_total - required_size;
        if (remaining >= sizeof(McpeGuestHeapBlock5BV) +
                         MCPE_GUEST_HEAP_MIN_PAYLOAD_5BV) {
            block->total_size = static_cast<uint32_t>(required_size);
            McpeGuestHeapBlock5BV *split =
                reinterpret_cast<McpeGuestHeapBlock5BV *>(
                    reinterpret_cast<uint8_t *>(block) + required_size);
            split->magic = MCPE_GUEST_HEAP_MAGIC_5BV;
            split->total_size = static_cast<uint32_t>(remaining);
            split->previous_size = block->total_size;
            split->flags = 0u;
            split->next_free = 0u;
            split->previous_free = 0u;
            mcpe_guest_heap_update_following_5BV(split);
            mcpe_guest_heap_coalesce_5BV(split);
            g_mcpe_guest_heap_used_5AR -= remaining;
        }
        mcpe_guest_heap_unlock_5AR();
        return p;
    }

    McpeGuestHeapBlock5BV *next = mcpe_guest_heap_next_5BV(block);
    if (next && !(next->flags & MCPE_GUEST_HEAP_USED_5BV) &&
        old_total + next->total_size >= required_size) {
        mcpe_guest_heap_remove_free_5BW(next);
        const size_t combined = old_total + next->total_size;
        const size_t remaining = combined - required_size;
        size_t new_total = combined;
        if (remaining >= sizeof(McpeGuestHeapBlock5BV) +
                         MCPE_GUEST_HEAP_MIN_PAYLOAD_5BV) {
            new_total = required_size;
            McpeGuestHeapBlock5BV *split =
                reinterpret_cast<McpeGuestHeapBlock5BV *>(
                    reinterpret_cast<uint8_t *>(block) + required_size);
            split->magic = MCPE_GUEST_HEAP_MAGIC_5BV;
            split->total_size = static_cast<uint32_t>(remaining);
            split->previous_size = static_cast<uint32_t>(new_total);
            split->flags = 0u;
            split->next_free = 0u;
            split->previous_free = 0u;
            mcpe_guest_heap_update_following_5BV(split);
            mcpe_guest_heap_insert_free_5BW(split);
        }
        block->total_size = static_cast<uint32_t>(new_total);
        mcpe_guest_heap_update_following_5BV(block);
        g_mcpe_guest_heap_used_5AR += new_total - old_total;
        if (g_mcpe_guest_heap_used_5AR > g_mcpe_guest_heap_peak_5BV)
            g_mcpe_guest_heap_peak_5BV = g_mcpe_guest_heap_used_5AR;
        mcpe_guest_heap_unlock_5AR();
        return p;
    }

    mcpe_guest_heap_unlock_5AR();

    void *new_ptr = mcpe_guest_heap_alloc_5AR(s);
    if (!new_ptr)
        return NULL;

    const size_t copy_size = (old_size < s) ? old_size : s;
    memcpy(new_ptr, p, copy_size);
    vita_free(p);

    return new_ptr;
}

static void mcpe_assign_guest_string_5BA(
    void *string_object, const char *text, size_t length)
{
    if (!string_object || !text)
        return;
    if (length > 1023u)
        length = 1023u;

    const size_t header_size = 3u * sizeof(uint32_t);
    uint8_t *rep = static_cast<uint8_t *>(
        mcpe_guest_heap_alloc_5AR(header_size + length + 1u));
    if (!rep)
        return;

    uint32_t *header = reinterpret_cast<uint32_t *>(rep);
    header[0] = static_cast<uint32_t>(length);
    header[1] = static_cast<uint32_t>(length);
    header[2] = 0u;
    char *characters = reinterpret_cast<char *>(rep + header_size);
    memcpy(characters, text, length);
    characters[length] = '\0';
    *static_cast<char **>(string_object) = characters;
    printf("[MCPE-STAGE5BA] PATH_STRING object=%p value=%s length=%u\n",
           string_object, characters, static_cast<unsigned>(length));
}

static const size_t MCPE_IME_TEXT_CAPACITY_5BR = 256u;
static SceWChar16 g_mcpe_ime_initial_5BR[MCPE_IME_TEXT_CAPACITY_5BR];
static SceWChar16 g_mcpe_ime_input_5BR[MCPE_IME_TEXT_CAPACITY_5BR];
static const SceWChar16 g_mcpe_ime_title_5BR[] = {
    'M', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', 0
};
static bool g_mcpe_ime_active_5BR = false;
static bool g_mcpe_ime_aborting_5BR = false;
static bool g_mcpe_ime_runtime_ready_5BR = false;
static uint8_t *g_mcpe_ime_platform_5BR = NULL;
static uint64_t g_mcpe_ime_shown_at_5BR = 0u;

static bool mcpe_ime_is_active_5BR(void)
{
    return g_mcpe_ime_active_5BR;
}

static size_t mcpe_utf8_to_utf16_5BR(
    const char *source, size_t source_length,
    SceWChar16 *output, size_t capacity)
{
    if (!output || capacity == 0u)
        return 0u;

    size_t written = 0u;
    const unsigned char *cursor =
        reinterpret_cast<const unsigned char *>(source ? source : "");
    size_t remaining = source ? source_length : 0u;
    while (remaining > 0u && *cursor && written + 1u < capacity) {
        uint32_t codepoint = 0xfffdu;
        size_t consumed = 1u;
        if (cursor[0] < 0x80u) {
            codepoint = cursor[0];
        } else if (remaining >= 2u &&
                   (cursor[0] & 0xe0u) == 0xc0u &&
                   (cursor[1] & 0xc0u) == 0x80u) {
            codepoint = ((cursor[0] & 0x1fu) << 6u) |
                        (cursor[1] & 0x3fu);
            consumed = 2u;
            if (codepoint < 0x80u)
                codepoint = 0xfffdu;
        } else if (remaining >= 3u &&
                   (cursor[0] & 0xf0u) == 0xe0u &&
                   (cursor[1] & 0xc0u) == 0x80u &&
                   (cursor[2] & 0xc0u) == 0x80u) {
            codepoint = ((cursor[0] & 0x0fu) << 12u) |
                        ((cursor[1] & 0x3fu) << 6u) |
                        (cursor[2] & 0x3fu);
            consumed = 3u;
            if (codepoint < 0x800u ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu))
                codepoint = 0xfffdu;
        } else if (remaining >= 4u &&
                   (cursor[0] & 0xf8u) == 0xf0u &&
                   (cursor[1] & 0xc0u) == 0x80u &&
                   (cursor[2] & 0xc0u) == 0x80u &&
                   (cursor[3] & 0xc0u) == 0x80u) {
            codepoint = ((cursor[0] & 0x07u) << 18u) |
                        ((cursor[1] & 0x3fu) << 12u) |
                        ((cursor[2] & 0x3fu) << 6u) |
                        (cursor[3] & 0x3fu);
            consumed = 4u;
            if (codepoint < 0x10000u || codepoint > 0x10ffffu)
                codepoint = 0xfffdu;
        }

        cursor += consumed;
        remaining -= consumed;
        if (codepoint <= 0xffffu) {
            output[written++] = static_cast<SceWChar16>(codepoint);
        } else if (written + 2u < capacity) {
            codepoint -= 0x10000u;
            output[written++] = static_cast<SceWChar16>(
                0xd800u | (codepoint >> 10u));
            output[written++] = static_cast<SceWChar16>(
                0xdc00u | (codepoint & 0x3ffu));
        } else {
            break;
        }
    }
    output[written] = 0;
    return written;
}

static size_t mcpe_utf16_to_utf8_5BR(
    const SceWChar16 *source, char *output, size_t capacity)
{
    if (!output || capacity == 0u)
        return 0u;

    size_t written = 0u;
    size_t index = 0u;
    while (source && source[index] != 0) {
        uint32_t codepoint = source[index++];
        if (codepoint >= 0xd800u && codepoint <= 0xdbffu && source[index]) {
            const uint32_t low = source[index];
            if (low >= 0xdc00u && low <= 0xdfffu) {
                ++index;
                codepoint = 0x10000u +
                    ((codepoint - 0xd800u) << 10u) + (low - 0xdc00u);
            } else {
                codepoint = 0xfffdu;
            }
        } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
            codepoint = 0xfffdu;
        }

        unsigned char encoded[4];
        size_t encoded_size = 0u;
        if (codepoint < 0x80u) {
            encoded[0] = static_cast<unsigned char>(codepoint);
            encoded_size = 1u;
        } else if (codepoint < 0x800u) {
            encoded[0] = static_cast<unsigned char>(0xc0u | (codepoint >> 6u));
            encoded[1] = static_cast<unsigned char>(0x80u | (codepoint & 0x3fu));
            encoded_size = 2u;
        } else if (codepoint < 0x10000u) {
            encoded[0] = static_cast<unsigned char>(0xe0u | (codepoint >> 12u));
            encoded[1] = static_cast<unsigned char>(0x80u | ((codepoint >> 6u) & 0x3fu));
            encoded[2] = static_cast<unsigned char>(0x80u | (codepoint & 0x3fu));
            encoded_size = 3u;
        } else {
            encoded[0] = static_cast<unsigned char>(0xf0u | (codepoint >> 18u));
            encoded[1] = static_cast<unsigned char>(0x80u | ((codepoint >> 12u) & 0x3fu));
            encoded[2] = static_cast<unsigned char>(0x80u | ((codepoint >> 6u) & 0x3fu));
            encoded[3] = static_cast<unsigned char>(0x80u | (codepoint & 0x3fu));
            encoded_size = 4u;
        }
        if (written + encoded_size + 1u > capacity)
            break;
        memcpy(output + written, encoded, encoded_size);
        written += encoded_size;
    }
    output[written] = '\0';
    return written;
}

static void mcpe_ime_set_platform_visible_5BR(bool visible)
{
    if (g_mcpe_ime_platform_5BR)
        g_mcpe_ime_platform_5BR[4u] = visible ? 1u : 0u;
}

static bool mcpe_ime_prepare_runtime_5BR(void)
{
    if (g_mcpe_ime_runtime_ready_5BR)
        return true;

    SceAppUtilInitParam app_util;
    SceAppUtilBootParam boot;
    memset(&app_util, 0, sizeof(app_util));
    memset(&boot, 0, sizeof(boot));
    const int app_result = sceAppUtilInit(&app_util, &boot);

    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    const int dialog_result = sceCommonDialogSetConfigParam(&config);

    g_mcpe_ime_runtime_ready_5BR = true;
    printf("[MCPE-STAGE5BR] IME_RUNTIME app=0x%08X dialog=0x%08X\n",
           static_cast<uint32_t>(app_result),
           static_cast<uint32_t>(dialog_result));
    return true;
}

static void mcpe_feed_textbox_5BR(const char *text, size_t length)
{
    if (!g_mcpe_ninecraft_app_5BB || !text)
        return;

    const uint32_t vtable =
        *reinterpret_cast<const uint32_t *>(g_mcpe_ninecraft_app_5BB);
    if (vtable < 0x1000u)
        return;

    const uint32_t target = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(vtable) + 64u);
    if (target < 0x1001u)
        return;

    uint32_t guest_string = 0u;
    mcpe_assign_guest_string_5BA(&guest_string, text, length);
    typedef void (*McpeSetTextboxText)(void *, const void *);
    reinterpret_cast<McpeSetTextboxText>(
        static_cast<uintptr_t>(target))(
            g_mcpe_ninecraft_app_5BB, &guest_string);
    printf("[MCPE-STAGE5BR] TEXT_COMMITTED bytes=%u target=0x%08X\n",
           static_cast<unsigned>(length), target);
}

static void mcpe_show_keyboard_5BR(
    void *platform, const void *guest_text, int keyboard_type, bool multiline)
{
    (void)keyboard_type;
    g_mcpe_ime_platform_5BR = static_cast<uint8_t *>(platform);
    if (g_mcpe_ime_active_5BR) {
        mcpe_ime_set_platform_visible_5BR(true);
        return;
    }

    const char *initial = "";
    if (guest_text) {
        const char *candidate =
            *static_cast<char *const *>(guest_text);
        if (candidate)
            initial = candidate;
    }
    const size_t initial_length = strnlen(initial, 1023u);
    mcpe_utf8_to_utf16_5BR(
        initial, initial_length,
        g_mcpe_ime_initial_5BR, MCPE_IME_TEXT_CAPACITY_5BR);
    memcpy(g_mcpe_ime_input_5BR, g_mcpe_ime_initial_5BR,
           sizeof(g_mcpe_ime_input_5BR));

    mcpe_ime_prepare_runtime_5BR();
    SceImeDialogParam parameters;
    sceImeDialogParamInit(&parameters);
    parameters.supportedLanguages =
        SCE_IME_LANGUAGE_ENGLISH | SCE_IME_LANGUAGE_ITALIAN;
    parameters.languagesForced = SCE_FALSE;
    parameters.type = SCE_IME_TYPE_BASIC_LATIN;
    parameters.option = multiline
        ? static_cast<uint32_t>(SCE_IME_OPTION_MULTILINE) : 0u;
    parameters.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    parameters.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    parameters.title = g_mcpe_ime_title_5BR;
    parameters.maxTextLength = MCPE_IME_TEXT_CAPACITY_5BR - 1u;
    parameters.initialText = g_mcpe_ime_initial_5BR;
    parameters.inputTextBuffer = g_mcpe_ime_input_5BR;

    const int result = sceImeDialogInit(&parameters);
    if (result >= 0) {
        g_mcpe_ime_active_5BR = true;
        g_mcpe_ime_aborting_5BR = false;
        g_mcpe_ime_shown_at_5BR = sceKernelGetProcessTimeWide();
        mcpe_ime_set_platform_visible_5BR(true);
    } else {
        mcpe_ime_set_platform_visible_5BR(false);
    }
    printf("[MCPE-STAGE5BR] IME_SHOW result=0x%08X active=%u multiline=%u\n",
           static_cast<uint32_t>(result),
           g_mcpe_ime_active_5BR ? 1u : 0u,
           multiline ? 1u : 0u);
    mcpe_hardware_log_5CK(
        "IME_SHOW result=0x%08X active=%u multiline=%u\n",
        static_cast<uint32_t>(result),
        g_mcpe_ime_active_5BR ? 1u : 0u,
        multiline ? 1u : 0u);
}

static void mcpe_hide_keyboard_5BR(void *platform)
{
    if (platform)
        g_mcpe_ime_platform_5BR = static_cast<uint8_t *>(platform);

    if (g_mcpe_ime_active_5BR && !g_mcpe_ime_aborting_5BR) {
        const uint64_t now = sceKernelGetProcessTimeWide();
        if (now >= g_mcpe_ime_shown_at_5BR &&
            now - g_mcpe_ime_shown_at_5BR < 500000u) {
            mcpe_ime_set_platform_visible_5BR(true);
            mcpe_hardware_log_5CK(
                "IME_HIDE_DEFERRED age_us=%u\n",
                static_cast<unsigned int>(
                    now - g_mcpe_ime_shown_at_5BR));
            return;
        }
    }

    mcpe_ime_set_platform_visible_5BR(false);
    if (g_mcpe_ime_active_5BR && !g_mcpe_ime_aborting_5BR) {
        sceImeDialogAbort();
        g_mcpe_ime_aborting_5BR = true;
    }
}

static void mcpe_update_textbox_text_5BR(
    void *platform, const void *guest_text)
{
    (void)guest_text;
    if (platform)
        g_mcpe_ime_platform_5BR = static_cast<uint8_t *>(platform);
}

static void mcpe_textbox_set_focus_5BR(void *textbox, void *minecraft)
{
    if (!textbox || !minecraft)
        return;

    uint8_t *box = static_cast<uint8_t *>(textbox);
    if (box[68u] != 0u)
        return;

    void *platform = NULL;
    const uint32_t minecraft_vtable =
        *reinterpret_cast<const uint32_t *>(minecraft);
    if (minecraft_vtable >= 0x1000u) {
        const uint32_t get_platform =
            *reinterpret_cast<const uint32_t *>(
                static_cast<uintptr_t>(minecraft_vtable) + 28u);
        if (get_platform >= 0x1001u) {
            typedef void *(*McpeGetPlatform5BR)(void *);
            platform = reinterpret_cast<McpeGetPlatform5BR>(
                static_cast<uintptr_t>(get_platform))(minecraft);
        }
    }

    int keyboard_type = 0;
    uint32_t valid_characters = 0u;
    memcpy(&keyboard_type, box + 72u, sizeof(keyboard_type));
    memcpy(&valid_characters, box + 76u, sizeof(valid_characters));
    mcpe_hardware_log_5CK(
        "TEXTBOX_FOCUS textbox=%p platform=%p type=%d filtered=%u\n",
        textbox, platform, keyboard_type,
        valid_characters != 0u ? 1u : 0u);
    mcpe_show_keyboard_5BR(
        platform, box + 60u, keyboard_type, valid_characters != 0u);
    box[68u] = 1u;
}

static bool mcpe_ime_update_5BR(void)
{
    if (!g_mcpe_ime_active_5BR)
        return false;

    const SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return true;

    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    const int get_result = sceImeDialogGetResult(&result);
    const bool commit = get_result >= 0 &&
        !g_mcpe_ime_aborting_5BR &&
        result.button == SCE_IME_DIALOG_BUTTON_ENTER;

    char utf8[1024];
    size_t utf8_length = 0u;
    if (commit) {
        utf8_length = mcpe_utf16_to_utf8_5BR(
            g_mcpe_ime_input_5BR, utf8, sizeof(utf8));
    }

    sceImeDialogTerm();
    g_mcpe_ime_active_5BR = false;
    g_mcpe_ime_aborting_5BR = false;
    g_mcpe_ime_shown_at_5BR = 0u;
    mcpe_ime_set_platform_visible_5BR(false);
    if (commit)
        mcpe_feed_textbox_5BR(utf8, utf8_length);

    printf("[MCPE-STAGE5BR] IME_FINISH result=0x%08X button=%d commit=%u\n",
           static_cast<uint32_t>(get_result), result.button,
           commit ? 1u : 0u);
    mcpe_hardware_log_5CK(
        "IME_FINISH result=0x%08X button=%d commit=%u bytes=%u\n",
        static_cast<uint32_t>(get_result), result.button,
        commit ? 1u : 0u, static_cast<unsigned int>(utf8_length));
    return false;
}

static uint8_t *mcpe_assign_guest_bytes_5BL(
    void *string_object, size_t length)
{
    if (!string_object || length == 0u || length > 16u * 1024u * 1024u)
        return NULL;

    const size_t header_size = 3u * sizeof(uint32_t);
    if (length > static_cast<size_t>(-1) - header_size - 1u)
        return NULL;

    uint8_t *rep = static_cast<uint8_t *>(
        mcpe_guest_heap_alloc_5AR(header_size + length + 1u));
    if (!rep)
        return NULL;

    uint32_t *header = reinterpret_cast<uint32_t *>(rep);
    header[0] = static_cast<uint32_t>(length);
    header[1] = static_cast<uint32_t>(length);
    header[2] = 0u;

    uint8_t *bytes = rep + header_size;
    bytes[length] = 0u;
    *static_cast<uint8_t **>(string_object) = bytes;
    return bytes;
}

static volatile uint32_t g_mcpe_png_calls_5BL = 0;

static void mcpe_load_png_5BL(
    void *platform, void *image_data, const void *guest_path_string)
{
    (void)platform;

    if (!image_data || !guest_path_string)
        return;

    uint32_t *fields = static_cast<uint32_t *>(image_data);
    fields[0] = 0u;
    fields[1] = 0u;
    fields[3] = 0u;
    fields[4] = 0u;

    const char *requested =
        *static_cast<char *const *>(guest_path_string);
    if (!requested || !requested[0]) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=EMPTY_PATH\n");
        return;
    }

    while (*requested == '/')
        ++requested;
    if (strncmp(requested, "data/assets/", 12u) == 0)
        requested += 12u;
    else if (strncmp(requested, "assets/", 7u) == 0)
        requested += 7u;

    if (!requested[0] || strstr(requested, "..") || strchr(requested, ':')) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=INVALID_PATH path=%s\n",
               requested);
        return;
    }

    char filename[512];
    const int filename_length =
        snprintf(filename, sizeof(filename),
                 "app0:data/assets/%s", requested);
    if (filename_length <= 0 ||
        static_cast<size_t>(filename_length) >= sizeof(filename)) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=PATH_TOO_LONG path=%s\n",
               requested);
        return;
    }

    if (strcmp(requested, "images/mob/wolf_collar.png") == 0) {
        const uint32_t fallback_width = 64u;
        const uint32_t fallback_height = 32u;
        const size_t fallback_bytes = fallback_width * fallback_height * 4u;
        uint8_t *pixels = mcpe_assign_guest_bytes_5BL(
            fields + 2u, fallback_bytes);
        if (!pixels) {
            printf("[MCPE-STAGE5BL] PNG_FAIL reason=OUT_OF_GUEST_MEMORY path=%s bytes=%u\n",
                   requested, static_cast<unsigned>(fallback_bytes));
            return;
        }
        memset(pixels, 0, fallback_bytes);
        fields[0] = fallback_width;
        fields[1] = fallback_height;
        fields[3] = 0u;
        printf("[MCPE-STAGE5BL] PNG_FALLBACK path=%s w=%u h=%u transparent=YES\n",
               requested, fallback_width, fallback_height);
        return;
    }

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, filename)) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=OPEN path=%s error=%s\n",
               requested, image.message);
        png_image_free(&image);
        return;
    }

    image.format = PNG_FORMAT_RGBA;
    const size_t byte_count = PNG_IMAGE_SIZE(image);
    if (image.width == 0u || image.height == 0u ||
        byte_count == 0u || byte_count > 16u * 1024u * 1024u) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=BAD_SIZE path=%s w=%u h=%u bytes=%u\n",
               requested, image.width, image.height,
               static_cast<unsigned>(byte_count));
        png_image_free(&image);
        return;
    }

    uint8_t *pixels = mcpe_assign_guest_bytes_5BL(fields + 2u, byte_count);
    if (!pixels) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=OUT_OF_GUEST_MEMORY path=%s bytes=%u\n",
               requested, static_cast<unsigned>(byte_count));
        png_image_free(&image);
        return;
    }

    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        printf("[MCPE-STAGE5BL] PNG_FAIL reason=DECODE path=%s error=%s\n",
               requested, image.message);
        png_image_free(&image);
        return;
    }

    fields[0] = image.width;
    fields[1] = image.height;
    fields[3] = 0u;
    const uint32_t call =
        __sync_add_and_fetch(&g_mcpe_png_calls_5BL, 1u);
    if (call <= 64u) {
        printf("[MCPE-STAGE5BL] PNG_OK call=%u path=%s w=%u h=%u bytes=%u\n",
               call, requested, image.width, image.height,
               static_cast<unsigned>(byte_count));
    }
    png_image_free(&image);
}

static volatile uint32_t g_mcpe_tga_calls_5BN = 0;

static void mcpe_load_tga_5BN(
    void *platform, void *image_data, const void *guest_path_string)
{
    (void)platform;

    if (!image_data || !guest_path_string)
        return;

    uint32_t *fields = static_cast<uint32_t *>(image_data);
    fields[0] = 0u;
    fields[1] = 0u;
    fields[3] = 0u;

    const char *requested =
        *static_cast<char *const *>(guest_path_string);
    if (!requested || !requested[0]) {
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=EMPTY_PATH\n");
        return;
    }

    while (*requested == '/')
        ++requested;
    if (strncmp(requested, "data/assets/", 12u) == 0)
        requested += 12u;
    else if (strncmp(requested, "assets/", 7u) == 0)
        requested += 7u;

    if (!requested[0] || strstr(requested, "..") || strchr(requested, ':')) {
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=INVALID_PATH path=%s\n",
               requested);
        return;
    }

    char filename[512];
    const int filename_length = snprintf(
        filename, sizeof(filename), "app0:data/assets/%s", requested);
    if (filename_length <= 0 ||
        static_cast<size_t>(filename_length) >= sizeof(filename)) {
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=PATH_TOO_LONG path=%s\n",
               requested);
        return;
    }

    const SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (fd < 0) {
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=OPEN path=%s result=0x%08X\n",
               requested, static_cast<unsigned>(fd));
        return;
    }

    uint8_t header[18];
    if (!read_exact(fd, header, sizeof(header), 0u)) {
        sceIoClose(fd);
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=HEADER path=%s\n",
               requested);
        return;
    }

    const uint32_t image_type = header[2];
    const uint32_t width =
        static_cast<uint32_t>(header[12]) |
        (static_cast<uint32_t>(header[13]) << 8u);
    const uint32_t height =
        static_cast<uint32_t>(header[14]) |
        (static_cast<uint32_t>(header[15]) << 8u);
    const uint32_t bits_per_pixel = header[16];
    const uint32_t source_channels = bits_per_pixel / 8u;
    const uint32_t data_offset = 18u + header[0];
    const bool top_origin = (header[17] & 0x20u) != 0u;
    const bool right_origin = (header[17] & 0x10u) != 0u;

    if (header[1] != 0u || image_type != 2u ||
        (source_channels != 3u && source_channels != 4u) ||
        width == 0u || height == 0u || width > 4096u || height > 4096u) {
        sceIoClose(fd);
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=UNSUPPORTED path=%s type=%u cmap=%u bpp=%u w=%u h=%u\n",
               requested, image_type, header[1], bits_per_pixel,
               width, height);
        return;
    }

    const size_t byte_count =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    const size_t source_row_bytes =
        static_cast<size_t>(width) * source_channels;
    if (byte_count == 0u || byte_count > 16u * 1024u * 1024u) {
        sceIoClose(fd);
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=BAD_SIZE path=%s bytes=%u\n",
               requested, static_cast<unsigned>(byte_count));
        return;
    }

    uint8_t *pixels = mcpe_assign_guest_bytes_5BL(fields + 2u, byte_count);
    uint8_t *source_row = static_cast<uint8_t *>(malloc(source_row_bytes));
    if (!pixels || !source_row) {
        free(source_row);
        sceIoClose(fd);
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=OUT_OF_MEMORY path=%s bytes=%u\n",
               requested, static_cast<unsigned>(byte_count));
        return;
    }

    bool decoded = true;
    for (uint32_t source_y = 0u; source_y < height; ++source_y) {
        const uint32_t row_offset = data_offset +
            static_cast<uint32_t>(source_y * source_row_bytes);
        if (!read_exact(fd, source_row,
                        static_cast<uint32_t>(source_row_bytes), row_offset)) {
            decoded = false;
            break;
        }

        const uint32_t destination_y =
            top_origin ? source_y : (height - 1u - source_y);
        uint8_t *destination =
            pixels + static_cast<size_t>(destination_y) * width * 4u;
        for (uint32_t source_x = 0u; source_x < width; ++source_x) {
            const uint32_t destination_x =
                right_origin ? (width - 1u - source_x) : source_x;
            const uint8_t *source =
                source_row + static_cast<size_t>(source_x) * source_channels;
            uint8_t *target =
                destination + static_cast<size_t>(destination_x) * 4u;
            target[0] = source[2];
            target[1] = source[1];
            target[2] = source[0];
            target[3] = source_channels == 4u ? source[3] : 255u;
        }
    }

    free(source_row);
    sceIoClose(fd);
    if (!decoded) {
        printf("[MCPE-STAGE5BN] TGA_FAIL reason=SHORT_READ path=%s\n",
               requested);
        return;
    }

    fields[0] = width;
    fields[1] = height;
    fields[3] = 0u;
    const uint32_t call =
        __sync_add_and_fetch(&g_mcpe_tga_calls_5BN, 1u);
    printf("[MCPE-STAGE5BN] TGA_OK call=%u path=%s w=%u h=%u bpp=%u bytes=%u\n",
           call, requested, width, height, bits_per_pixel,
           static_cast<unsigned>(byte_count));
}

static void *mcpe_read_asset_file_5BM(
    void *result_string, void *platform, const void *guest_path_string)
{
    (void)platform;

    if (!result_string)
        return result_string;

    const char *requested = guest_path_string
        ? *static_cast<char *const *>(guest_path_string)
        : NULL;
    if (!requested)
        requested = "";

    while (*requested == '/')
        ++requested;
    if (strncmp(requested, "data/assets/", 12u) == 0)
        requested += 12u;
    else if (strncmp(requested, "assets/", 7u) == 0)
        requested += 7u;

    if (!requested[0] || strstr(requested, "..") || strchr(requested, ':')) {
        mcpe_assign_guest_string_5BA(result_string, "", 0u);
        printf("[MCPE-STAGE5BM] ASSET_FILE_FAIL reason=INVALID_PATH path=%s\n",
               requested);
        return result_string;
    }

    char filename[512];
    const int filename_length =
        snprintf(filename, sizeof(filename),
                 "app0:data/assets/%s", requested);
    if (filename_length <= 0 ||
        static_cast<size_t>(filename_length) >= sizeof(filename)) {
        mcpe_assign_guest_string_5BA(result_string, "", 0u);
        printf("[MCPE-STAGE5BM] ASSET_FILE_FAIL reason=PATH_TOO_LONG path=%s\n",
               requested);
        return result_string;
    }

    const SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (fd < 0) {
        mcpe_assign_guest_string_5BA(result_string, "", 0u);
        printf("[MCPE-STAGE5BM] ASSET_FILE_FAIL reason=OPEN path=%s result=0x%08X\n",
               requested, static_cast<unsigned>(fd));
        return result_string;
    }

    const SceOff file_size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (file_size <= 0 || file_size > 16 * 1024 * 1024 ||
        sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
        sceIoClose(fd);
        mcpe_assign_guest_string_5BA(result_string, "", 0u);
        printf("[MCPE-STAGE5BM] ASSET_FILE_FAIL reason=SIZE_OR_SEEK path=%s size=%lld\n",
               requested, static_cast<long long>(file_size));
        return result_string;
    }

    const size_t size = static_cast<size_t>(file_size);
    uint8_t *bytes = mcpe_assign_guest_bytes_5BL(result_string, size);
    if (!bytes) {
        sceIoClose(fd);
        mcpe_assign_guest_string_5BA(result_string, "", 0u);
        printf("[MCPE-STAGE5BM] ASSET_FILE_FAIL reason=OUT_OF_GUEST_MEMORY path=%s size=%u\n",
               requested, static_cast<unsigned>(size));
        return result_string;
    }

    size_t consumed = 0u;
    while (consumed < size) {
        const int amount = sceIoRead(
            fd, bytes + consumed,
            static_cast<unsigned>(size - consumed));
        if (amount <= 0)
            break;
        consumed += static_cast<size_t>(amount);
    }
    sceIoClose(fd);

    if (consumed != size) {
        bytes[0] = 0u;
        uint32_t *header = reinterpret_cast<uint32_t *>(bytes - 12u);
        header[0] = 0u;
        header[1] = 0u;
        printf("[MCPE-STAGE5BM] ASSET_FILE_FAIL reason=SHORT_READ path=%s got=%u expected=%u\n",
               requested, static_cast<unsigned>(consumed),
               static_cast<unsigned>(size));
        return result_string;
    }

    printf("[MCPE-STAGE5BM] ASSET_FILE_OK path=%s bytes=%u\n",
           requested, static_cast<unsigned>(size));
    return result_string;
}

static void *vita_memcpy(void *d, const void *s, size_t n)
{
    return memcpy(d, s, n);
}

static void *vita_memmove(void *d, const void *s, size_t n)
{
    return memmove(d, s, n);
}

static void *vita_memset(void *d, int c, size_t n)
{
    return memset(d, c, n);
}

static int vita_memcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

static void *vita_memchr(const void *p, int c, size_t n)
{
    const unsigned char *q = static_cast<const unsigned char *>(p);

    for (size_t i = 0; i < n; ++i) {
        if (q[i] == static_cast<unsigned char>(c))
            return const_cast<unsigned char *>(&q[i]);
    }

    return NULL;
}

static size_t vita_strlen(const char *s) { return strlen(s); }
static int vita_strcmp(const char *a, const char *b) { return strcmp(a, b); }
static int vita_strncmp(const char *a, const char *b, size_t n)
{
    return strncmp(a, b, n);
}
static char *vita_strcpy(char *d, const char *s) { return strcpy(d, s); }
static char *vita_strncpy(char *d, const char *s, size_t n)
{
    return strncpy(d, s, n);
}
static char *vita_strcat(char *d, const char *s) { return strcat(d, s); }
static char *vita_strchr(const char *s, int c)
{
    return const_cast<char *>(strchr(s, c));
}
static char *vita_strrchr(const char *s, int c)
{
    return const_cast<char *>(strrchr(s, c));
}
static int vita_atoi(const char *s) { return atoi(s); }
static long vita_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
static unsigned long vita_strtoul(const char *s, char **e, int b)
{
    return strtoul(s, e, b);
}

static int vita_vsnprintf(
    char *dst,
    size_t size,
    const char *fmt,
    va_list ap)
{
    return vsnprintf(dst, size, fmt, ap);
}

static int vita_snprintf(
    char *dst,
    size_t size,
    const char *fmt,
    ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return r;
}

static int vita_vprintf(const char *fmt, va_list ap)
{
    return mcpe_diag_vprintf_5AW(fmt, ap);
}

static int vita_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = mcpe_diag_vprintf_5AW(fmt, ap);
    va_end(ap);
    return r;
}

extern "C" {
int __cxa_atexit(
    void (*destructor)(void *),
    void *object,
    void *dso_handle);

void __cxa_finalize(void *dso_handle);
int __cxa_guard_acquire(long long *guard);
void __cxa_guard_release(long long *guard);
void __cxa_guard_abort(long long *guard);
void __cxa_pure_virtual(void);
void __cxa_deleted_virtual(void);
}

static int vita_cxa_atexit(
    void (*destructor)(void *),
    void *object,
    void *dso)
{
    return __cxa_atexit(destructor, object, dso);
}

static void vita_cxa_finalize(void *dso) { __cxa_finalize(dso); }
static int vita_cxa_guard_acquire(long long *g) { return __cxa_guard_acquire(g); }
static void vita_cxa_guard_release(long long *g) { __cxa_guard_release(g); }
static void vita_cxa_guard_abort(long long *g) { __cxa_guard_abort(g); }

static void vita_cxa_pure_virtual(void)
{
    printf("[MCPE-STAGE4F] __cxa_pure_virtual invoked\n");
    abort();
}

static void vita_cxa_deleted_virtual(void)
{
    printf("[MCPE-STAGE4F] __cxa_deleted_virtual invoked\n");
    abort();
}

static void *vita_operator_new(size_t size) { return malloc(size); }
static void vita_operator_delete(void *p) { free(p); }
static void *vita_operator_new_array(size_t size) { return malloc(size); }
static void vita_operator_delete_array(void *p) { free(p); }

static uintptr_t g_mcpe_exidx_runtime_5BG = 0;
static int g_mcpe_exidx_count_5BG = 0;
static uintptr_t g_mcpe_image_begin_5BG = 0;
static uintptr_t g_mcpe_image_end_5BG = 0;
static uintptr_t g_mcpe_runtime_base_5CL = 0;

static uintptr_t vita_gnu_Unwind_Find_exidx(uintptr_t pc, int *count)
{
    if (g_mcpe_exidx_runtime_5BG &&
        pc >= g_mcpe_image_begin_5BG &&
        pc < g_mcpe_image_end_5BG) {
        if (count)
            *count = g_mcpe_exidx_count_5BG;
        return g_mcpe_exidx_runtime_5BG;
    }

    if (count)
        *count = 0;
    return 0;
}

static void vita_stack_chk_fail(void)
{
    printf("[MCPE-STAGE4F] __stack_chk_fail invoked\n");
    abort();
}

static int32_t vita_aeabi_idiv(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    if (a == INT32_MIN && b == -1)
        return INT32_MIN;
    return a / b;
}

static uint32_t vita_aeabi_uidiv(uint32_t a, uint32_t b)
{
    if (b == 0)
        return 0;
    return a / b;
}

static int32_t vita_aeabi_idivmod(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    return a % b;
}

static uint32_t vita_aeabi_uidivmod(uint32_t a, uint32_t b)
{
    if (b == 0)
        return 0;
    return a % b;
}

static double MCPE_GUEST_SOFTFP vita_sin(double x) { return sin(x); }
static double MCPE_GUEST_SOFTFP vita_cos(double x) { return cos(x); }
static double MCPE_GUEST_SOFTFP vita_tan(double x) { return tan(x); }
static double MCPE_GUEST_SOFTFP vita_asin(double x) { return asin(x); }
static double MCPE_GUEST_SOFTFP vita_acos(double x) { return acos(x); }
static double MCPE_GUEST_SOFTFP vita_atan(double x) { return atan(x); }
static double MCPE_GUEST_SOFTFP vita_atan2(double y, double x) { return atan2(y, x); }
static float MCPE_GUEST_SOFTFP vita_atan2f(float y, float x) { return atan2f(y, x); }
static double MCPE_GUEST_SOFTFP vita_sinh(double x) { return sinh(x); }
static double MCPE_GUEST_SOFTFP vita_cosh(double x) { return cosh(x); }
static double MCPE_GUEST_SOFTFP vita_tanh(double x) { return tanh(x); }
static double MCPE_GUEST_SOFTFP vita_exp(double x) { return exp(x); }
static double MCPE_GUEST_SOFTFP vita_log(double x) { return log(x); }
static double MCPE_GUEST_SOFTFP vita_log10(double x) { return log10(x); }
static double MCPE_GUEST_SOFTFP vita_pow(double a, double b) { return pow(a, b); }
static double MCPE_GUEST_SOFTFP vita_sqrt(double x) { return sqrt(x); }
static double MCPE_GUEST_SOFTFP vita_floor(double x) { return floor(x); }
static double MCPE_GUEST_SOFTFP vita_ceil(double x) { return ceil(x); }
static double MCPE_GUEST_SOFTFP vita_fabs(double x) { return fabs(x); }
static double MCPE_GUEST_SOFTFP vita_fmod(double a, double b) { return fmod(a, b); }
static double MCPE_GUEST_SOFTFP vita_modf(double x, double *ip) { return modf(x, ip); }
static double MCPE_GUEST_SOFTFP vita_frexp(double x, int *e) { return frexp(x, e); }
static double MCPE_GUEST_SOFTFP vita_ldexp(double x, int e) { return ldexp(x, e); }

static float MCPE_GUEST_SOFTFP vita_sinf(float x) { return sinf(x); }
static float MCPE_GUEST_SOFTFP vita_cosf(float x) { return cosf(x); }
static float MCPE_GUEST_SOFTFP vita_tanf(float x) { return tanf(x); }
static float MCPE_GUEST_SOFTFP vita_expf(float x) { return expf(x); }
static float MCPE_GUEST_SOFTFP vita_logf(float x) { return logf(x); }
static float MCPE_GUEST_SOFTFP vita_powf(float a, float b) { return powf(a, b); }
static float MCPE_GUEST_SOFTFP vita_sqrtf(float x) { return sqrtf(x); }
static float MCPE_GUEST_SOFTFP vita_floorf(float x) { return floorf(x); }
static float MCPE_GUEST_SOFTFP vita_ceilf(float x) { return ceilf(x); }
static float MCPE_GUEST_SOFTFP vita_fabsf(float x) { return fabsf(x); }
static float MCPE_GUEST_SOFTFP vita_fmodf(float a, float b) { return fmodf(a, b); }

static time_t vita_time(time_t *out)
{
    return time(out);
}

static clock_t vita_clock(void)
{
    return clock();
}

static double MCPE_GUEST_SOFTFP vita_difftime(time_t a, time_t b)
{
    return difftime(a, b);
}

static struct tm *vita_gmtime(const time_t *t)
{
    return gmtime(t);
}

static struct tm *vita_localtime(const time_t *t)
{
    return localtime(t);
}

static time_t vita_mktime(struct tm *tmv)
{
    return mktime(tmv);
}

static size_t vita_strftime(
    char *dst,
    size_t size,
    const char *fmt,
    const struct tm *tmv)
{
    return strftime(dst, size, fmt, tmv);
}

struct McpeBionicFile32;
static int mcpe_legacy_file_index(const McpeBionicFile32 *fp);

static volatile uint32_t g_mcpe_terminate_caller_5BH = 0;
static volatile uint32_t g_mcpe_terminate_message_started_5CI = 0;

static void mcpe_append_terminate_message_5CI(
    const void *data, size_t size)
{
    if (!data || size == 0u || g_mcpe_terminate_caller_5BH == 0u)
        return;

    const bool first = __sync_bool_compare_and_swap(
        &g_mcpe_terminate_message_started_5CI, 0u, 1u);
    const int flags = SCE_O_CREAT | SCE_O_WRONLY |
        (first ? SCE_O_TRUNC : SCE_O_APPEND);
    const SceUID fd = sceIoOpen(
        "ux0:data/mcpe/terminate_message.log", flags, 0