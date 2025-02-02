// Copyright (c) 2021 CINN Authors. All Rights Reserved.
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

#include "cinn/runtime/cpu/host_intrinsics.h"

#include <gtest/gtest.h>

#include "cinn/backends/compiler.h"
#include "cinn/backends/llvm/execution_engine.h"
#include "cinn/backends/llvm/simple_jit.h"
#include "cinn/cinn.h"
#include "cinn/common/ir_util.h"
#include "cinn/common/target.h"
#include "cinn/common/test_helper.h"
#include "cinn/runtime/cpu/use_extern_funcs.h"

namespace cinn {
namespace runtime {
namespace cpu {

TEST(tanh, basic) {
  Expr M(10), N(20);
  Placeholder<float> x("x", {M, N});
  auto y = Compute(
      {M, N}, [&](Expr i, Expr j) { return CallExtern("tanh", {x(i, j)}); }, "y");

  auto stages = CreateStages({y});

  auto jit = backends::SimpleJIT::Create();

  ir::Module::Builder builder("module1", common::DefaultHostTarget());

  auto fn = Lower("fn", stages, {x, y});
  LOG(INFO) << "fn:\n" << fn;

  builder.AddFunction(fn);

  jit->Link(builder.Build());

  auto fn_ptr = jit->Lookup("fn");
  auto fnp    = reinterpret_cast<lower_func_ptr_t>(fn_ptr);
  ASSERT_TRUE(fnp);

  auto* x_buf   = common::BufferBuilder(Float(32), {M.as_int32(), N.as_int32()}).set_random().Build();
  auto* out_buf = common::BufferBuilder(Float(32), {M.as_int32(), N.as_int32()}).set_zero().Build();
  auto args     = common::ArgsBuilder().Add(x_buf).Add(out_buf).Build();
  fnp(args.data(), args.size());

  auto* x_buf_data   = reinterpret_cast<float*>(x_buf->memory);
  auto* out_buf_data = reinterpret_cast<float*>(out_buf->memory);

  for (int i = 0; i < x_buf->num_elements(); i++) {
    LOG_FIRST_N(INFO, 3) << out_buf_data[i];
    ASSERT_NEAR(out_buf_data[i], std::tanh(x_buf_data[i]), 1e-5);
  }
}

}  // namespace cpu
}  // namespace runtime
}  // namespace cinn
