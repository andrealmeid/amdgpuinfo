# amdgpuinfo

Get informations from AMD Radeon GPUs.

---

### Dependencies

* libpci

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
* `-s` `--short` Short form output - 1 GPU/line - `<OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<Memory Type>`
* `--use-stderr` Output errors to stderr

