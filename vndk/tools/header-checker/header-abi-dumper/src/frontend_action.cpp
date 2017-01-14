// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "frontend_action.h"
#include "ast_processing.h"

#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/MultiplexConsumer.h>
#include <clang/Lex/Token.h>
#include <clang/Serialization/ASTWriter.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <string>

HeaderCheckerFrontendAction::HeaderCheckerFrontendAction(
    const std::string &dump_name)
  : dump_name_(dump_name) {}

std::unique_ptr<clang::ASTConsumer>
HeaderCheckerFrontendAction::CreateASTConsumer(clang::CompilerInstance &ci,
                                               llvm::StringRef header_file) {
  // Add preprocessor callbacks.
  clang::Preprocessor &pp = ci.getPreprocessor();
  pp.addPPCallbacks(llvm::make_unique<HeaderASTPPCallbacks>());

  // Create AST consumers.
  std::vector<std::unique_ptr<clang::ASTConsumer>> consumers;
  consumers.push_back(llvm::make_unique<HeaderASTConsumer>(
      header_file, &ci, dump_name_));
  // Still have a MultiplexConsumer in case other consumers need to be
  // added later.
  return llvm::make_unique<clang::MultiplexConsumer>(std::move(consumers));
}
