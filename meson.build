project('vtermdriver', ['c', 'cpp'])

cc = meson.get_compiler('cpp')

util_dep = cc.find_library('util')

vterm_dep = dependency('vterm')


qt5_dep = dependency('qt5', modules: 'Core')
qt5 = import('qt5')

#ide:editable-filelist
vtermdriver_files = [
  'capture.cpp',
  'main.cpp',
]

executable('vtermdriver', vtermdriver_files, dependencies: [vterm_dep, qt5_dep, util_dep])

