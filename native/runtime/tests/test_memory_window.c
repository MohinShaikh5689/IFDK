/* runtime_memory.c with the iOS window layout (GOW3_WINDOW_LAYOUT), run on Linux:
 *   cc -std=c11 -O2 -Wall -Wextra -DGOW3_WINDOW_LAYOUT -pthread -Isrc src/runtime_memory.c \
 *      tests/test_memory_window.c -o test_memory_window && ./test_memory_window
 * (native/runtime/tests/run.sh does this.) Checks that every guest and host-owned mapping stays inside
 * [0x70_0000_0000, 0x80_0000_0000), that MAP_FIXED outside it is refused, that the pool is a shared
 * file (dirty pages are not anonymous memory, which is what iOS counts against the app), and that
 * released memory reads as zero and gives its storage back. */
#define _GNU_SOURCE
#include "runtime.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* What runtime_memory.c needs from the rest of the runtime. */
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,nid)) return (uintptr_t)table[i].function;
    return 0;
}
int runtime_memory_write_backing(uintptr_t address, const void *data, uint64_t size);

#define LOW_MIN UINT64_C(0x7000000000)
#define BACKING_MIN UINT64_C(0x7100000000)
#define USER_MIN UINT64_C(0x7400000000)
#define USER_MAX UINT64_C(0x8000000000)
#define MiB (UINT64_C(1)<<20)
typedef int32_t (*Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
typedef int32_t (*Map)(void **, uint64_t, int, int, int64_t, uint64_t);
typedef int32_t (*Unmap)(void *, uint64_t);
typedef int32_t (*Release)(uint64_t, uint64_t);
typedef int32_t (*Flexible)(void **, uint64_t, int, int);
typedef int32_t (*Reserve)(void **, uint64_t, int, uint64_t);
#define GET(type, name) ((type)runtime_memory_resolve(name))

static long status_kb(const char *field) {
    FILE *f=fopen("/proc/self/status","r");
    char line[256]; long value=-1; size_t n=strlen(field);
    while (f && fgets(line,sizeof(line),f)) if (!strncmp(line,field,n) && line[n]==':') value=strtol(line+n+1,NULL,10);
    if (f) fclose(f);
    return value;
}
/* The permissions /proc/self/maps lists for the mapping that holds `address`. */
static void perms_at(uintptr_t address, char out[5]) {
    FILE *f=fopen("/proc/self/maps","r");
    char line[512]; strcpy(out,"none");
    while (f && fgets(line,sizeof(line),f)) {
        uintptr_t a,b; char p[5];
        if (sscanf(line,"%" SCNxPTR "-%" SCNxPTR " %4s",&a,&b,p)==3 && address>=a && address<b) { memcpy(out,p,5); break; }
    }
    if (f) fclose(f);
}
static int in_user(void *p, uint64_t size) { return (uintptr_t)p>=USER_MIN && (uintptr_t)p+size<=USER_MAX; }

int main(void) {
    runtime_memory_reserve();
    char perms[5];
    perms_at(USER_MIN+123*MiB,perms); assert(!strcmp(perms,"---p")); /* the window is reserved */

    /* Host-owned guest-visible memory (image, stacks) comes from the low map. */
    unsigned char *low=runtime_low_map(100000,PROT_READ|PROT_WRITE), *low2=runtime_low_map(16384,PROT_READ|PROT_WRITE);
    assert((uintptr_t)low>=LOW_MIN && (uintptr_t)low2+16384<=BACKING_MIN && low2>low);
    low[99999]=1;
    perms_at((uintptr_t)low2-1,perms); assert(!strcmp(perms,"---p")); /* guard page between blocks */

    Allocate alloc=GET(Allocate,"rTXw65xmLIA#libkernel");
    Map map=GET(Map,"L-Q3LEjIbgA#libkernel");
    Unmap unmap=GET(Unmap,"cQke9UuBQOk#libkernel");
    Release release=GET(Release,"MBuItvba6z8#libkernel");
    Flexible flexible=GET(Flexible,"sceKernelMapFlexibleMemory");
    Reserve reserve=GET(Reserve,"sceKernelReserveVirtualRange");
    assert(alloc && map && unmap && release && flexible && reserve);

    /* Aliased direct mappings inside the user range (as gow3's test_runtime.c checks). */
    int64_t a=-1;
    const uint64_t big=256*MiB;
    assert(alloc(0,5056*MiB,big,2*MiB,0,&a)==0 && a==0);
    void *x=NULL,*y=NULL;
    assert(map(&x,big,3,0,a,2*MiB)==0 && in_user(x,big) && !((uintptr_t)x%(2*MiB)));
    assert(map(&y,16384,3,0,a,0)==0 && in_user(y,16384));
    *(uint64_t *)x=UINT64_C(0xabcdef0123456789);
    assert(*(uint64_t *)y==UINT64_C(0xabcdef0123456789));
    uint64_t v=42;
    assert(runtime_memory_write_backing((uintptr_t)x+8,&v,sizeof(v)) && ((uint64_t *)y)[1]==42);

    /* Dirty pool pages are file pages, not anonymous memory. */
    long anon_before=status_kb("RssAnon");
    memset(x,0x5a,big);
    long anon_after=status_kb("RssAnon");
    printf("256 MiB of guest memory written: RssAnon %+ld KiB, RssFile %ld KiB, RssShmem %ld KiB\n",
           anon_after-anon_before,status_kb("RssFile"),status_kb("RssShmem"));
    assert(anon_after-anon_before<16*1024);

    /* Release unmaps, punches the storage out and reads back as zero. */
    long file_before=status_kb("RssFile")+status_kb("RssShmem");
    assert(release(a,big)==0);
    perms_at((uintptr_t)x,perms); assert(!strcmp(perms,"---p")); /* back to the window reservation */
    assert(alloc(0,5056*MiB,big,0,0,&a)==0 && a==0);
    x=NULL; assert(map(&x,big,3,0,a,0)==0 && ((uint64_t *)x)[0]==0 && ((unsigned char *)x)[big-1]==0);
    printf("after release: file-backed RSS %+ld KiB\n",status_kb("RssFile")+status_kb("RssShmem")-file_before);
    assert(release(a,big)==0);

    /* Flexible memory, hints and MAP_FIXED. */
    void *f=(void *)UINT64_C(0x200000000); /* a PS4-style hint below the window: placed inside it instead */
    assert(flexible(&f,64*MiB,3,0)==0 && in_user(f,64*MiB));
    ((char *)f)[0]=7;
    void *fixed=(void *)(USER_MIN+UINT64_C(0x100000000));
    assert(flexible(&fixed,16384,3,0x10)==0 && fixed==(void *)(USER_MIN+UINT64_C(0x100000000)));
    void *outside=(void *)UINT64_C(0x1000000000); /* the Linux/PC user range: host memory on iOS */
    assert((uint32_t)flexible(&outside,16384,3,0x10)==0x8002000c);
    void *r=NULL;
    assert(reserve(&r,32*MiB,0,0)==0 && in_user(r,32*MiB));
    assert(unmap(f,64*MiB)==0 && unmap(fixed,16384)==0 && unmap(r,32*MiB)==0);
    perms_at((uintptr_t)f,perms); assert(!strcmp(perms,"---p"));

    puts("PASS: iOS guest window layout, file-backed pool, MAP_FIXED containment");
    return 0;
}
