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

#include "cinn/frontend/syntax.h"

#include <gtest/gtest.h>

#include <memory>
//
#include "cinn/cinn.h"
#include "cinn/frontend/optimize.h"
#include "cinn/hlir/framework/graph.h"
#include "cinn/hlir/framework/graph_compiler.h"
#include "cinn/hlir/framework/pass.h"
#include "cinn/hlir/framework/scope.h"
#include "cinn/hlir/op/use_ops.h"
#include "cinn/hlir/pass/use_pass.h"
#include "cinn/utils/data_util.h"

DEFINE_string(model_dir, "", "");

namespace cinn {
namespace frontend {

using ::cinn::hlir::framework::Graph;
using ::cinn::hlir::framework::Scope;

// using hlir::framework::Scope;
using utils::Join;

std::unique_ptr<Program> CreateAddProgram() {
  const int M = 32;
  const int N = 24;

  Placeholder a(Float(32), {M, N});
  Placeholder b(Float(32), {M, N});
  std::unique_ptr<Program> program(new Program);

  auto c = program->add(a, b);
  auto d = program->add(a, c);

  program->SetInputs({a, b});
  program->Validate();

  return program;
}

TEST(syntax, basic) {
  auto program = CreateAddProgram();
  // output program
  for (int i = 0; i < program->size(); i++) {
    LOG(INFO) << "instruction: " << (*program)[i];
  }
}

TEST(syntax, program_execute_multi_elementwise_add) {
  auto program  = CreateAddProgram();
  Target target = common::DefaultTarget();
  auto graph    = std::make_shared<hlir::framework::Graph>(*program, target);
  LOG(INFO) << "graph:\n" << graph->Visualize();

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  auto scope = BuildScope(target, graph);
  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
  scope->Var<hlir::framework::Tensor>("A");
  scope->Var<hlir::framework::Tensor>("B");
  auto A = scope->GetTensor("A");
  auto B = scope->GetTensor("B");
  SetRandData<float>(A, target);
  SetRandData<float>(B, target);
  runtime_program->Execute();
}

TEST(syntax, program_execute_multi_elementwise_add2) {
  auto program  = CreateAddProgram();
  Target target = common::DefaultTarget();
  auto graph    = std::make_shared<hlir::framework::Graph>(*program, target);
  LOG(INFO) << "graph:\n" << graph->Visualize();

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  auto scope = BuildScope(target, graph);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();

  scope->Var<hlir::framework::Tensor>("A");
  scope->Var<hlir::framework::Tensor>("B");

  auto A = scope->GetTensor("A");
  auto B = scope->GetTensor("B");
  SetRandData<float>(A, target);
  SetRandData<float>(B, target);

  runtime_program->Execute();
}

TEST(syntax, program_execute_fc) {
  const int B = 10;  // batch size
  const int M = 32;
  const int K = 18;
  const int N = 24;

  Placeholder a(Float(32), {B, M, K}, "A");
  Placeholder w(Float(32), {N, K}, "W");  // weight
  Placeholder b(Float(32), {N}, "B");     // bias

  Program program;
  auto mul_out = program.mul(a, w, 2, 1);
  auto add_out = program.add(mul_out, b);
  program.SetInputs({a, w, b});
  program.Validate();

  Target target = common::DefaultTarget();
  auto graph    = std::make_shared<hlir::framework::Graph>(program, target);

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  auto scope = BuildScope(target, graph);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();

  scope->Var<hlir::framework::Tensor>(std::string(a.id()));
  scope->Var<hlir::framework::Tensor>(std::string(w.id()));
  scope->Var<hlir::framework::Tensor>(std::string(b.id()));
  scope->Var<hlir::framework::Tensor>(std::string(mul_out->id));

  auto at        = scope->GetTensor(std::string(a.id()));
  auto wt        = scope->GetTensor(std::string(w.id()));
  auto bt        = scope->GetTensor(std::string(b.id()));
  auto fake_outt = scope->GetTensor(std::string(mul_out->id));
  auto add_outt  = scope->GetTensor(std::string(add_out->id));
  SetRandData<float>(at, target);
  SetRandData<float>(wt, target);
  SetRandData<float>(bt, target);

  runtime_program->Execute();
}

// Load a simple Paddle model, execute it
TEST(load_paddle_model, fc_execute) {
  auto scope = std::make_shared<Scope>();

  auto programTuple               = LoadPaddleProgram(FLAGS_model_dir, scope.get(), false);
  auto& program                   = std::get<0>(programTuple);
  auto& var_map                   = std::get<1>(programTuple);
  auto& var_map_paddle_to_program = std::get<2>(programTuple);

  var_map["A"]->shape = {1, 30};
  program->SetInputs({var_map["A"]});
  program->Validate();

  LOG(INFO) << "program:\n" << *program;

  Target target = common::DefaultHostTarget();
  auto graph    = std::make_shared<hlir::framework::Graph>(*program, target);

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  scope = BuildScope(target, graph, scope);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();

  auto at = scope->GetTensor("A");
  SetRandData<float>(at, target);
  LOG(INFO) << "Before Execute";

  runtime_program->Execute();

  LOG(INFO) << "scope.names: " << Join(scope->var_names(), ",");

  const std::string output_name = "fc_0.tmp_2";
  auto tensor                   = scope->GetTensor(var_map_paddle_to_program.at(output_name));
  LOG(INFO) << "tensor.shape: " << utils::Join(tensor->shape().data(), ",");
  auto data = GetTensorData<float>(tensor, target);
  for (int i = 0; i < 10; i++) LOG(INFO) << "data: " << data[i];
}

/*
TEST(Frontend, conv) {
  Placeholder A(Float(32), {2, 24, 56, 45}, "A");
  Placeholder B(Float(32), {2, 24, 56, 45}, "B");
  Placeholder E(Float(32), {144, 24, 1, 1}, "E");

  Program program;
  auto c = program.elementwise_add(A, B);
  auto d = program.relu(c);
  absl::flat_hash_map<std::string, Program::attr_t> attrs;
  attrs["stride"]   = std::vector<int>({1, 1});
  attrs["dilation"] = std::vector<int>({1, 1});
  attrs["padding"]  = std::vector<int>({0, 0});

  auto f = program.conv2d(d, E, attrs);

  absl::flat_hash_map<std::string, Program::attr_t> attrs1, attrs2;

  attrs1["scale"] = 2.0f;
  attrs1["bias"]  = 0.5f;

  auto g = program.scale(f, attrs1);

  attrs2["axis"] = 1;

  auto h = program.softmax(g, attrs2);

  program.SetInputs({A, B, E});
  program.Validate();

  Target target = common::DefaultTarget();
  auto graph = std::make_shared<hlir::framework::Graph>(program, target);

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  auto scope    = BuildScope(target, graph);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
}
 */

}  // namespace frontend
}  // namespace cinn
