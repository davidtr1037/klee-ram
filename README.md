Relocatable Addressing Model for KLEE
=============================

## Build

### Requirements
Install the following packages:
```
sudo apt-get install cmake bison flex libboost-all-dev python perl zlib1g-dev build-essential curl libcap-dev git cmake libncurses5-dev python-minimal python-pip unzip libtcmalloc-minimal4 libgoogle-perftools-dev libsqlite3-dev doxygen
pip3 install tabulate wllvm
```

### LLVM 7.0

```
wget https://releases.llvm.org/7.0.0/llvm-7.0.0.src.tar.xz
wget https://releases.llvm.org/7.0.0/cfe-7.0.0.src.tar.xz
wget https://releases.llvm.org/7.0.0/compiler-rt-7.0.0.src.tar.xz
tar xJf llvm-7.0.0.src.tar.xz
tar xJf cfe-7.0.0.src.tar.xz
tar xJf compiler-rt-7.0.0.src.tar.xz
mv cfe-7.0.0.src llvm-7.0.0.src/tools/clang
mv compiler-rt-7.0.0.src compiler-rt
mkdir llvm-7.0.0.obj
cd llvm-7.0.0.obj
cmake CMAKE_BUILD_TYPE:STRING=Release -DLLVM_ENABLE_THREADS:BOOL=ON -DLLVM_ENABLE_PROJECTS:STRING=compiler-rt ../llvm-7.0.0.src
make -j4
```
Update the following environment variables:
```
export PATH=<llvm_build_dir>/bin:$PATH
export LLVM_COMPILER=clang
```

### minisat

```
git clone https://github.com/stp/minisat.git
cd minisat
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local/ ../
sudo make install
```

### STP

```
git clone https://github.com/stp/stp.git
cd stp
git checkout tags/2.3.3
mkdir build
cd build
cmake ..
make
sudo make install
```

### klee-uclibc
```
git clone https://github.com/klee/klee-uclibc.git
cd klee-uclibc
git checkout 038b7dc050c07a7b4d941b48a0f548eea3089214 # we used that version in our experiments
./configure --make-llvm-lib
make
```

### Our Tool
To build our tool, which is an extension of KLEE, do the following:
```
mkdir <klee_ram_build_dir>
cd <klee_ram_build_dir>
CXXFLAGS="-fno-rtti" cmake \
    -DENABLE_SOLVER_STP=ON \
    -DENABLE_POSIX_RUNTIME=ON \
    -DENABLE_KLEE_UCLIBC=ON \
    -DKLEE_UCLIBC_PATH=<klee_uclibc_dir> \
    -DLLVM_CONFIG_BINARY=<llvm_build_dir>/bin/llvm-config \
    -DLLVMCC=<llvm_build_dir>/bin/clang \
    -DLLVMCXX=<llvm_build_dir>/bin/clang++ \
    -DENABLE_UNIT_TESTS=OFF \
    -DKLEE_RUNTIME_BUILD_TYPE=Release+Asserts \
    -DENABLE_SYSTEM_TESTS=ON \
    -DENABLE_TCMALLOC=ON \
    <klee_ram_dir>
make -j4
```

## Usage
### Command Line Options
We extended KLEE with several command line options.
The main options are:
- _use-sym-addr_: use the addressing model described in the paper
- _use-rebase_: enable dynamic merging of objects
- _split-objects_: enable dynamic splitting of objects

Merging related options:
- _reuse-segments_: reuse previously allocated segments
- _use-context-resolve=1_: use context-based resolution
- _use-kcontext=k_: use k-context abstraction for context-based resolution (default: k = 0)

Splitting related options:
- _split-threshold=N_: split objects larger than _N_ bytes
- _partition-size=N_: set the size of the split objects

### Example
Suppose that we have the following program:
```C
#include <stdio.h>
#include <stdlib.h>
#include <klee/klee.h>

#define N (2)

int main(int argc, char *argv[]) {
    char **array = calloc(N, sizeof(char *));
    for (unsigned int i = 0; i < N; i++) {
        array[i] = calloc(256, 1);
    }

    unsigned int i = klee_range(0, N, "i");
    unsigned int j = klee_range(0, 100, "j");
    if (array[i][j] == 7) {
        printf("...\n");
    }

    return 0;
}
```

Compile the program:
```
clang -c -g -emit-llvm -I <klee_include_dir> <source_file> -o <bitcode_file>
```

#### Vanilla KLEE
When we analyze this program with vanilla KLEE:
```
klee -libc=uclibc <bitcode_file>
```
we get 2 explored paths, since the symbolic pointer `array[i][j]` is resolved to two objects.

#### Merging
To analyze the program with the dynamic merging approach, use the following command:
```
klee -libc=uclibc -use-sym-addr -use-rebase <bitcode_file>
```
Now, you will see that only one path is explored,
since the two objects pointed by the symbolic pointer are dynamically merged into one segment.

#### Splitting
To analyze the program with the dynamic splitting approach, use the following command:
```
klee -libc=uclibc -use-sym-addr -split-objects -split-threshold=100 -partition-size=64 <bitcode_file>
```
Here, when a _big object_ (w.r.t. the specified threshold) is accessed with a symbolic offset, it is split to smaller objects of at most 64 bytes.
As in vanilla KLEE, the symbolic pointer `array[i][j]` is resolved here to two objects, which leads to a fork.
Then, each of the forked states accesses a big object (of size 256) with a symbolic offset,
so the accessed object is dynamically split into 4 objects.
After the split, the symbolic pointer `array[i][j]` is resolved to the first two smaller objects (since _j < 100_), and we have an additional fork.
In total we have 4 explored paths, which is more than we have with vanilla KLEE,
but the created SMT arrays are smaller.
That can be seen if looking at the query log:
```
klee -use-query-log=all:kquery -libc=uclibc -use-sym-addr -split-objects -split-threshold=100 -partition-size=64 <bitcode_file>
vi <klee_out_dir>/all-queries.kquery
```
