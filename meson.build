project('AMDGPUInfo', 'c',
        version: '0.2.0',
        meson_version: '>= 0.45.1',
        default_options : [
          'c_std=c11',
          'warning_level=3',
        ],
)

project_args = [
  '-D_GNU_SOURCE' # For strdup in C11
]
add_project_arguments(project_args, language: ['c'])

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('NAME', meson.project_name())

pci_dep = dependency('libpci')

configure_file(
  output: 'config.h',
  configuration: conf,
)

executable(
  'amdgpuinfo', ['amdgpuinfo.c'],
  dependencies: [pci_dep],
  install: true)

