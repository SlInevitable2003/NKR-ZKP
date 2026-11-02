#pragma once
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <hip/hip_runtime.h>

// Polynomial kernels also use this helper while executing on the device.
// DCC does not implicitly make ordinary inline functions device-callable.
__host__ __device__ inline size_t ceil_div(size_t a, size_t b)
{
   return (a + b - 1) / b;
}
inline size_t round_up(size_t a, size_t b) { return ceil_div(a, b) * b; }
inline size_t round_down(size_t a, size_t b) { return a / b * b; }
inline size_t log2_floor(size_t a) { return 63 - __builtin_clzll(a); }

#define HIP_CHECK(ans) { hip_assert((ans), __FILE__, __LINE__); }
// Retained for imported Groth16 sources which historically used CUDA_CHECK for
// both runtimes. New HIP code should use HIP_CHECK.
#define CUDA_CHECK(ans) HIP_CHECK(ans)
inline void hip_assert(hipError_t code, const char *file, int line) {
   if (code != hipSuccess) {
      fprintf(stderr, "HIP Error: %s %s %d\n", hipGetErrorString(code), file, line);
      std::abort();
   }
}

template<typename T>
__device__ __host__ void print_mem(const T& val) {
   const uint8_t *ptr = reinterpret_cast<const uint8_t*>(&val);
   for (int i = 0; i < sizeof(T); i++) printf("%02x", ptr[i]);
   printf("\n");
}

template<typename F, typename... Args> __global__ void kernel(F func, Args... args) { func(args...); }

struct TypedGpuArena {
    struct Request {
        void **out_ptr;
        size_t size;
        size_t alignment;
    };

    static size_t align_up(size_t offset, size_t alignment)
    {
        size_t mod = offset % alignment;
        return mod ? (offset + alignment - mod) : offset;
    }

    std::vector<Request> requests;

    void *base_ptr = nullptr;
    bool committed = false;

public:
    TypedGpuArena() = default;

    ~TypedGpuArena() { if (base_ptr) hipFree(base_ptr); }

    TypedGpuArena(const TypedGpuArena&) = delete;
    TypedGpuArena& operator=(const TypedGpuArena&) = delete;

    template<typename T>
    void register_alloc(T*& out_ptr, size_t count)
    {
        assert(!committed && "Cannot register after commit");
        requests.push_back({reinterpret_cast<void**>(&out_ptr), sizeof(T) * count, alignof(T)});
    }

    void commit(const char *info = "TypedGpuArena")
    {
        assert(!committed && "Already committed");

        size_t total_size = 0;

        for (auto& r : requests) {
            total_size = align_up(total_size, r.alignment);
            total_size += r.size;
        }

        size_t free_bytes = 0, total_bytes = 0;
        hipError_t err = hipMemGetInfo(&free_bytes, &total_bytes);
        if (err != hipSuccess)
            throw std::runtime_error(std::string("hipMemGetInfo failed: ") +
                                     hipGetErrorString(err));
        const size_t reserve = 512ULL << 20;
        if (free_bytes <= reserve || total_size > free_bytes - reserve)
            throw std::runtime_error(
                "refusing HIP allocation: less than 512 MiB would remain free");
        err = hipMalloc(&base_ptr, total_size);
        if (err != hipSuccess)
            throw std::runtime_error(std::string("hipMalloc failed: ") +
                                     hipGetErrorString(err));

        char* ptr = reinterpret_cast<char*>(base_ptr);
        size_t offset = 0;

        for (auto& r : requests) {
            offset = align_up(offset, r.alignment);
            *(r.out_ptr) = ptr + offset;
            offset += r.size;
        }

        committed = true;
        printf("[%s] Successfully alloc %f GB memory.\n", info, double(total_size) / double(1 << 30));
    }
};

void check_gpu_ptr(void* ptr);
