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

#ifndef COBALT_RENDERER_BACKEND_BLITTER_SURFACE_RENDER_TARGET_H_
#define COBALT_RENDERER_BACKEND_BLITTER_SURFACE_RENDER_TARGET_H_

#include "cobalt/math/size.h"
#include "cobalt/renderer/backend/blitter/render_target.h"
#include "starboard/blitter.h"

#if SB_API_VERSION < 12 && SB_HAS(BLITTER)

namespace cobalt {
namespace renderer {
namespace backend {

class SurfaceRenderTargetBlitter : public RenderTargetBlitter {
 public:
  SurfaceRenderTargetBlitter(SbBlitterDevice device,
                             const math::Size& dimensions);

  const math::Size& GetSize() const override;

  SbBlitterRenderTarget GetSbRenderTarget() const override;

  SbBlitterSurface GetSbSurface() const { return surface_; }

  // Returns and gives up ownership of the surface. After this is called, the
  // SurfaceRenderTargetBlitter's internal surface will be invalid and so the
  // object itself becomes invalid.
  SbBlitterSurface TakeSbSurface() {
    SbBlitterSurface original_surface_ = surface_;
    surface_ = kSbBlitterInvalidSurface;
    return original_surface_;
  }

  void Flip() override {}

  bool CreationError() override { return !SbBlitterIsSurfaceValid(surface_); }

 private:
  ~SurfaceRenderTargetBlitter() override;

  SbBlitterSurface surface_;
  SbBlitterRenderTarget render_target_;

  math::Size size_;
};

}  // namespace backend
}  // namespace renderer
}  // namespace cobalt

#endif  // SB_API_VERSION < 12 && SB_HAS(BLITTER)

#endif  // COBALT_RENDERER_BACKEND_BLITTER_SURFACE_RENDER_TARGET_H_
