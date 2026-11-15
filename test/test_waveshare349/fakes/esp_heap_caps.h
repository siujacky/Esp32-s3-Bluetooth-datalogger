#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_DMA=1;
inline void *heap_caps_malloc(size_t n,int){return malloc(n);}
