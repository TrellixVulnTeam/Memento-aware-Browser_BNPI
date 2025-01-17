// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_OUTPUT_PRESENTER_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_OUTPUT_PRESENTER_H_

#include "components/viz/service/display/output_surface.h"
#include "components/viz/service/display/overlay_processor_interface.h"
#include "components/viz/service/display/skia_output_surface.h"
#include "components/viz/service/viz_service_export.h"
#include "gpu/command_buffer/service/shared_image_factory.h"
#include "ui/gfx/presentation_feedback.h"
#include "ui/gfx/swap_result.h"

namespace viz {

class SkiaOutputSurfaceDependency;

class VIZ_SERVICE_EXPORT OutputPresenter {
 public:
  class Image {
   public:
    Image();
    virtual ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    bool Initialize(
        gpu::SharedImageFactory* factory,
        gpu::SharedImageRepresentationFactory* representation_factory,
        const gpu::Mailbox& mailbox,
        SkiaOutputSurfaceDependency* deps);

    gpu::SharedImageRepresentationSkia* skia_representation() {
      return skia_representation_.get();
    }

    void BeginWriteSkia();
    SkSurface* sk_surface();
    std::vector<GrBackendSemaphore> TakeEndWriteSkiaSemaphores();
    void EndWriteSkia();

    virtual void BeginPresent() = 0;
    virtual void EndPresent() = 0;
    virtual int present_count() = 0;

    base::WeakPtr<Image> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

   private:
    base::ScopedClosureRunner shared_image_deleter_;
    std::unique_ptr<gpu::SharedImageRepresentationSkia> skia_representation_;
    std::unique_ptr<gpu::SharedImageRepresentationSkia::ScopedWriteAccess>
        scoped_skia_write_access_;

    std::vector<GrBackendSemaphore> end_semaphores_;
    base::WeakPtrFactory<Image> weak_ptr_factory_{this};
  };

  class OverlayData {
   public:
    OverlayData(
        std::unique_ptr<gpu::SharedImageRepresentationOverlay> representation,
        std::unique_ptr<gpu::SharedImageRepresentationOverlay::ScopedReadAccess>
            scoped_read_access);
    OverlayData(OverlayData&&);
    ~OverlayData();
    OverlayData& operator=(OverlayData&&);

   private:
    std::unique_ptr<gpu::SharedImageRepresentationOverlay> representation_;
    std::unique_ptr<gpu::SharedImageRepresentationOverlay::ScopedReadAccess>
        scoped_read_access_;
  };

  OutputPresenter() = default;
  virtual ~OutputPresenter() = default;

  using BufferPresentedCallback =
      base::OnceCallback<void(const gfx::PresentationFeedback& feedback)>;
  using SwapCompletionCallback =
      base::OnceCallback<void(gfx::SwapCompletionResult)>;

  virtual void InitializeCapabilities(
      OutputSurface::Capabilities* capabilities) = 0;
  virtual bool Reshape(const gfx::Size& size,
                       float device_scale_factor,
                       const gfx::ColorSpace& color_space,
                       gfx::BufferFormat format,
                       gfx::OverlayTransform transform) = 0;
  virtual std::vector<std::unique_ptr<Image>> AllocateImages(
      gfx::ColorSpace color_space,
      gfx::Size image_size,
      size_t num_images) = 0;
  virtual void SwapBuffers(SwapCompletionCallback completion_callback,
                           BufferPresentedCallback presentation_callback) = 0;
  virtual void PostSubBuffer(const gfx::Rect& rect,
                             SwapCompletionCallback completion_callback,
                             BufferPresentedCallback presentation_callback) = 0;
  virtual void CommitOverlayPlanes(
      SwapCompletionCallback completion_callback,
      BufferPresentedCallback presentation_callback,
      std::vector<ui::LatencyInfo> latency_info) = 0;
  virtual void SchedulePrimaryPlane(
      const OverlayProcessorInterface::OutputSurfaceOverlayPlane& plane,
      Image* image,
      bool is_submitted) = 0;
  virtual std::vector<OverlayData> ScheduleOverlays(
      SkiaOutputSurface::OverlayList overlays) = 0;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_EMBEDDER_OUTPUT_PRESENTER_H_
