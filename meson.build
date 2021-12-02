project(
  'comdel',
  ['c', 'cpp'],
  version : run_command(
    [find_program('python', 'python3'), 'bin/meson_get_version.py']
  ).stdout(),
  license : 'MIT',
  meson_version : '>= 0.52',
  default_options : ['buildtype=debug'] #, 'b_ndebug=if-release', 'c_std=c99', 'cpp_std=c++17']
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
buildtype = get_option('buildtype')
builddir = meson.current_build_dir()

null_dep = dependency('', required : false)

# Arguments for the preprocessor, put these in a separate array from the C and
# C++ (cpp in meson terminology) arguments since they need to be added to the
# default arguments for both C and C++.
pre_args = [
  '-D__STDC_CONSTANT_MACROS',
  '-D__STDC_FORMAT_MACROS',
  '-D__STDC_LIMIT_MACROS',
  '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  '-DPACKAGE_BUGREPORT="https://gitlab.freedesktop.org/mesa/mesa/-/issues"',
]

extra_cpp_args = ['-Wall', '-std=c++17']

if buildtype == 'release'
  extra_cpp_args += ['-O3', '-g', '-Wno-strict-aliasing', '-msse4.1', '-flto']
endif

extra_c_args = extra_cpp_args

#if cpp.get_id() == 'gcc'
#  extra_cpp_args += ['-std=c++17']
#else
#  extra_cpp_args += []
#endif

link_args = ['-ldl', '-lpthread', '-L' + meson.current_build_dir(), '-lstdc++' ]

model_symbol_list = meson.current_source_dir() + '/model_symbol_export.def'

#pymodule = import('python3')
#python3 = pymodule.find_python()

COSTREAM_HOME = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["COSTREAM_HOME"])']).stdout().strip()
CODRIVE_HOME = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["CODRIVE_HOME"])']).stdout().strip()
COASM_HOME = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["COASM_HOME"])']).stdout().strip()
TVM_HOME = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["TVM_HOME"])']).stdout().strip()
LIBGO_HOME = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["LIBGO_HOME"])']).stdout().strip()
COMODEL_HOME = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["COMODEL_HOME"])']).stdout().strip()

LINUX_SRC = run_command([find_program('python', 'python3'), '-c', 'import os; print(os.environ["LINUX_SRC"])']).stdout().strip()

message('COSTREAM_HOME is ' + COSTREAM_HOME)
message('CODRIVE_HOME is ' + CODRIVE_HOME)
message('COASM_HOME is ' + COASM_HOME)
message('TVM_HOME is ' + TVM_HOME)
message('CMODEL_HOME is ' + COMODEL_HOME)

costream_inc = [COSTREAM_HOME + '/include']
costream_src = COSTREAM_HOME + '/src/'


codrive_inc = ['.', 'include', 'src', 'src/drive', 'src/device', 'src/common', '3rdparty/msgpack-c/include']
#CODRIVER_HOME + '/common'
codrive_inc += costream_inc
codrive_inc += 'src/cmdio'

message(codrive_inc)

util_src = [
  costream_src + 'core/util/lnx/os_linux.cpp']

codrive_src = [
  'src/device/Device.cpp',
  'src/device/DeviceInfo.cpp',
  'src/device/GpuMemory.cpp',
  'src/device/MemMgr.cpp',
  'src/device/Vma.cpp',
  'src/device/Debug.cpp',
  'src/device/Doorbells.cpp',
  'src/device/QueueMgr.cpp',
  'src/device/EventMgr.cpp',
  'src/device/Topology.cpp',
  'src/drive/Agent.cpp',
  'src/drive/loader_api.cc',
  'src/drive/MemoryRegion.cpp',
  'src/drive/Runtime.cpp',
  'src/drive/loader/CodeObject.cpp',
  'src/drive/loader/ElfImage.cpp',
  'src/drive/loader/Executable.cpp',
  'src/drive/loader/LoaderContext.cpp',
  'src/drive/loader/OfflineLoaderContext.cpp',
  ] + util_src

codrive_src = [
  'src/device/Device.cpp',
  'src/device/DeviceInfo.cpp',
  'src/device/GpuMemory.cpp',
  'src/device/MemMgr.cpp',
  'src/device/Vma.cpp',
  'src/device/Debug.cpp',
  'src/device/Doorbells.cpp',
  'src/device/QueueMgr.cpp',
  'src/device/EventMgr.cpp',
  'src/device/MemObj.cpp',
  'src/device/Topology.cpp'] + util_src

cmdio_inc = codrive_inc
cmdio_inc += LINUX_SRC + '/include/'
cmdio_inc += LINUX_SRC + '/arch/x86/include/generated'
cmdio_inc += LINUX_SRC + '/arch/x86/include/'

cmdio_src = [
  'src/cmdio/cmdio_api.cpp',
  'src/cmdio/linux/opu_core.c',
  'src/cmdio/linux/opu_chip.c',
  'src/cmdio/linux/opu_deps.c',
  'src/cmdio/linux/opu_drv.c',
  'src/cmdio/linux/opu_fence.c',
  'src/cmdio/linux/opu_kms.c',
  'src/cmdio/linux/opu_module.c',
  'src/cmdio/linux/opu_core_asic_v1.c',
  'src/cmdio/linux/opu_chardev.c',
  'src/cmdio/linux/opu_crat.c',
  'src/cmdio/linux/opu_dbgdev.c',
  'src/cmdio/linux/opu_dbgmgr.c',
  'src/cmdio/linux/opu_device.c',
  'src/cmdio/linux/opu_doorbell.c',
  'src/cmdio/linux/opu_events.c',
  'src/cmdio/linux/opu_flat_memory.c',
  'src/cmdio/linux/opu_interrupt.c',
  'src/cmdio/linux/opu_iommu.c',
  'src/cmdio/linux/opu_migrate.c',
  'src/cmdio/linux/opu_module.c',
  'src/cmdio/linux/opu_pasid.c',
  'src/cmdio/linux/opu_process.c',
  'src/cmdio/linux/opu_queue.c',
  'src/cmdio/linux/opu_svm.c',
  'src/cmdio/linux/opu_topology.c',
  'src/cmdio/linux/opu_acpi.c'
  ]

#codrive_link_args = link_args + ['-Wl,--version-script='+codrive_symbol_list]
codrive_link_args = link_args + ['-lpthread']
cmdio_link_args = link_args + ['-lpthread']

codrive = shared_library(
  'codrive',
  codrive_src,
  include_directories : include_directories(codrive_inc),
  install : false,
  cpp_args : extra_cpp_args,
  link_args : codrive_link_args,
  )

cmdio = shared_library(
  'cmdio',
  cmdio_src,
  include_directories : include_directories(cmdio_inc),
  install : false,
  cpp_args : extra_cpp_args,
  link_args : cmdio_link_args,
  )



#test_src = [
#  'tests/test/simple.cpp'
#  ]

#test_src += codrive_src

#test_inc = codrive_inc

#cotest = executable(
#  'cotest',
#  test_src,
#  include_directories : test_inc,
#  install :false,
#  link_args : codrive_link_args,
#  #dependencies: declare_dependency(link_with : codrive)
#  )
