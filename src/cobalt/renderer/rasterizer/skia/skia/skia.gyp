# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      # This is the target that all Cobalt modules should depend on if they
      # wish to use Skia.  It augments skia_library (Skia's core code) with
      # Cobalt specific code defined in source files from skia_cobalt.gypi,
      # such as platform-specific implementations of certain functions.
      'target_name': 'skia',
      'type': 'static_library',
      'dependencies': [
        'skia_library',
      ],
      'includes': [
        'skia_cobalt.gypi',
        'skia_common.gypi',
      ],
      'export_dependent_settings': [
        'skia_library',
      ],
    },

    {
      # Skia's core code from the Skia repository.
      'target_name': 'skia_library',
      'type': 'static_library',
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        'skia_library_no_asan',
      ],
      'includes': [
        'skia_common.gypi',
        'skia_library.gypi',
        'skia_sksl.gypi',
      ],
    },

    {
      # Skia's core code from the Skia repository that should be compiled
      # without ASAN. This is done to avoid drastic slowdown in debug unit
      # tests.
      'target_name': 'skia_library_no_asan',
      'type': 'static_library',
      'includes': [
        'skia_common.gypi',
        '<(DEPTH)/third_party/skia/gyp/effects_imagefilters.gypi',
      ],
      'sources': [
        '<@(skia_effects_imagefilter_sources_no_asan)',
      ],
      'target_conditions': [
        ['use_asan==1 and cobalt_config=="debug"', {
          'cflags!': [
            '-fsanitize=address',
          ],
        }],
      ],
    },

    {
      # A small program imported from Chromium that tests Skia with fuzzed
      # filters.
      'target_name': 'filter_fuzz_stub',
      'type': 'executable',
      'dependencies': [
        'skia',
      ],
      'sources': [
        'test/filter_fuzz_stub/filter_fuzz_stub.cc',
      ],
    },

    {
      'target_name': 'filter_fuzz_stub_deploy',
      'type': 'none',
      'dependencies': [
        'filter_fuzz_stub',
      ],
      'variables': {
        'executable_name': 'filter_fuzz_stub',
      },
      'includes': [ '<(DEPTH)/starboard/build/deploy.gypi' ],
    },
  ],
}
