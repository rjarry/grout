# MPack - A C encoder/decoder for MessagePack
# https://github.com/ludocode/mpack
# MIT license (see LICENSE)

mpack_lib = static_library(
  'mpack', 'mpack.c',
  c_args: ['-DMPACK_STDIO=0'],
)
mpack_dep = declare_dependency(
  link_with: mpack_lib,
  include_directories: include_directories('.'),
)
