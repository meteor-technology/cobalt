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
{
  'includes': [
    # Note that we are 'includes'ing a 'gyp' file, not a 'gypi' file.  The idea
    # is that we just want this file to *be* the parent gyp file.
    '<(DEPTH)/starboard/linux/x64x11/starboard_platform_tests.gyp',
  ],
}
