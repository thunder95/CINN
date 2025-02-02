// Copyright (c) 2022 CINN Authors. All Rights Reserved.
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

#include "cinn/auto_schedule/search_space/auto_gen_rule/auto_unroll.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "cinn/cinn.h"
#include "cinn/lang/lower.h"

namespace cinn {
namespace auto_schedule {

TEST(AutoUnroll, Init) {
  using namespace ir;

  Expr M(100);
  Expr N(4);
  Placeholder<float> A("A", {M, N});
  Placeholder<float> B("B", {M, N});
  Tensor C = Compute(
      {M, N}, [&](Var i, Var j) { return A(i, j) * B(i, j); }, "C");

#ifdef CINN_WITH_CUDA
  Target target = common::DefaultNVGPUTarget();
#else
  Target target = common::DefaultHostTarget();
#endif
  auto stages = CreateStages({C});
  auto funcs  = cinn::lang::LowerVec("test_init", stages, {A, B, C}, {}, {}, nullptr, target, true);

  auto ast_expr = funcs[0]->body;
  ir::ModuleExpr mod_expr({ast_expr});
  AutoUnroll test_rule(target);
  // not meet specific condition
  ASSERT_EQ(test_rule.Init(mod_expr), RuleApplyType::kCannotApply);
}

TEST(AutoUnroll, UnrollableApply) {
  using namespace ir;

  Expr M(100);
  Expr N(4);
  Expr K(32);
  Placeholder<float> A("A", {M, K});
  Placeholder<float> B("B", {K, N});
  Var k(K.as_int32(), "k0");
  Tensor C = Compute(
      {M, N}, [&](Var i, Var j) { return ReduceSum(A(i, k) * B(k, j), {k}); }, "C");

#ifdef CINN_WITH_CUDA
  Target target = common::DefaultNVGPUTarget();
#else
  Target target = common::DefaultHostTarget();
#endif
  auto stages = CreateStages({C});
  auto funcs  = cinn::lang::LowerVec("test_unrollable", stages, {A, B, C}, {}, {}, nullptr, target, true);

  auto ast_expr        = funcs[0]->body;
  auto* block_realize  = ast_expr.As<ir::Block>()->stmts.front().As<ir::ScheduleBlockRealize>();
  auto* schedule_block = block_realize->schedule_block.As<ir::ScheduleBlock>();
  ASSERT_NE(schedule_block, nullptr);
  ASSERT_TRUE(schedule_block->attrs.empty());
  ir::ModuleExpr mod_expr({ast_expr});
  VLOG(6) << "Before auto-unroll:\n" << ast_expr;

  AutoUnroll test_rule(target);
  ASSERT_EQ(test_rule.Init(mod_expr), RuleApplyType::kApplyAndSkipThisRule);
  EXPECT_EQ(test_rule.NumberApplicable(), 1);
  test_rule.ApplyRandomly();

  ASSERT_FALSE(schedule_block->attrs.empty());
  EXPECT_EQ(schedule_block->attrs.count(ir::attr::auto_unroll_max_step), 1);
  const auto& attr_value = schedule_block->attrs.at(ir::attr::auto_unroll_max_step);
  const int* max_step    = absl::get_if<int>(&attr_value);
  EXPECT_NE(max_step, nullptr);
  EXPECT_LE(*max_step, 128);
  VLOG(6) << "After auto-unroll:max_step=" << *max_step << ", Ast:\n" << ast_expr;
}

}  // namespace auto_schedule
}  // namespace cinn
