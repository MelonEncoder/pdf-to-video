project('pti_v2', 'cpp')

include_dirs = ['/usr/include/poppler']

linked_libs = ['-lpoppler-cpp']

executable(
    'test.out',
    'src/main.cpp',
    include_directories: include_dirs,
    link_args: linked_libs,
)
