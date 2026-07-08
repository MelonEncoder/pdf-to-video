project('PTV', 'cpp')

output = './bin/ptv'
exe = executable(
    output,
    sources: [
        'src/main.cpp',
        'src/ptv.cpp',
    ],
    cpp_args: [
        '-DWITH_FFMPEG=ON',
    ],
    dependencies: [
        dependency('poppler-cpp', version: '>=25.01.0'),
        dependency('opencv5', version: '>=5.0.0'),
    ],
)
