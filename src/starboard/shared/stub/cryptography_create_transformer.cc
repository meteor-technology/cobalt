// Copyright 2017 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/configuration.h"

#if SB_API_VERSION < 12

#include "starboard/cryptography.h"

SbCryptographyTransformer SbCryptographyCreateTransformer(
    const char* algorithm,
    int block_size_bits,
    SbCryptographyDirection direction,
    SbCryptographyBlockCipherMode mode,
    const void* initialization_vector,
    int initialization_vector_size,
    const void* key,
    int key_size) {
  return kSbCryptographyInvalidTransformer;
}

#endif  // SB_API_VERSION < 12
