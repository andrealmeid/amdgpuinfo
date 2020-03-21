# amdgpuinfo

Get informations from AMD Radeon GPUs.

---

### Dependencies

* ocl-icd
* opencl-headers

### Installation

* `git clone https://github.com/andrealmeid/amdgpuinfo`
* `cd amdgpuinfo`
* `make`
* Optional: `sudo cp amdgpuinfo /usr/local/bin`

### Usage

`./amdgpuinfo [options]`

Options:
* `-h` `--help` Display Help
* `-o` `--opencl` Order by OpenCL ID
* `-q` `--quiet` Only output results
* `-s` `--short` Short form output - 1 GPU/line - `<OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<Memory Type>`
* `--use-stderr` Output errors to stderr

