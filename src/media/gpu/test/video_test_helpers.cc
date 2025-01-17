// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/test/video_test_helpers.h"

#include <limits>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "base/sys_byteorder.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_frame_layout.h"
#include "media/gpu/test/video.h"
#include "media/video/h264_parser.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace test {
namespace {
constexpr uint16_t kIvfFileHeaderSize = 32;
constexpr size_t kIvfFrameHeaderSize = 12;
}  // namespace

IvfFileHeader GetIvfFileHeader(const base::span<const uint8_t>& data) {
  LOG_ASSERT(data.size_bytes() == 32u);
  IvfFileHeader file_header;
  memcpy(&file_header, data.data(), sizeof(IvfFileHeader));
  file_header.ByteSwap();
  return file_header;
}

IvfFrameHeader GetIvfFrameHeader(const base::span<const uint8_t>& data) {
  LOG_ASSERT(data.size_bytes() == 12u);
  IvfFrameHeader frame_header{};
  memcpy(&frame_header.frame_size, data.data(), 4);
  memcpy(&frame_header.timestamp, &data[4], 8);
  return frame_header;
}

IvfWriter::IvfWriter(base::FilePath output_filepath) {
  output_file_ = base::File(
      output_filepath, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  LOG_ASSERT(output_file_.IsValid());
}

bool IvfWriter::WriteFileHeader(VideoCodec codec,
                                const gfx::Size& resolution,
                                uint32_t frame_rate,
                                uint32_t num_frames) {
  char ivf_header[kIvfFileHeaderSize] = {};
  // Bytes 0-3 of an IVF file header always contain the signature 'DKIF'.
  strcpy(&ivf_header[0], "DKIF");
  constexpr uint16_t kVersion = 0;
  auto write16 = [&ivf_header](int i, uint16_t v) {
    memcpy(&ivf_header[i], &v, sizeof(v));
  };
  auto write32 = [&ivf_header](int i, uint32_t v) {
    memcpy(&ivf_header[i], &v, sizeof(v));
  };

  write16(4, kVersion);
  write16(6, kIvfFileHeaderSize);
  switch (codec) {
    case kCodecVP8:
      strcpy(&ivf_header[8], "VP80");
      break;
    case kCodecVP9:
      strcpy(&ivf_header[8], "VP90");
      break;
    default:
      LOG(ERROR) << "Unknown codec: " << GetCodecName(codec);
      return false;
  }

  write16(12, resolution.width());
  write16(14, resolution.height());
  write32(16, frame_rate);
  write32(20, 1);
  write32(24, num_frames);
  // Reserved.
  write32(28, 0);
  return output_file_.WriteAtCurrentPos(ivf_header, kIvfFileHeaderSize) ==
         static_cast<int>(kIvfFileHeaderSize);
}

bool IvfWriter::WriteFrame(uint32_t data_size,
                           uint64_t timestamp,
                           const uint8_t* data) {
  char ivf_frame_header[kIvfFrameHeaderSize] = {};
  memcpy(&ivf_frame_header[0], &data_size, sizeof(data_size));
  memcpy(&ivf_frame_header[4], &timestamp, sizeof(timestamp));
  bool success =
      output_file_.WriteAtCurrentPos(ivf_frame_header, kIvfFrameHeaderSize) ==
          static_cast<int>(kIvfFrameHeaderSize) &&
      output_file_.WriteAtCurrentPos(reinterpret_cast<const char*>(data),
                                     data_size) == static_cast<int>(data_size);
  return success;
}

EncodedDataHelper::EncodedDataHelper(const std::vector<uint8_t>& stream,
                                     VideoCodecProfile profile)
    : data_(std::string(reinterpret_cast<const char*>(stream.data()),
                        stream.size())),
      profile_(profile) {}

EncodedDataHelper::~EncodedDataHelper() {
  base::STLClearObject(&data_);
}

bool EncodedDataHelper::IsNALHeader(const std::string& data, size_t pos) {
  return data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0 &&
         data[pos + 3] == 1;
}

scoped_refptr<DecoderBuffer> EncodedDataHelper::GetNextBuffer() {
  switch (VideoCodecProfileToVideoCodec(profile_)) {
    case kCodecH264:
      return GetNextFragment();
    case kCodecVP8:
    case kCodecVP9:
      return GetNextFrame();
    default:
      NOTREACHED();
      return nullptr;
  }
}

scoped_refptr<DecoderBuffer> EncodedDataHelper::GetNextFragment() {
  if (next_pos_to_decode_ == 0) {
    size_t skipped_fragments_count = 0;
    if (!LookForSPS(&skipped_fragments_count)) {
      next_pos_to_decode_ = 0;
      return nullptr;
    }
    num_skipped_fragments_ += skipped_fragments_count;
  }

  size_t start_pos = next_pos_to_decode_;
  size_t next_nalu_pos = GetBytesForNextNALU(start_pos);

  // Update next_pos_to_decode_.
  next_pos_to_decode_ = next_nalu_pos;
  return DecoderBuffer::CopyFrom(
      reinterpret_cast<const uint8_t*>(&data_[start_pos]),
      next_nalu_pos - start_pos);
}

size_t EncodedDataHelper::GetBytesForNextNALU(size_t start_pos) {
  size_t pos = start_pos;
  if (pos + 4 > data_.size())
    return pos;
  if (!IsNALHeader(data_, pos)) {
    ADD_FAILURE();
    return std::numeric_limits<std::size_t>::max();
  }
  pos += 4;
  while (pos + 4 <= data_.size() && !IsNALHeader(data_, pos)) {
    ++pos;
  }
  if (pos + 3 >= data_.size())
    pos = data_.size();
  return pos;
}

bool EncodedDataHelper::LookForSPS(size_t* skipped_fragments_count) {
  *skipped_fragments_count = 0;
  while (next_pos_to_decode_ + 4 < data_.size()) {
    if ((data_[next_pos_to_decode_ + 4] & 0x1f) == 0x7) {
      return true;
    }
    *skipped_fragments_count += 1;
    next_pos_to_decode_ = GetBytesForNextNALU(next_pos_to_decode_);
  }
  return false;
}

scoped_refptr<DecoderBuffer> EncodedDataHelper::GetNextFrame() {
  // Helpful description: http://wiki.multimedia.cx/index.php?title=IVF
  // Only IVF video files are supported. The first 4bytes of an IVF video file's
  // header should be "DKIF".
  if (next_pos_to_decode_ == 0) {
    if (data_.size() < kIvfFileHeaderSize) {
      LOG(ERROR) << "data is too small";
      return nullptr;
    }
    auto ivf_header = GetIvfFileHeader(base::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&data_[0]), kIvfFileHeaderSize));
    if (strncmp(ivf_header.signature, "DKIF", 4) != 0) {
      LOG(ERROR) << "Unexpected data encountered while parsing IVF header";
      return nullptr;
    }
    next_pos_to_decode_ = kIvfFileHeaderSize;  // Skip IVF header.
  }

  // Group IVF data whose timestamps are the same. Spatial layers in a
  // spatial-SVC stream may separately be stored in IVF data, where the
  // timestamps of the IVF frame headers are the same. However, it is necessary
  // for VD(A) to feed the spatial layers by a single DecoderBuffer. So this
  // grouping is required.
  std::vector<IvfFrame> ivf_frames;
  while (!ReachEndOfStream()) {
    auto frame_header = GetNextIvfFrameHeader();
    if (!frame_header)
      return nullptr;

    // Timestamp is different from the current one. The next IVF data must be
    // grouped in the next group.
    if (!ivf_frames.empty() &&
        frame_header->timestamp != ivf_frames[0].header.timestamp) {
      break;
    }

    auto frame_data = ReadNextIvfFrame();
    if (!frame_data)
      return nullptr;

    ivf_frames.push_back(*frame_data);
  }

  if (ivf_frames.empty()) {
    LOG(ERROR) << "No IVF frame is available";
    return nullptr;
  }

  // Standard stream case.
  if (ivf_frames.size() == 1) {
    return DecoderBuffer::CopyFrom(
        reinterpret_cast<const uint8_t*>(ivf_frames[0].data),
        ivf_frames[0].header.frame_size);
  }

  if (ivf_frames.size() > 3) {
    LOG(ERROR) << "Number of IVF frames with same timestamps exceeds maximum of"
               << "3: ivf_frames.size()=" << ivf_frames.size();
    return nullptr;
  }

  std::string data;
  std::vector<uint32_t> frame_sizes;
  frame_sizes.reserve(ivf_frames.size());
  for (const IvfFrame& ivf : ivf_frames) {
    data.append(reinterpret_cast<char*>(ivf.data), ivf.header.frame_size);
    frame_sizes.push_back(ivf.header.frame_size);
  }

  // Copy frame_sizes information to DecoderBuffer's side data. Since side_data
  // is uint8_t*, we need to copy as uint8_t from uint32_t. The copied data is
  // recognized as uint32_t in VD(A).
  const uint8_t* side_data =
      reinterpret_cast<const uint8_t*>(frame_sizes.data());
  size_t side_data_size =
      frame_sizes.size() * sizeof(uint32_t) / sizeof(uint8_t);

  return DecoderBuffer::CopyFrom(reinterpret_cast<const uint8_t*>(data.data()),
                                 data.size(), side_data, side_data_size);
}

base::Optional<IvfFrameHeader> EncodedDataHelper::GetNextIvfFrameHeader()
    const {
  const size_t pos = next_pos_to_decode_;
  // Read VP8/9 frame size from IVF header.
  if (pos + kIvfFrameHeaderSize > data_.size()) {
    LOG(ERROR) << "Unexpected data encountered while parsing IVF frame header";
    return base::nullopt;
  }
  return GetIvfFrameHeader(base::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(data_[pos]), kIvfFrameHeaderSize));
}

base::Optional<IvfFrame> EncodedDataHelper::ReadNextIvfFrame() {
  auto frame_header = GetNextIvfFrameHeader();
  if (!frame_header)
    return base::nullopt;

  // Skip IVF frame header.
  const size_t pos = next_pos_to_decode_ + kIvfFrameHeaderSize;

  // Make sure we are not reading out of bounds.
  if (pos + frame_header->frame_size > data_.size()) {
    LOG(ERROR) << "Unexpected data encountered while parsing IVF frame header";
    next_pos_to_decode_ = data_.size();
    return base::nullopt;
  }

  // Update next_pos_to_decode_.
  next_pos_to_decode_ = pos + frame_header->frame_size;

  return IvfFrame{*frame_header, reinterpret_cast<uint8_t*>(&data_[pos])};
}

// static
bool EncodedDataHelper::HasConfigInfo(const uint8_t* data,
                                      size_t size,
                                      VideoCodecProfile profile) {
  if (profile >= H264PROFILE_MIN && profile <= H264PROFILE_MAX) {
    H264Parser parser;
    parser.SetStream(data, size);
    H264NALU nalu;
    H264Parser::Result result = parser.AdvanceToNextNALU(&nalu);
    if (result != H264Parser::kOk) {
      // Let the VDA figure out there's something wrong with the stream.
      return false;
    }

    return nalu.nal_unit_type == H264NALU::kSPS;
  } else if (profile >= VP8PROFILE_MIN && profile <= VP9PROFILE_MAX) {
    return (size > 0 && !(data[0] & 0x01));
  }
  // Shouldn't happen at this point.
  LOG(FATAL) << "Invalid profile: " << GetProfileName(profile);
  return false;
}

AlignedDataHelper::AlignedDataHelper(const std::vector<uint8_t>& stream,
                                     uint32_t num_frames,
                                     VideoPixelFormat pixel_format,
                                     const gfx::Rect& visible_area,
                                     const gfx::Size& coded_size)
    : num_frames_(num_frames),
      pixel_format_(pixel_format),
      visible_area_(visible_area),
      coded_size_(coded_size) {
  // TODO(b/150257482): Rather than aligning the video stream data here, we
  // could directly create a vector of aligned video frames.
  CreateAlignedInputStream(stream);
}

AlignedDataHelper::~AlignedDataHelper() {}

scoped_refptr<VideoFrame> AlignedDataHelper::GetNextFrame() {
  size_t num_planes = VideoFrame::NumPlanes(pixel_format_);
  CHECK_LE(num_planes, 3u);

  uint8_t* frame_data[3] = {};
  std::vector<ColorPlaneLayout> planes(num_planes);
  size_t offset = data_pos_;

  for (size_t i = 0; i < num_planes; i++) {
    frame_data[i] = reinterpret_cast<uint8_t*>(&aligned_data_[0]) + offset;
    planes[i].stride =
        VideoFrame::RowBytes(i, pixel_format_, coded_size_.width());
    planes[i].offset = offset;
    planes[i].size = aligned_plane_size_[i];
    offset += aligned_plane_size_[i];
  }

  auto layout = VideoFrameLayout::CreateWithPlanes(pixel_format_, coded_size_,
                                                   std::move(planes));
  if (!layout) {
    LOG(ERROR) << "Failed to create VideoFrameLayout";
    return nullptr;
  }

  // TODO(crbug.com/1045825): Investigate use of MOJO_SHARED_BUFFER, similar to
  // changes made in crrev.com/c/2050895.
  scoped_refptr<VideoFrame> video_frame =
      VideoFrame::WrapExternalYuvDataWithLayout(
          *layout, visible_area_, visible_area_.size(), frame_data[0],
          frame_data[1], frame_data[2], base::TimeTicks::Now().since_origin());

  data_pos_ += static_cast<off_t>(aligned_frame_size_);
  DCHECK_LE(data_pos_, aligned_data_.size());

  EXPECT_NE(nullptr, video_frame.get());
  return video_frame;
}

void AlignedDataHelper::Rewind() {
  data_pos_ = 0;
}

bool AlignedDataHelper::AtHeadOfStream() const {
  return data_pos_ == 0;
}

bool AlignedDataHelper::AtEndOfStream() const {
  return data_pos_ == aligned_data_.size();
}

void AlignedDataHelper::CreateAlignedInputStream(
    const std::vector<uint8_t>& stream) {
  ASSERT_NE(pixel_format_, PIXEL_FORMAT_UNKNOWN);
  size_t num_planes = VideoFrame::NumPlanes(pixel_format_);
  std::vector<size_t> coded_bpl(num_planes);
  std::vector<size_t> visible_bpl(num_planes);
  std::vector<size_t> visible_plane_rows(num_planes);

  // Calculate padding in bytes to be added after each plane required to keep
  // starting addresses of all planes at a byte boundary required by the
  // platform. This padding will be added after each plane when copying to the
  // temporary file.
  // At the same time we also need to take into account coded_size requested by
  // the VEA; each row of visible_bpl bytes in the original file needs to be
  // copied into a row of coded_bpl bytes in the aligned file.
  for (size_t i = 0; i < num_planes; i++) {
    coded_bpl[i] = VideoFrame::RowBytes(i, pixel_format_, coded_size_.width());
    visible_bpl[i] =
        VideoFrame::RowBytes(i, pixel_format_, visible_area_.width());
    visible_plane_rows[i] =
        VideoFrame::Rows(i, pixel_format_, visible_area_.height());
    size_t coded_area_size =
        coded_bpl[i] * VideoFrame::Rows(i, pixel_format_, coded_size_.height());
    const size_t aligned_size = AlignToPlatformRequirements(coded_area_size);
    aligned_plane_size_.push_back(aligned_size);
    aligned_frame_size_ += aligned_size;
  }

  // NOTE: VideoFrame::AllocationSize() cannot used here because the width and
  // height on each plane is aligned by 2 for YUV format.
  size_t frame_buffer_size = 0;
  for (size_t i = 0; i < num_planes; ++i) {
    size_t row_bytes =
        VideoFrame::RowBytes(i, pixel_format_, visible_area_.width());
    size_t rows = VideoFrame::Rows(i, pixel_format_, visible_area_.height());
    frame_buffer_size += rows * row_bytes;
  }

  LOG_ASSERT(stream.size() % frame_buffer_size == 0U)
      << "Stream byte size is not a product of calculated frame byte size";

  LOG_ASSERT(aligned_frame_size_ > 0UL);
  aligned_data_.resize(aligned_frame_size_ * num_frames_);

  off_t src_offset = 0;
  off_t dest_offset = 0;
  for (size_t frame = 0; frame < num_frames_; frame++) {
    const char* src_ptr = reinterpret_cast<const char*>(&stream[src_offset]);

    for (size_t i = 0; i < num_planes; i++) {
      // Assert that each plane of frame starts at required byte boundary.
      ASSERT_EQ(0u, dest_offset & (kPlatformBufferAlignment - 1))
          << "Planes of frame should be mapped per platform requirements";
      char* dst_ptr = &aligned_data_[dest_offset];
      for (size_t j = 0; j < visible_plane_rows[i]; j++) {
        memcpy(dst_ptr, src_ptr, visible_bpl[i]);
        src_ptr += visible_bpl[i];
        dst_ptr += static_cast<off_t>(coded_bpl[i]);
      }
      dest_offset += aligned_plane_size_[i];
    }
    src_offset += static_cast<off_t>(frame_buffer_size);
  }
}

// static
std::unique_ptr<RawDataHelper> RawDataHelper::Create(Video* video) {
  size_t frame_size = 0;
  VideoPixelFormat pixel_format = video->PixelFormat();
  const size_t num_planes = VideoFrame::NumPlanes(pixel_format);
  size_t strides[VideoFrame::kMaxPlanes] = {};
  size_t plane_sizes[VideoFrame::kMaxPlanes] = {};
  const gfx::Size& resolution = video->Resolution();
  // Calculate size of frames and their planes.
  for (size_t i = 0; i < num_planes; ++i) {
    const size_t bytes_per_line =
        VideoFrame::RowBytes(i, pixel_format, resolution.width());
    const size_t plane_size =
        bytes_per_line * VideoFrame::Rows(i, pixel_format, resolution.height());
    strides[i] = bytes_per_line;
    plane_sizes[i] = plane_size;
    frame_size += plane_size;
  }

  // Verify whether calculated frame size is valid.
  const size_t data_size = video->Data().size();
  if (frame_size == 0 || data_size % frame_size != 0 ||
      data_size / frame_size != video->NumFrames()) {
    LOG(ERROR) << "Invalid frame_size=" << frame_size
               << ", file size=" << data_size;
    return nullptr;
  }

  std::vector<ColorPlaneLayout> planes(num_planes);
  size_t offset = 0;
  for (size_t i = 0; i < num_planes; ++i) {
    planes[i].stride =
        VideoFrame::RowBytes(i, pixel_format, resolution.width());
    planes[i].offset = offset;
    planes[i].size = plane_sizes[i];
    offset += plane_sizes[i];
  }
  auto layout = VideoFrameLayout::CreateWithPlanes(pixel_format, resolution,
                                                   std::move(planes));
  if (!layout) {
    LOG(ERROR) << "Failed to create VideoFrameLayout";
    return nullptr;
  }

  return base::WrapUnique(new RawDataHelper(video, frame_size, *layout));
}

RawDataHelper::RawDataHelper(Video* video,
                             size_t frame_size,
                             const VideoFrameLayout& layout)
    : video_(video), frame_size_(frame_size), layout_(layout) {}

RawDataHelper::~RawDataHelper() = default;

scoped_refptr<const VideoFrame> RawDataHelper::GetFrame(size_t index) {
  if (index >= video_->NumFrames()) {
    LOG(ERROR) << "index is too big. index=" << index
               << ", num_frames=" << video_->NumFrames();
    return nullptr;
  }

  size_t offset = frame_size_ * index;
  uint8_t* frame_data[VideoFrame::kMaxPlanes] = {};
  const size_t num_planes = VideoFrame::NumPlanes(video_->PixelFormat());
  for (size_t i = 0; i < num_planes; ++i) {
    frame_data[i] = reinterpret_cast<uint8_t*>(video_->Data().data()) + offset;
    offset += layout_->planes()[i].size;
  }
  // TODO(crbug.com/1045825): Investigate use of MOJO_SHARED_BUFFER, similar to
  // changes made in crrev.com/c/2050895.
  scoped_refptr<const VideoFrame> video_frame =
      VideoFrame::WrapExternalYuvDataWithLayout(
          *layout_, gfx::Rect(video_->Resolution()), video_->Resolution(),
          frame_data[0], frame_data[1], frame_data[2],
          base::TimeTicks::Now().since_origin());
  return video_frame;
}

}  // namespace test
}  // namespace media
