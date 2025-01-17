project('pdf_to_video', 'cpp')

output = 'ptv.test'
srcs = ['src/main.cpp']
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
    '-lopencv_videoio',
]

args = [
    '-DWITH_FFMPEG=ON',
]

## Dependencies
poppler = dependency('poppler-cpp')
opencv = dependency('opencv4')

executable(
    output,
    sources: srcs,
    include_directories: include_dirs,
    link_args: linked_libs,
    cpp_args: args,
    dependencies: [poppler, opencv],
)
