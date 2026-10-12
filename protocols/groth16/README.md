# Groth16

`common/` 保存平台无关的数据结构；`backends/cuda` 与 `backends/dcu` 分别保存 NVIDIA CUDA 和 HIP/DCU 实现。

两个后端目前都依赖 lightZK 原有的 `depends` 树。配置时传入：

```bash
cmake -S ../../.. -B build \
  -DNKR_ZKP_GROTH16_BACKEND=CUDA \
  -DNKR_ZKP_GROTH16_DEPS_DIR=/path/to/lightZK/depends
```

DCU 时将 `CUDA` 换成 `DCU`。依赖必须包含 `libsnark/`；CUDA 测试还会使用 `cgbn/`。

