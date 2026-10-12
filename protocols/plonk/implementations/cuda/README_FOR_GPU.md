This project utilizes GPU acceleration to enhance the proving performance. All relevant GPU-accelerated code can be found within the `./gpu` directory of this repository. As of now, MSM/FFT computations has been transitioned to utilize GPU acceleration. However, it's important to note that this implementation currently serves as a framework. Many computations within the project still rely on CPU processing rather than taking full advantage of GPU capabilities.

## How to run?
```
export PATH=$PATH:/usr/local/cuda/bin/ // specify the correct cuda path
<!-- generate the cuda kernel -->
cd ./gpu/TrapdoorTech-zprize-ec-gpu-kernels
cargo run --release -- --cuda
cp gpu-kernel.bin ../../
cd ../../
<!-- run the matrix-multiplication script -->
cargo run --release --example matmul
```

## How to generate/update kernel image?
```
cd ./gpu/TrapdoorTech-zprize-ec-gpu-kernels
cargo run --release -- --cuda
cp gpu-kernel.bin ../../
```

Wishing you all an enjoyable development experience.
