#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <cuda_runtime.h>

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

    ~TypedGpuArena() { if (base_ptr) cudaFree(base_ptr); }

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
        cudaError_t err = cudaMemGetInfo(&free_bytes, &total_bytes);
        if (err != cudaSuccess)
            throw std::runtime_error(std::string("cudaMemGetInfo failed: ") + cudaGetErrorString(err));
        const size_t reserve = 512ULL << 20;
        if (free_bytes <= reserve || total_size > free_bytes - reserve)
            throw std::runtime_error("refusing CUDA allocation: less than 512 MiB would remain free");
        err = cudaMalloc(&base_ptr, total_size);
        if (err != cudaSuccess)
            throw std::runtime_error(std::string("cudaMalloc failed: ") + cudaGetErrorString(err));

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
