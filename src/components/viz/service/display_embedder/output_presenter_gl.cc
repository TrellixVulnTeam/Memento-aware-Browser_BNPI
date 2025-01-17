// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display_embedder/output_presenter_gl.h"

#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "components/viz/common/resources/resource_format_utils.h"
#include "components/viz/service/display_embedder/skia_output_surface_dependency.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/ipc/common/gpu_surface_lookup.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gl/gl_fence.h"
#include "ui/gl/gl_surface.h"

#if defined(OS_ANDROID)
#include "ui/gl/gl_surface_egl_surface_control.h"
#endif

namespace viz {

namespace {

class PresenterImageGL : public OutputPresenter::Image {
 public:
  PresenterImageGL() = default;
  ~PresenterImageGL() override = default;

  bool Initialize(gpu::SharedImageFactory* factory,
                  gpu::SharedImageRepresentationFactory* representation_factory,
                  const gfx::Size& size,
                  const gfx::ColorSpace& color_space,
                  ResourceFormat format,
                  SkiaOutputSurfaceDependency* deps,
                  uint32_t shared_image_usage);

  void BeginPresent() final;
  void EndPresent() final;
  int present_count() final;

  gl::GLImage* GetGLImage(std::unique_ptr<gfx::GpuFence>* fence);

 private:
  std::unique_ptr<gpu::SharedImageRepresentationOverlay>
      overlay_representation_;
  std::unique_ptr<gpu::SharedImageRepresentationGLTexture> gl_representation_;
  std::unique_ptr<gpu::SharedImageRepresentationOverlay::ScopedReadAccess>
      scoped_overlay_read_access_;
  std::unique_ptr<gpu::SharedImageRepresentationGLTexture::ScopedAccess>
      scoped_gl_read_access_;

  int present_count_ = 0;
};

bool PresenterImageGL::Initialize(
    gpu::SharedImageFactory* factory,
    gpu::SharedImageRepresentationFactory* representation_factory,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    ResourceFormat format,
    SkiaOutputSurfaceDependency* deps,
    uint32_t shared_image_usage) {
  auto mailbox = gpu::Mailbox::GenerateForSharedImage();

  if (!factory->CreateSharedImage(mailbox, format, size, color_space,
                                  deps->GetSurfaceHandle(),
                                  shared_image_usage)) {
    DLOG(ERROR) << "CreateSharedImage failed.";
    return false;
  }

  if (!Image::Initialize(factory, representation_factory, mailbox, deps))
    return false;

  overlay_representation_ = representation_factory->ProduceOverlay(mailbox);

  // If the backing doesn't support overlay, then fallback to GL.
  if (!overlay_representation_)
    gl_representation_ = representation_factory->ProduceGLTexture(mailbox);

  if (!overlay_representation_ && !gl_representation_) {
    DLOG(ERROR) << "ProduceOverlay() and ProduceGLTexture() failed.";
    return false;
  }

  return true;
}

void PresenterImageGL::BeginPresent() {
  if (++present_count_ != 1) {
    DCHECK(scoped_overlay_read_access_ || scoped_gl_read_access_);
    return;
  }

  DCHECK(!sk_surface());
  DCHECK(!scoped_overlay_read_access_);

  if (overlay_representation_) {
    scoped_overlay_read_access_ =
        overlay_representation_->BeginScopedReadAccess(
            true /* need_gl_image */);
    DCHECK(scoped_overlay_read_access_);
    return;
  }

  scoped_gl_read_access_ = gl_representation_->BeginScopedAccess(
      GL_SHARED_IMAGE_ACCESS_MODE_READ_CHROMIUM,
      gpu::SharedImageRepresentation::AllowUnclearedAccess::kNo);
  DCHECK(scoped_gl_read_access_);
}

void PresenterImageGL::EndPresent() {
  DCHECK(present_count_);
  if (--present_count_)
    return;
  scoped_overlay_read_access_.reset();
  scoped_gl_read_access_.reset();
}

int PresenterImageGL::present_count() {
  return present_count_;
}

gl::GLImage* PresenterImageGL::GetGLImage(
    std::unique_ptr<gfx::GpuFence>* fence) {
  if (scoped_overlay_read_access_)
    return scoped_overlay_read_access_->gl_image();

  DCHECK(scoped_gl_read_access_);

  if (gl::GLFence::IsGpuFenceSupported() && fence) {
    if (auto gl_fence = gl::GLFence::CreateForGpuFence())
      *fence = gl_fence->GetGpuFence();
  }
  auto* texture = gl_representation_->GetTexture();
  return texture->GetLevelImage(texture->target(), 0);
}

}  // namespace

// static
const uint32_t OutputPresenterGL::kDefaultSharedImageUsage =
    gpu::SHARED_IMAGE_USAGE_SCANOUT | gpu::SHARED_IMAGE_USAGE_DISPLAY |
    gpu::SHARED_IMAGE_USAGE_GLES2_FRAMEBUFFER_HINT;

// static
std::unique_ptr<OutputPresenterGL> OutputPresenterGL::Create(
    SkiaOutputSurfaceDependency* deps,
    gpu::MemoryTracker* memory_tracker) {
#if defined(OS_ANDROID)
  if (deps->GetGpuFeatureInfo()
          .status_values[gpu::GPU_FEATURE_TYPE_ANDROID_SURFACE_CONTROL] !=
      gpu::kGpuFeatureStatusEnabled) {
    return nullptr;
  }

  bool can_be_used_with_surface_control = false;
  ANativeWindow* window =
      gpu::GpuSurfaceLookup::GetInstance()->AcquireNativeWidget(
          deps->GetSurfaceHandle(), &can_be_used_with_surface_control);
  if (!window || !can_be_used_with_surface_control)
    return nullptr;
  // TODO(https://crbug.com/1012401): don't depend on GL.
  auto gl_surface = base::MakeRefCounted<gl::GLSurfaceEGLSurfaceControl>(
      window, base::ThreadTaskRunnerHandle::Get());
  if (!gl_surface->Initialize(gl::GLSurfaceFormat())) {
    LOG(ERROR) << "Failed to initialize GLSurfaceEGLSurfaceControl.";
    return nullptr;
  }

  if (!deps->GetSharedContextState()->MakeCurrent(gl_surface.get(),
                                                  true /* needs_gl*/)) {
    LOG(ERROR) << "MakeCurrent failed.";
    return nullptr;
  }

  return std::make_unique<OutputPresenterGL>(
      std::move(gl_surface), deps, memory_tracker, kDefaultSharedImageUsage);
#else
  return nullptr;
#endif
}

OutputPresenterGL::OutputPresenterGL(scoped_refptr<gl::GLSurface> gl_surface,
                                     SkiaOutputSurfaceDependency* deps,
                                     gpu::MemoryTracker* memory_tracker,
                                     uint32_t shared_image_usage)
    : gl_surface_(gl_surface),
      dependency_(deps),
      supports_async_swap_(gl_surface_->SupportsAsyncSwap()),
      shared_image_factory_(deps->GetGpuPreferences(),
                            deps->GetGpuDriverBugWorkarounds(),
                            deps->GetGpuFeatureInfo(),
                            deps->GetSharedContextState().get(),
                            deps->GetMailboxManager(),
                            deps->GetSharedImageManager(),
                            deps->GetGpuImageFactory(),
                            memory_tracker,
                            true),
      shared_image_representation_factory_(deps->GetSharedImageManager(),
                                           memory_tracker),
      shared_image_usage_(shared_image_usage) {
  // GL is origin is at bottom left normally, all Surfaceless implementations
  // are flipped.
  DCHECK_EQ(gl_surface_->GetOrigin(), gfx::SurfaceOrigin::kTopLeft);

  // TODO(https://crbug.com/958166): The initial |image_format_| should not be
  // used, and the gfx::BufferFormat specified in Reshape should be used
  // instead, because it may be updated to reflect changes in the content being
  // displayed (e.g, HDR content appearing on-screen).
#if defined(USE_OZONE)
  image_format_ = GetResourceFormat(display::DisplaySnapshot::PrimaryFormat());
#elif defined(OS_MACOSX)
  image_format_ = BGRA_8888;
#else
  image_format_ = RGBA_8888;
#endif
}

OutputPresenterGL::~OutputPresenterGL() = default;

void OutputPresenterGL::InitializeCapabilities(
    OutputSurface::Capabilities* capabilities) {
  capabilities->android_surface_control_feature_enabled = true;
  capabilities->supports_post_sub_buffer = gl_surface_->SupportsPostSubBuffer();
  capabilities->supports_commit_overlay_planes =
      gl_surface_->SupportsCommitOverlayPlanes();

  // Set supports_surfaceless to enable overlays.
  capabilities->supports_surfaceless = true;
  // We expect origin of buffers is at top left.
  capabilities->output_surface_origin = gfx::SurfaceOrigin::kTopLeft;

  // TODO(penghuang): Use defaultBackendFormat() in shared image implementation
  // to make sure backend format is consistent.
  capabilities->sk_color_type = ResourceFormatToClosestSkColorType(
      true /* gpu_compositing */, image_format_);
  capabilities->gr_backend_format =
      dependency_->GetSharedContextState()->gr_context()->defaultBackendFormat(
          capabilities->sk_color_type, GrRenderable::kYes);
}

bool OutputPresenterGL::Reshape(const gfx::Size& size,
                                float device_scale_factor,
                                const gfx::ColorSpace& color_space,
                                gfx::BufferFormat format,
                                gfx::OverlayTransform transform) {
  return gl_surface_->Resize(size, device_scale_factor, color_space,
                             gfx::AlphaBitsForBufferFormat(format));
}

std::vector<std::unique_ptr<OutputPresenter::Image>>
OutputPresenterGL::AllocateImages(gfx::ColorSpace color_space,
                                  gfx::Size image_size,
                                  size_t num_images) {
  std::vector<std::unique_ptr<Image>> images;
  for (size_t i = 0; i < num_images; ++i) {
    auto image = std::make_unique<PresenterImageGL>();
    if (!image->Initialize(&shared_image_factory_,
                           &shared_image_representation_factory_, image_size,
                           color_space, image_format_, dependency_,
                           shared_image_usage_)) {
      DLOG(ERROR) << "Failed to initialize image.";
      return {};
    }
    images.push_back(std::move(image));
  }

  return images;
}

void OutputPresenterGL::SwapBuffers(
    SwapCompletionCallback completion_callback,
    BufferPresentedCallback presentation_callback) {
  if (supports_async_swap_) {
    gl_surface_->SwapBuffersAsync(std::move(completion_callback),
                                  std::move(presentation_callback));
  } else {
    auto result = gl_surface_->SwapBuffers(std::move(presentation_callback));
    std::move(completion_callback).Run(gfx::SwapCompletionResult(result));
  }
}

void OutputPresenterGL::PostSubBuffer(
    const gfx::Rect& rect,
    SwapCompletionCallback completion_callback,
    BufferPresentedCallback presentation_callback) {
  if (supports_async_swap_) {
    gl_surface_->PostSubBufferAsync(
        rect.x(), rect.y(), rect.width(), rect.height(),
        std::move(completion_callback), std::move(presentation_callback));
  } else {
    auto result = gl_surface_->PostSubBuffer(rect.x(), rect.y(), rect.width(),
                                             rect.height(),
                                             std::move(presentation_callback));
    std::move(completion_callback).Run(gfx::SwapCompletionResult(result));
  }
}

void OutputPresenterGL::SchedulePrimaryPlane(
    const OverlayProcessorInterface::OutputSurfaceOverlayPlane& plane,
    Image* image,
    bool is_submitted) {
  std::unique_ptr<gfx::GpuFence> fence;
  // If the submitted_image() is being scheduled, we don't new a new fence.
  auto* gl_image = reinterpret_cast<PresenterImageGL*>(image)->GetGLImage(
      is_submitted ? nullptr : &fence);

  // Output surface is also z-order 0.
  constexpr int kPlaneZOrder = 0;
  // Output surface always uses the full texture.
  constexpr gfx::RectF kUVRect(0.f, 0.f, 1.0f, 1.0f);
  gl_surface_->ScheduleOverlayPlane(kPlaneZOrder, plane.transform, gl_image,
                                    ToNearestRect(plane.display_rect), kUVRect,
                                    plane.enable_blending, std::move(fence));
}

void OutputPresenterGL::CommitOverlayPlanes(
    SwapCompletionCallback completion_callback,
    BufferPresentedCallback presentation_callback,
    std::vector<ui::LatencyInfo> latency_info) {
  if (supports_async_swap_) {
    gl_surface_->CommitOverlayPlanesAsync(std::move(completion_callback),
                                          std::move(presentation_callback));
  } else {
    auto result =
        gl_surface_->CommitOverlayPlanes(std::move(presentation_callback));
    std::move(completion_callback).Run(gfx::SwapCompletionResult(result));
  }
}

std::vector<OutputPresenter::OverlayData> OutputPresenterGL::ScheduleOverlays(
    SkiaOutputSurface::OverlayList overlays) {
  std::vector<OverlayData> pending_overlays;
#if defined(OS_ANDROID) || defined(OS_MACOSX)
  // Note while reading through this for-loop that |overlay| has different
  // types on different platforms. On Android and Ozone it is an
  // OverlayCandidate, on Windows it is a DCLayerOverlay, and on macOS it is
  // a CALayerOverlay.
  for (auto& overlay : overlays) {
    // Extract the shared image and GLImage for the overlay. Note that for
    // solid color overlays, this will remain nullptr.
    gl::GLImage* gl_image = nullptr;
    if (overlay.mailbox.IsSharedImage()) {
      auto shared_image =
          shared_image_representation_factory_.ProduceOverlay(overlay.mailbox);
      // When display is re-opened, the first few frames might not have video
      // resource ready. Possible investigation crbug.com/1023971.
      if (!shared_image) {
        LOG(ERROR) << "Invalid mailbox.";
        continue;
      }

      auto shared_image_access =
          shared_image->BeginScopedReadAccess(true /* needs_gl_image */);
      if (!shared_image_access) {
        LOG(ERROR) << "Could not access SharedImage for read.";
        continue;
      }

      gl_image = shared_image_access->gl_image();
      DLOG_IF(ERROR, !gl_image) << "Cannot get GLImage.";

      pending_overlays.emplace_back(std::move(shared_image),
                                    std::move(shared_image_access));
    }

#if defined(OS_ANDROID)
    if (gl_image) {
      DCHECK(!overlay.gpu_fence_id);
      gl_surface_->ScheduleOverlayPlane(
          overlay.plane_z_order, overlay.transform, gl_image,
          ToNearestRect(overlay.display_rect), overlay.uv_rect,
          !overlay.is_opaque, nullptr /* gpu_fence */);
    }
#elif defined(OS_MACOSX)
    gl_surface_->ScheduleCALayer(ui::CARendererLayerParams(
        overlay.shared_state->is_clipped,
        gfx::ToEnclosingRect(overlay.shared_state->clip_rect),
        overlay.shared_state->rounded_corner_bounds,
        overlay.shared_state->sorting_context_id,
        gfx::Transform(overlay.shared_state->transform), gl_image,
        overlay.contents_rect, gfx::ToEnclosingRect(overlay.bounds_rect),
        overlay.background_color, overlay.edge_aa_mask,
        overlay.shared_state->opacity, overlay.filter));
#endif
  }
#endif  //  defined(OS_ANDROID) || defined(OS_MACOSX)

  return pending_overlays;
}

}  // namespace viz
