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

// Module Overview: Starboard Cryptography module
//
// Hardware-accelerated cryptography. Platforms should **only** implement this
// when there are **hardware-accelerated** cryptography facilities. Applications
// must fall back to platform-independent CPU-based algorithms if the cipher
// algorithm isn't supported in hardware.
//
// # Tips for Porters
//
// You should implement cipher algorithms in this descending order of priority
// to maximize usage for SSL.
//
//   1. GCM - The preferred block cipher mode for OpenSSL, mainly due to speed.
//   2. CTR - This can be used internally with GCM, as long as the CTR
//            implementation only uses the last 4 bytes of the IV for the
//            counter. (i.e. 96-bit IV, 32-bit counter)
//   3. ECB - This can be used (with a null IV) with any of the other cipher
//            block modes to accelerate the core AES algorithm if none of the
//            streaming modes can be accelerated.
//   4. CBC - GCM is always preferred if the server and client both support
//            it. If not, they will generally negotiate down to AES-CBC. If this
//            happens, and CBC is supported by SbCryptography, then it will be
//            accelerated appropriately. But, most servers should support GCM,
//            so it is not likely to come up much, which is why it is the lowest
//            priority.
//
// Further reading on block cipher modes:
//
// https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation
//
// https://crbug.com/442572
//
// https://crypto.stackexchange.com/questions/10775/practical-disadvantages-of-gcm-mode-encryption
//

#ifndef STARBOARD_CRYPTOGRAPHY_H_
#define STARBOARD_CRYPTOGRAPHY_H_

#include "starboard/configuration.h"
#include "starboard/export.h"
#include "starboard/types.h"

#if SB_API_VERSION >= 12
#error "Starboard Crypto API is deprecated"
#else

#ifdef __cplusplus
extern "C" {
#endif

// String literal for the AES symmetric block cipher.
// https://en.wikipedia.org/wiki/Advanced_Encryption_Standard
#define kSbCryptographyAlgorithmAes "aes"

// The direction of a cryptographic transformation.
typedef enum SbCryptographyDirection {
  // Cryptographic transformations that encode/encrypt data into a
  // target format.
  kSbCryptographyDirectionEncode,

  // Cryptographic transformations that decode/decrypt data into
  // its original form.
  kSbCryptographyDirectionDecode,
} SbCryptographyDirection;

// The method of chaining encrypted blocks in a sequence.
// https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation
typedef enum SbCryptographyBlockCipherMode {
  // Cipher Block Chaining mode.
  kSbCryptographyBlockCipherModeCbc,

  // Cipher Feedback mode.
  kSbCryptographyBlockCipherModeCfb,

  // Counter mode: A nonce is combined with an increasing counter.
  kSbCryptographyBlockCipherModeCtr,

  // Electronic Code Book mode: No chaining.
  kSbCryptographyBlockCipherModeEcb,

  // Output Feedback mode.
  kSbCryptographyBlockCipherModeOfb,

  // Galois/Counter Mode.
  kSbCryptographyBlockCipherModeGcm,
} SbCryptographyBlockCipherMode;

// Private structure representing a cryptographic transformer
// configuration and state.
typedef struct SbCryptographyTransformerPrivate
    SbCryptographyTransformerPrivate;

// A handle to a cryptographic transformer.
typedef SbCryptographyTransformerPrivate* SbCryptographyTransformer;

// Well-defined value for an invalid transformer.
#define kSbCryptographyInvalidTransformer ((SbCryptographyTransformer)NULL)

// Returns whether the given transformer handle is valid.
static SB_C_INLINE bool SbCryptographyIsTransformerValid(
    SbCryptographyTransformer transformer) {
  return transformer != kSbCryptographyInvalidTransformer;
}

// Creates an SbCryptographyTransformer with the given initialization
// data. It can then be used to transform a series of data blocks.
// Returns kSbCryptographyInvalidTransformer if the algorithm isn't
// supported, or if the parameters are not compatible with the
// algorithm.
//
// An SbCryptographyTransformer contains all state to start decrypting
// a sequence of cipher blocks according to the cipher block mode. It
// is not thread-safe, but implementations must allow different
// SbCryptographyTransformer instances to operate on different threads.
//
// All parameters must not be assumed to live longer than the call to
// this function. They must be copied by the implementation to be
// retained.
//
// This function determines success mainly based on whether the combination of
// |algorithm|, |direction|, |block_size_bits|, and |mode| is supported and
// whether all the sizes passed in are sufficient for the selected
// parameters. In particular, this function cannot verify that the key and IV
// used were correct for the ciphertext, were it to be used in the decode
// direction. The caller must make that verification.
//
// For example, to decrypt AES-128-CTR:
//   SbCryptographyCreateTransformer(kSbCryptographyAlgorithmAes, 128,
//                                   kSbCryptographyDirectionDecode,
//                                   kSbCryptographyBlockCipherModeCtr,
//                                   ...);
//
// |algorithm|: A string that represents the cipher algorithm.
// |block_size_bits|: The block size variant of the algorithm to use, in bits.
// |direction|: The direction in which to transform the data.
// |mode|: The block cipher mode to use.
// |initialization_vector|: The Initialization Vector (IV) to use. May be NULL
// for block cipher modes that don't use it, or don't set it at init time.
// |initialization_vector_size|: The size, in bytes, of the IV.
// |key|: The key to use for this transformation.
// |key_size|: The size, in bytes, of the key.
//
// presubmit: allow sb_export mismatch
SB_EXPORT SbCryptographyTransformer
SbCryptographyCreateTransformer(const char* algorithm,
                                int block_size_bits,
                                SbCryptographyDirection direction,
                                SbCryptographyBlockCipherMode mode,
                                const void* initialization_vector,
                                int initialization_vector_size,
                                const void* key,
                                int key_size);

// Destroys the given |transformer| instance.
//
// presubmit: allow sb_export mismatch
SB_EXPORT void SbCryptographyDestroyTransformer(
    SbCryptographyTransformer transformer);

// Transforms one or more |block_size_bits|-sized blocks of |in_data|, with the
// given |transformer|, placing the result in |out_data|. Returns the number of
// bytes that were written to |out_data|, unless there was an error, in which
// case it will return a negative number.
//
// |transformer|: A transformer initialized with an algorithm, IV, cipherkey,
// and so on.
// |in_data|: The data to be transformed.
// |in_data_size|: The size of the data to be transformed, in bytes. Must be a
// multiple of the transformer's |block-size_bits|, or an error will be
// returned.
// |out_data|: A buffer where the transformed data should be placed. Must have
// at least capacity for |in_data_size| bytes. May point to the same memory as
// |in_data|.
//
// presubmit: allow sb_export mismatch
SB_EXPORT int SbCryptographyTransform(
    SbCryptographyTransformer transformer,
    const void* in_data,
    int in_data_size,
    void* out_data);

// Sets the initialization vector (IV) for a transformer, replacing the
// internally-set IV. The block cipher mode algorithm will update the IV
// appropriately after every block, so this is not necessary unless the stream
// is discontiguous in some way. This happens with AES-GCM in TLS.
//
// presubmit: allow sb_export mismatch
SB_EXPORT void SbCryptographySetInitializationVector(
    SbCryptographyTransformer transformer,
    const void* initialization_vector,
    int initialization_vector_size);

// Sets additional authenticated data (AAD) for a transformer, for chaining
// modes that support it (GCM). Returns whether the data was successfully
// set. This can fail if the chaining mode doesn't support AAD, if the
// parameters are invalid, or if the internal state is invalid for setting AAD.
//
// presubmit: allow sb_export mismatch
SB_EXPORT bool SbCryptographySetAuthenticatedData(
    SbCryptographyTransformer transformer,
    const void* data,
    int data_size);

// Calculates the authenticator tag for a transformer and places up to
// |out_tag_size| bytes of it in |out_tag|. Returns whether it was able to get
// the tag, which mainly has to do with whether it is compatible with the
// current block cipher mode.
//
// presubmit: allow sb_export mismatch
SB_EXPORT bool SbCryptographyGetTag(
    SbCryptographyTransformer transformer,
    void* out_tag,
    int out_tag_size);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SB_API_VERSION >= 12

#endif  // STARBOARD_CRYPTOGRAPHY_H_
