project('pti_v2', 'cpp')

output = 'test.out'

srcs = ['src/main.cpp', 'src/logic.cpp']

include_dirs = [
    '/usr/include/poppler',
    '/usr/include/opencv4/opencv2',
]

## found in the /usr/lib/ dir
linked_libs = [
    '-lpoppler-cpp',
    '-lopencv_core',
    '-lopencv_imgproc',
    '-lopencv_highgui',
    '-lopencv_imgcodecs',
]

executable(
    output,
    sources: srcs,
    include_directories: include_dirs,
    link_args: linked_libs,
)
