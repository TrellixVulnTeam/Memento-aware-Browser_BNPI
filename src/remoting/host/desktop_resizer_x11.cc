// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/desktop_resizer.h"

#include <string.h>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/ranges.h"
#include "remoting/base/logging.h"
#include "remoting/host/linux/x11_util.h"
#include "ui/gfx/x/randr.h"
#include "ui/gfx/x/x11.h"

// On Linux, we use the xrandr extension to change the desktop resolution. In
// curtain mode, we do exact resize where supported (currently only using a
// patched Xvfb server). Otherwise, we try to pick the best resolution from the
// existing modes.
//
// Xrandr has a number of restrictions that make exact resize more complex:
//
//   1. It's not possible to change the resolution of an existing mode. Instead,
//      the mode must be deleted and recreated.
//   2. It's not possible to delete a mode that's in use.
//   3. Errors are communicated via Xlib's spectacularly unhelpful mechanism
//      of terminating the process unless you install an error handler.
//
// The basic approach is as follows:
//
//   1. Create a new mode with the correct resolution;
//   2. Switch to the new mode;
//   3. Delete the old mode.
//
// Since the new mode must have a different name, and we want the current mode
// name to be consistent, we then additionally:
//
//   4. Recreate the old mode at the new resolution;
//   5. Switch to the old mode;
//   6. Delete the temporary mode.
//
// Name consistency will allow a future CL to disable resize-to-client if the
// user has changed the mode to something other than "Chrome Remote Desktop
// client resolution". It doesn't make the code significantly more complex.

namespace {

constexpr auto kInvalidMode = static_cast<x11::RandR::Mode>(0);

int PixelsToMillimeters(int pixels, int dpi) {
  DCHECK(dpi != 0);

  const double kMillimetersPerInch = 25.4;

  // (pixels / dpi) is the length in inches. Multiplying by
  // kMillimetersPerInch converts to mm. Multiplication is done first to
  // avoid integer division.
  return static_cast<int>(kMillimetersPerInch * pixels / dpi);
}

// TODO(jamiewalch): Use the correct DPI for the mode: http://crbug.com/172405.
const int kDefaultDPI = 96;

}  // namespace

namespace remoting {

// Wrapper class for the XRRScreenResources struct.
class ScreenResources {
 public:
  ScreenResources() = default;

  ~ScreenResources() = default;

  bool Refresh(x11::RandR* randr, x11::Window window) {
    resources_ = nullptr;
    if (auto response = randr->GetScreenResourcesCurrent({window}).Sync())
      resources_ = std::move(response.reply);
    return resources_ != nullptr;
  }

  x11::RandR::Mode GetIdForMode(const std::string& name) {
    CHECK(resources_);
    const char* names = reinterpret_cast<const char*>(resources_->names.data());
    for (const auto& mode_info : resources_->modes) {
      std::string mode_name(names, mode_info.name_len);
      names += mode_info.name_len;
      if (name == mode_name)
        return static_cast<x11::RandR::Mode>(mode_info.id);
    }
    return kInvalidMode;
  }

  // For now, assume we're only ever interested in the first output.
  x11::RandR::Output GetOutput() {
    CHECK(resources_);
    return resources_->outputs[0];
  }

  // For now, assume we're only ever interested in the first crtc.
  x11::RandR::Crtc GetCrtc() {
    CHECK(resources_);
    return resources_->crtcs[0];
  }

  x11::RandR::GetScreenResourcesCurrentReply* get() { return resources_.get(); }

 private:
  std::unique_ptr<x11::RandR::GetScreenResourcesCurrentReply> resources_;
};

class DesktopResizerX11 : public DesktopResizer {
 public:
  DesktopResizerX11();
  ~DesktopResizerX11() override;

  // DesktopResizer interface
  ScreenResolution GetCurrentResolution() override;
  std::list<ScreenResolution> GetSupportedResolutions(
      const ScreenResolution& preferred) override;
  void SetResolution(const ScreenResolution& resolution) override;
  void RestoreResolution(const ScreenResolution& original) override;

 private:
  // Add a mode matching the specified resolution and switch to it.
  void SetResolutionNewMode(const ScreenResolution& resolution);

  // Attempt to switch to an existing mode matching the specified resolution
  // using RandR, if such a resolution exists. Otherwise, do nothing.
  void SetResolutionExistingMode(const ScreenResolution& resolution);

  // Create a mode, and attach it to the primary output. If the mode already
  // exists, it is left unchanged.
  void CreateMode(const char* name, int width, int height);

  // Remove the specified mode from the primary output, and delete it. If the
  // mode is in use, it is not deleted.
  void DeleteMode(const char* name);

  // Switch the primary output to the specified mode. If name is nullptr, the
  // primary output is disabled instead, which is required before changing
  // its resolution.
  void SwitchToMode(const char* name);

  x11::Connection connection_;
  x11::RandR* const randr_ = nullptr;
  const x11::Screen* const screen_ = nullptr;
  x11::Window root_;
  ScreenResources resources_;
  bool exact_resize_;
  bool has_randr_;

  DISALLOW_COPY_AND_ASSIGN(DesktopResizerX11);
};

DesktopResizerX11::DesktopResizerX11()
    : randr_(&connection_.randr()),
      screen_(&connection_.default_screen()),
      root_(screen_->root),
      exact_resize_(base::CommandLine::ForCurrentProcess()->HasSwitch(
          "server-supports-exact-resize")) {
  has_randr_ = randr_->present();
  if (has_randr_)
    randr_->SelectInput({root_, x11::RandR::NotifyMask::ScreenChange});
}

DesktopResizerX11::~DesktopResizerX11() = default;

ScreenResolution DesktopResizerX11::GetCurrentResolution() {
  // Xrandr requires that we process RRScreenChangeNotify events, otherwise
  // DisplayWidth and DisplayHeight do not return the current values. Normally,
  // this would be done via a central X event loop, but we don't have one, hence
  // this horrible hack.
  //
  // Note that the WatchFileDescriptor approach taken in XServerClipboard
  // doesn't work here because resize events have already been read from the
  // X server socket by the time the resize function returns, hence the
  // file descriptor is never seen as readable.
  if (has_randr_) {
    while (XEventsQueued(connection_.display(), QueuedAlready)) {
      XEvent event;
      XNextEvent(connection_.display(), &event);
      XRRUpdateConfiguration(&event);
    }
  }

  ScreenResolution result(
      webrtc::DesktopSize(DisplayWidth(connection_.display(),
                                       DefaultScreen(connection_.display())),
                          DisplayHeight(connection_.display(),
                                        DefaultScreen(connection_.display()))),
      webrtc::DesktopVector(kDefaultDPI, kDefaultDPI));
  return result;
}

std::list<ScreenResolution> DesktopResizerX11::GetSupportedResolutions(
    const ScreenResolution& preferred) {
  std::list<ScreenResolution> result;
  if (!has_randr_)
    return result;
  if (exact_resize_) {
    // Clamp the specified size to something valid for the X server.
    if (auto response = randr_->GetScreenSizeRange({root_}).Sync()) {
      int width = base::ClampToRange(
          static_cast<uint16_t>(preferred.dimensions().width()),
          response->min_width, response->max_width);
      int height = base::ClampToRange(
          static_cast<uint16_t>(preferred.dimensions().height()),
          response->min_height, response->max_height);
      // Additionally impose a minimum size of 640x480, since anything smaller
      // doesn't seem very useful.
      ScreenResolution actual(
          webrtc::DesktopSize(std::max(640, width), std::max(480, height)),
          webrtc::DesktopVector(kDefaultDPI, kDefaultDPI));
      result.push_back(actual);
    }
  } else {
    // Retrieve supported resolutions with RandR
    if (auto response = randr_->GetScreenInfo({root_}).Sync()) {
      for (const auto& size : response->sizes) {
        result.emplace_back(webrtc::DesktopSize(size.width, size.height),
                            webrtc::DesktopVector(kDefaultDPI, kDefaultDPI));
      }
    }
  }
  return result;
}

void DesktopResizerX11::SetResolution(const ScreenResolution& resolution) {
  if (!has_randr_)
    return;

  // Ignore X errors encountered while resizing the display. We might hit an
  // error, for example if xrandr has been used to add a mode with the same
  // name as our temporary mode, or to remove the "client resolution" mode. We
  // don't want to terminate the process if this happens.
  ScopedXErrorHandler handler(ScopedXErrorHandler::Ignore());

  // Grab the X server while we're changing the display resolution. This ensures
  // that the display configuration doesn't change under our feet.
  ScopedXGrabServer grabber(connection_.display());

  if (exact_resize_)
    SetResolutionNewMode(resolution);
  else
    SetResolutionExistingMode(resolution);
}

void DesktopResizerX11::RestoreResolution(const ScreenResolution& original) {
  SetResolution(original);
}

void DesktopResizerX11::SetResolutionNewMode(
    const ScreenResolution& resolution) {
  // The name of the mode representing the current client view resolution and
  // the temporary mode used for the reasons described at the top of this file.
  // The former should be localized if it's user-visible; the latter only
  // exists briefly and does not need to localized.
  const char* kModeName = "Chrome Remote Desktop client resolution";
  const char* kTempModeName = "Chrome Remote Desktop temporary mode";

  // Actually do the resize operation, preserving the current mode name. Note
  // that we have to detach the output from any mode in order to resize it
  // (strictly speaking, this is only required when reducing the size, but it
  // seems safe to do it regardless).
  HOST_LOG << "Changing desktop size to " << resolution.dimensions().width()
           << "x" << resolution.dimensions().height();

  // TODO(lambroslambrou): Use the DPI from client size information.
  int width_mm =
      PixelsToMillimeters(resolution.dimensions().width(), kDefaultDPI);
  int height_mm =
      PixelsToMillimeters(resolution.dimensions().height(), kDefaultDPI);
  CreateMode(kTempModeName, resolution.dimensions().width(),
             resolution.dimensions().height());
  SwitchToMode(nullptr);
  randr_->SetScreenSize({root_, resolution.dimensions().width(),
                         resolution.dimensions().height(), width_mm,
                         height_mm});
  SwitchToMode(kTempModeName);
  DeleteMode(kModeName);
  CreateMode(kModeName, resolution.dimensions().width(),
             resolution.dimensions().height());
  SwitchToMode(kModeName);
  DeleteMode(kTempModeName);
}

void DesktopResizerX11::SetResolutionExistingMode(
    const ScreenResolution& resolution) {
  if (auto config = randr_->GetScreenInfo({root_}).Sync()) {
    x11::RandR::Rotation current_rotation = config->rotation;
    const std::vector<x11::RandR::ScreenSize>& sizes = config->sizes;
    for (size_t i = 0; i < sizes.size(); ++i) {
      if (sizes[i].width == resolution.dimensions().width() &&
          sizes[i].height == resolution.dimensions().height()) {
        randr_->SetScreenConfig({
            .window = root_,
            .timestamp = x11::Time::CurrentTime,
            .config_timestamp = config->config_timestamp,
            .sizeID = i,
            .rotation = current_rotation,
            .rate = 0,
        });
        break;
      }
    }
  }
}

void DesktopResizerX11::CreateMode(const char* name, int width, int height) {
  x11::RandR::ModeInfo mode;
  mode.width = width;
  mode.height = height;
  mode.name_len = strlen(name);
  randr_->CreateMode({root_, mode, name});

  if (!resources_.Refresh(randr_, root_))
    return;
  x11::RandR::Mode mode_id = resources_.GetIdForMode(name);
  if (mode_id == kInvalidMode)
    return;
  randr_->AddOutputMode({
      resources_.GetOutput(),
      mode_id,
  });
}

void DesktopResizerX11::DeleteMode(const char* name) {
  x11::RandR::Mode mode_id = resources_.GetIdForMode(name);
  if (mode_id != kInvalidMode) {
    randr_->DeleteOutputMode({resources_.GetOutput(), mode_id});
    randr_->DestroyMode({mode_id});
    resources_.Refresh(randr_, root_);
  }
}

void DesktopResizerX11::SwitchToMode(const char* name) {
  auto mode_id = kInvalidMode;
  std::vector<x11::RandR::Output> outputs;
  if (name) {
    mode_id = resources_.GetIdForMode(name);
    CHECK_NE(mode_id, kInvalidMode);
    outputs = resources_.get()->outputs;
  }
  const auto* resources = resources_.get();
  randr_->SetCrtcConfig({
      .crtc = resources_.GetCrtc(),
      .timestamp = x11::Time::CurrentTime,
      .config_timestamp = resources->config_timestamp,
      .x = 0,
      .y = 0,
      .mode = mode_id,
      .rotation = x11::RandR::Rotation::Rotate_0,
      .outputs = outputs,
  });
}

std::unique_ptr<DesktopResizer> DesktopResizer::Create() {
  return base::WrapUnique(new DesktopResizerX11);
}

}  // namespace remoting
