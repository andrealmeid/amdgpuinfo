# amdgpuinfo

Get informations from AMD Radeon GPUs.

---

### Dependencies

* ocl-icd
* opencl-headers

### Build

* `git clone https://github.com/andrealmeid/amdgpuinfo`
* `cd amdgpuinfo`
* `meson build`
* `ninja -C build`

### Run locally

* `./build/amdgpuinfo`

### Installation

* `ninja -C build install`

### Usage

`./amdgpuinfo [options]`

Options:
* `-h` `--help` Display Help
* `-o` `--opencl` Order by OpenCL ID
* `-q` `--quiet` Only output results
* `-s` `--short` Short form output - 1 GPU/line - `<OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<Memory Type>`
* `--use-stderr` Output errors to stderr

