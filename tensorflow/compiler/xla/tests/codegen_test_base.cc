/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/xla/tests/codegen_test_base.h"

#include <stdlib.h>
#include <utility>

#include "tensorflow/compiler/xla/legacy_flags/debug_options_flags.h"
#include "tensorflow/compiler/xla/ptr_util.h"
#include "tensorflow/compiler/xla/service/backend.h"
#include "tensorflow/compiler/xla/service/compiler.h"
#include "tensorflow/compiler/xla/statusor.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/lib/strings/strcat.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/subprocess.h"
#include "tensorflow/core/platform/test.h"

namespace xla {

std::unique_ptr<HloModule> CodegenTestBase::CreateNewModuleWithEmbeddedIr(
    bool ftz) {
  HloModuleConfig config;
  auto debug_options = legacy_flags::GetDebugOptionsFromFlags();
  debug_options.set_xla_embed_ir_in_executable(true);
  debug_options.set_xla_gpu_ftz(ftz);
  config.set_debug_options(debug_options);
  return MakeUnique<HloModule>(TestName(), VersionedComputationHandle(),
                               config);
}

void CodegenTestBase::CompileAndVerifyIr(std::unique_ptr<HloModule> hlo_module,
                                         const string& pattern) {
  std::unique_ptr<Executable> executable =
      CompileToExecutable(std::move(hlo_module));
  string ir_module_string = GetIrFromExecutable(*executable);
  RunFileCheck(ir_module_string, pattern);
}

std::unique_ptr<Executable> CodegenTestBase::CompileToExecutable(
    std::unique_ptr<HloModule> hlo_module) {
  return backend_->compiler()
      ->Compile(std::move(hlo_module), test_hlo_dumper_,
                backend_->default_stream_executor())
      .ConsumeValueOrDie();
}

void CodegenTestBase::RunFileCheck(const string& input, const string& pattern) {
  using tensorflow::io::JoinPath;

  // Write input to a temporary file.
  char tempdir_template[] = "/tmp/ir_testXXXXXX";
  char* tempdir_name = mkdtemp(tempdir_template);
  CHECK_NOTNULL(tempdir_name);
  string pattern_path = JoinPath(tempdir_name, "xla_hlo_test_ir_pattern");
  TF_CHECK_OK(tensorflow::WriteStringToFile(tensorflow::Env::Default(),
                                            pattern_path, pattern));

  // Invoke FileCheck to check whether input matches `pattern`.
  const char* file_check_path_suffix = "external/llvm/FileCheck";
  string file_check_path;
  if (const char* test_srcdir = getenv("TEST_SRCDIR")) {
    file_check_path = JoinPath(test_srcdir, file_check_path_suffix);
  } else {
    file_check_path = file_check_path_suffix;
  }

  tensorflow::SubProcess file_check_process;
  file_check_process.SetProgram(file_check_path,
                                {file_check_path, pattern_path});
  file_check_process.SetChannelAction(tensorflow::CHAN_STDIN,
                                      tensorflow::ACTION_PIPE);
  file_check_process.SetChannelAction(tensorflow::CHAN_STDERR,
                                      tensorflow::ACTION_PIPE);
  CHECK(file_check_process.Start());
  string standard_error;
  int exit_status = file_check_process.Communicate(
      /*stdin_input=*/&input, /*stdout_output=*/nullptr,
      /*stderr_output=*/&standard_error);

  // FileCheck returns 0 when the inputs match. If matching failed, we output
  // the error message generated by FileCheck.
  SCOPED_TRACE(tensorflow::strings::StrCat("Input to FileCheck:\n", input));
  EXPECT_EQ(0, exit_status) << standard_error;
}

}  // namespace xla
