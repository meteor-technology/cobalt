// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_RENDERER_BACKEND_BLITTER_RENDER_TARGET_H_
#define COBALT_RENDERER_BACKEND_BLITTER_RENDER_TARGET_H_

#include "cobalt/renderer/backend/render_target.h"
#include "starboard/blitter.h"

#if SB_API_VERSION < 12 && SB_HAS(BLITTER)

namespace cobalt {
namespace renderer {
namespace backend {

class RenderTargetBlitter : public RenderTarget {
 public:
  virtual SbBlitterRenderTarget GetSbRenderTarget() const = 0;

  intptr_t GetPlatformHandle() const override {
    return reinterpret_cast<intptr_t>(SbBlitterRenderTarget());
  }

  virtual void Flip() = 0;

 protected:
  virtual ~RenderTargetBlitter() {}
};

}  // namespace backend
}  // namespace renderer
}  // namespace cobalt

#endif  // SB_API_VERSION < 12 && SB_HAS(BLITTER)

#endif  // COBALT_RENDERER_BACKEND_BLITTER_RENDER_TARGET_H_
