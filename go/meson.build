# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Robin Jarry

go_prog = find_program('go', native: true, required: false)
if not go_prog.found()
  subdir_done()
endif

go_cgo_flags = '-std=gnu2x -fms-extensions -Wno-microsoft'
go_cgo_flags += ' -DGROUT_VERSION="' + meson.project_version() + '"'
go_cgo_flags += ' -I' + meson.current_source_dir()

custom_target(
  'go-build',
  output: '.go-build.stamp',
  command: [
    'sh', '-c',
    'cd @0@ &&'.format(meson.project_source_root()) +
    ' CGO_CFLAGS_ALLOW=\'-fms-extensions|-Wno-microsoft\'' +
    ' CGO_CFLAGS=\'' + go_cgo_flags + '\'' +
    ' @0@ build ./go/ && touch @1@'.format(
      go_prog.full_path(),
      '@OUTPUT@',
    ),
  ],
  depend_files: files(
    'grout.go', 'shim.c', 'shim.h',
    'errors.go', 'types.go',
    'iface.go', 'nexthop.go',
    'ip4.go', 'ip6.go',
    'l2.go', 'nat.go', 'conntrack.go',
    'dhcp.go', 'srv6.go',
    'stats.go', 'affinity.go', 'event.go',
  ),
  build_by_default: true,
)
