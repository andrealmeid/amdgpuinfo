project('amdgpuinfo', 'c',
        version: '0.1',
        meson_version: '>= 0.45.1',
        default_options : [
          'c_std=c11',
          'warning_level=3',
        ],
)

pci_dep = dependency('libpci')

executable(
  'amdgpuinfo', ['amdgpuinfo.c'],
  dependencies: [pci_dep],
  install: true)

