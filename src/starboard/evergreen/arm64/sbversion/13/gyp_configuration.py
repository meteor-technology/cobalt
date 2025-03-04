# Copyright 2021 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Starboard evergreen-arm64 platform configuration for gyp_cobalt."""

from starboard.evergreen.arm64 import gyp_configuration as parent_configuration


def CreatePlatformConfig():
  return parent_configuration.EvergreenArm64Configuration(
      'evergreen-arm64-sbversion-13',
      sabi_json_path='starboard/sabi/arm64/sabi-v13.json')
