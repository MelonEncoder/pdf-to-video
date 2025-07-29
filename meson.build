project('pdf_to_video', 'cpp')

output = './bin/ptv'
srcs = [
    'src/main.cpp',
]
args = [
    '-DWITH_FFMPEG=ON',
]

## Dependencies
deps = [
    dependency('poppler-cpp', version: '>=25.01.0'),
    dependency('opencv4', version: '>=4.12.0'),
]

executable(
    output,
    sources: srcs,
    cpp_args: args,
    dependencies: deps,
)
