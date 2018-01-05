
#include <SkColorTable.h>
#include <SkBitmap.h>

extern "C" bool _ZN8SkBitmap14tryAllocPixelsEPNS_9AllocatorE(SkBitmap::Allocator* allocator);
extern "C" void _ZNK8SkBitmap10lockPixelsEv() {}
extern "C" void _ZNK8SkBitmap12unlockPixelsEv() {}

extern "C" bool _ZN8SkBitmap14tryAllocPixelsEPNS_9AllocatorEP12SkColorTable(SkBitmap::Allocator* allocator, SkColorTable* ctable) {
    return _ZN8SkBitmap14tryAllocPixelsEPNS_9AllocatorE(allocator);
}
