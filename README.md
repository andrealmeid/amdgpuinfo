# amdgpuinfo

Get informations from AMD Radeon GPUs.

- [AUR package](https://aur.archlinux.org/packages/amdgpuinfo-git)

---

### Dependencies

* pciutils

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

To get the VBIOS version, you need to run as root.

Options:
* `-h` `--help` Display Help
* `-s` `--short` Short form output - 1 GPU/line - `<PCI Bus.Dev.Func>:<GPU Type>:<Memory Type>`

---

### License

amdgpuinfo is licensed under [GPLv3](LICENSE).

This is a derivate work from
[amdmeminfo](https://github.com/minershive/amdmeminfo), by
Zuikkis <zuikkis@gmail.com> and Yann St.Arnaud <ystarnaud@gmail.com>.
