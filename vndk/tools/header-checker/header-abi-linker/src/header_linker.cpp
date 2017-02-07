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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#include "proto/abi_dump.pb.h"
#pragma clang diagnostic pop

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>


#include <google/protobuf/text_format.h>

#include <memory>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <stdlib.h>

using std::vector;

static llvm::cl::OptionCategory header_checker_category(
    "header-abi-linker options");

static llvm::cl::list<std::string> dump_files(
    llvm::cl::Positional, llvm::cl::desc("<dump-files>"), llvm::cl::Required,
    llvm::cl::cat(header_checker_category), llvm::cl::OneOrMore);

static llvm::cl::opt<std::string> linked_dump(
    "o", llvm::cl::desc("<linked dump>"), llvm::cl::Required,
    llvm::cl::cat(header_checker_category));

class HeaderAbiLinker {
 public:
  HeaderAbiLinker(vector<std::string> &files, std::string &linked_dump)
      : dump_files_(files), out_dump_name_(linked_dump) {};

  bool LinkAndDump();

 private:
  bool LinkRecords(const abi_dump::TranslationUnit *dump_tu,
                   abi_dump::TranslationUnit *linked_tu);

  bool LinkFunctions(const abi_dump::TranslationUnit *dump_tu,
                     abi_dump::TranslationUnit *linked_tu);

  bool LinkEnums(const abi_dump::TranslationUnit *dump_tu,
                 abi_dump::TranslationUnit *linked_tu);

  template <typename T>
  bool inline LinkDecl(
    google::protobuf::RepeatedPtrField<T> *dst,
    std::set<std::string> *link_set,
    const google::protobuf::RepeatedPtrField<T> &src);

 private:
  vector<std::string> &dump_files_;
  std::string &out_dump_name_;
  std::set<std::string> record_decl_set_;
  std::set<std::string> function_decl_set_;
  std::set<std::string> enum_decl_set_;
};

bool HeaderAbiLinker::LinkAndDump() {
  abi_dump::TranslationUnit linked_tu;
  std::string str_out;
  std::ofstream text_output(out_dump_name_ + ".txt");
  std::fstream binary_output(
      out_dump_name_,
      std::ios::out | std::ios::trunc | std::ios::binary);
  for (auto &i : dump_files_) {
    abi_dump::TranslationUnit dump_tu;
    std::fstream input(i, std::ios::binary | std::ios::in);
    if (!dump_tu.ParseFromIstream(&input) ||
        !LinkRecords(&dump_tu, &linked_tu) ||
        !LinkFunctions(&dump_tu, &linked_tu) ||
        !LinkEnums(&dump_tu, &linked_tu)) {
      return false;
    }
  }

  if (!google::protobuf::TextFormat::PrintToString(linked_tu, &str_out) ||
      !linked_tu.SerializeToOstream(&binary_output)) {
    llvm::errs() << "Serialization to ostream failed\n";
    return false;
  }
  text_output << str_out;

  return true;
}

bool HeaderAbiLinker::LinkRecords(const abi_dump::TranslationUnit *dump_tu,
                                  abi_dump::TranslationUnit *linked_tu) {
  return LinkDecl(linked_tu->mutable_records(),
                  &record_decl_set_,
                  dump_tu->records());
}

bool HeaderAbiLinker::LinkFunctions(const abi_dump::TranslationUnit *dump_tu,
                                    abi_dump::TranslationUnit *linked_tu) {
  return LinkDecl(linked_tu->mutable_functions(),
                  &function_decl_set_,
                  dump_tu->functions());
}

bool HeaderAbiLinker::LinkEnums(const abi_dump::TranslationUnit *dump_tu,
                                abi_dump::TranslationUnit *linked_tu) {
  return LinkDecl(linked_tu->mutable_enums(),
                  &enum_decl_set_,
                  dump_tu->enums());

}

template <typename T>
bool inline HeaderAbiLinker::LinkDecl(
    google::protobuf::RepeatedPtrField<T> *dst,
    std::set<std::string> *link_set,
    const google::protobuf::RepeatedPtrField<T> &src) {
  for (auto &&element : src) {
    // The element already exists in the linked dump. Skip.
    if (!link_set->insert(element.linker_set_key()).second) {
      continue;
    }
    T *added_element = dst->Add();
    if (!added_element) {
      return false;
    }
    *added_element = element;
  }
  return true;
}

int main(int argc, const char **argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  llvm::cl::ParseCommandLineOptions(argc, argv, "header-checker");
  HeaderAbiLinker Linker(dump_files, linked_dump);
  if (!Linker.LinkAndDump()) {
    return -1;
  }

  return 0;
}
