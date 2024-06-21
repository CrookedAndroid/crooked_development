// Copyright (C) 2019 The Android Open Source Project
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

#ifndef HEADER_CHECKER_REPR_IR_READER_H_
#define HEADER_CHECKER_REPR_IR_READER_H_

#include "repr/ir_representation.h"

#include <memory>
#include <string>


namespace header_checker {
namespace repr {


class IRReader {
 public:
  static std::unique_ptr<IRReader> CreateIRReader(TextFormatIR text_format,
                                                  ModuleIR &module_ir);

  IRReader(ModuleIR &module_ir) : module_(module_ir) {}

  virtual ~IRReader() {}

  bool ReadDump(const std::string &dump_file);

 private:
  virtual bool ReadDumpImpl(const std::string &dump_file) = 0;

 protected:
  ModuleIR &module_;
};


}  // namespace repr
}  // namespace header_checker


#endif  // HEADER_CHECKER_REPR_IR_READER_H_
