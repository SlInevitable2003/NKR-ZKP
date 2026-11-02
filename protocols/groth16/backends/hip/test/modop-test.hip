#include <cstring>
#include <random>
#include <iomanip>
#include "libff/common/profiling.hpp"
#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"

#include <gmp.h>
#include <omp.h>
using namespace std;

#include "api.h"
#include "arith-tester.h"
using namespace alt_bn128;
typedef libsnark::default_r1cs_ppzksnark_pp ppT;

#define ADD_TIMES 8
void modadd_compute(TestGPULayout<fr_t, fr_t> &gpu_layout)
{
    size_t n = gpu_layout.n;
    kernel<<<n / 256, 256>>>([=] __device__ (fr_t *array, fr_t *result) {
        int tid = threadIdx.x + blockIdx.x * blockDim.x;
        fr_t acc = array[2 * tid], inc = array[2 * tid + 1];
        #pragma unroll
        for (int i = 0; i < ADD_TIMES; i++) acc += inc;
        result[tid] = acc;
    }, gpu_layout.device_array, gpu_layout.device_result);
    hipDeviceSynchronize();
    CUDA_CHECK(hipGetLastError());
}

void modmul_compute(TestGPULayout<fr_t, fr_t> &gpu_layout)
{
    size_t n = gpu_layout.n;
    kernel<<<n / 256, 256>>>([=] __device__ (fr_t *array, fr_t *result) {
        int tid = threadIdx.x + blockIdx.x * blockDim.x;
        fr_t read[2] = {array[2 * tid], array[2 * tid + 1]};
        result[tid] = read[0] * read[1];
    }, gpu_layout.device_array, gpu_layout.device_result);
    hipDeviceSynchronize();
    CUDA_CHECK(hipGetLastError());
}

void modsub_compute(TestGPULayout<fr_t, fr_t> &gpu_layout)
{
    size_t n = gpu_layout.n;
    kernel<<<n / 256, 256>>>([=] __device__ (fr_t *array, fr_t *result) {
        int tid = threadIdx.x + blockIdx.x * blockDim.x;
        fr_t acc = array[2 * tid], inc = array[2 * tid + 1];
        #pragma unroll
        for (int i = 0; i < ADD_TIMES; i++) acc -= inc;
        result[tid] = acc;
    }, gpu_layout.device_array, gpu_layout.device_result);
    hipDeviceSynchronize();
    CUDA_CHECK(hipGetLastError());
}

int main(int argc, char *argv[])
{
    size_t n = size_t(1) << 24;

    ppT::init_public_params();

    int t = 1;
    if (argc > 1) t = atoi(argv[1]);
    while (t--) {

        if (1) {
            Tester<libff::Fr<ppT>, libff::Fr<ppT>> tester(
                n,
                [] (libff::Fr<ppT> &x) { 
                    x = libff::Fr<ppT>::random_element();
                },
                [] (const libff::Fr<ppT> &x, const libff::Fr<ppT> &y) { 
                    libff::Fr<ppT> s = x;
                    #pragma unroll
                    for (int i = 0; i < ADD_TIMES; i++) s += y;
                    return s; 
                },
                "Addition Under Module"
            );
            TestGPULayout<fr_t, fr_t> gpu_layout;
            tester.gpu_bench(gpu_layout, test_gpu_setup<libff::Fr<ppT>, fr_t, fr_t>, modadd_compute);
        }
	printf("\n############################################\n\n");
        if (1) {
            Tester<libff::Fr<ppT>, libff::Fr<ppT>> tester(
                n,
                [] (libff::Fr<ppT> &x) { x = libff::Fr<ppT>::random_element(); },
                [] (const libff::Fr<ppT> &x, const libff::Fr<ppT> &y) { 
                    libff::Fr<ppT> s = x;
                    #pragma unroll
                    for (int i = 0; i < ADD_TIMES; i++) s -= y;
                    return s;
                },
                "Subtraction Under Module"
            );
            TestGPULayout<fr_t, fr_t> gpu_layout;
            tester.gpu_bench(gpu_layout, test_gpu_setup<libff::Fr<ppT>, fr_t, fr_t>, modsub_compute);
        }
	
	if (1) {

            Tester<libff::Fr<ppT>, libff::Fr<ppT>> tester(
                n,
                [] (libff::Fr<ppT> &x) { x = libff::Fr<ppT>::random_element(); },
                [] (const libff::Fr<ppT> &x, const libff::Fr<ppT> &y) { return x * y; },
                "Multiplication Under Module"
            );
            TestGPULayout<fr_t, fr_t> gpu_layout;
            tester.gpu_bench(gpu_layout, test_gpu_setup<libff::Fr<ppT>, fr_t, fr_t>, modmul_compute);
      	}

    }

    return 0;
}
