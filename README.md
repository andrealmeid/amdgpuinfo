#amdmeminfo

Get GDDR5 memory information and other information from AMD Radeon GPUs.

---

##Installation

* Download ZIP or via Github client.
* Install AMDAPPSDK/OpenCL library (required for OpenCL functions)
* `sudo apt-get install libpci-dev`
* Unzip or git clone
* `cd amdmeminfo`
* Edit `Makefile` to specify AMDAPPSDK path
* `make`
* Optional: `sudo cp amdmeminfo /usr/local/bin`

---

##Usage

`amdmeminfo [options]`

Options:
`-h` `--help` Display Help
`-o` `--opencl` Order by OpenCL ID (cgminer/sgminer GPU order)
`-q` `--quiet` Only output results
`-s` `--short` Short form output - 1 GPU/line - `<OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<Memory Type>`
`--use-stderr` Output errors to stderr


