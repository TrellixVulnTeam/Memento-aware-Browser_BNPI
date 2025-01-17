/*
 * Copyright 2019 The libgav1 Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBGAV1_SRC_DSP_LOOP_RESTORATION_H_
#define LIBGAV1_SRC_DSP_LOOP_RESTORATION_H_

// Pull in LIBGAV1_DspXXX defines representing the implementation status
// of each function. The resulting value of each can be used by each module to
// determine whether an implementation is needed at compile time.
// IWYU pragma: begin_exports

// ARM:
#include "src/dsp/arm/loop_restoration_neon.h"

// x86:
// Note includes should be sorted in logical order avx2/avx/sse4, etc.
// The order of includes is important as each tests for a superior version
// before setting the base.
// clang-format off
#include "src/dsp/x86/loop_restoration_sse4.h"
// clang-format on

// IWYU pragma: end_exports

namespace libgav1 {
namespace dsp {

enum {
  // Precision of a division table (mtable)
  kSgrProjScaleBits = 20,
  kSgrProjReciprocalBits = 12,
  // Core self-guided restoration precision bits.
  kSgrProjSgrBits = 8,
  // Precision bits of generated values higher than source before projection.
  kSgrProjRestoreBits = 4
};  // anonymous enum

extern const int kXByXPlus1[256];
extern const uint8_t kSgrMa2Lookup[256];

// Initializes Dsp::loop_restorations. This function is not thread-safe.
void LoopRestorationInit_C();

}  // namespace dsp
}  // namespace libgav1

#endif  // LIBGAV1_SRC_DSP_LOOP_RESTORATION_H_
