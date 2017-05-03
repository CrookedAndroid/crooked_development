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

#ifndef ABI_DIFF_WRAPPER_H
#define ABI_DIFF_WRAPPER_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#include "proto/abi_dump.pb.h"
#include "proto/abi_diff.pb.h"
#pragma clang diagnostic pop

namespace abi_diff_wrappers {

template <typename T>
static bool IgnoreSymbol(const T *element,
                         const std::set<std::string> &ignore_symbols) {
    return ignore_symbols.find(element->basic_abi().linker_set_key()) !=
        ignore_symbols.end();
}

template <typename T, typename TDiff>
class DiffWrapperBase {
 public:
  virtual std::unique_ptr<TDiff> Get() = 0 ;
 protected:
  DiffWrapperBase(const T *oldp, const T *newp,
                  const std::set<std::string> &ignore_diff_symbols)
      : oldp_(oldp), newp_(newp), ignore_diff_symbols_(ignore_diff_symbols) { }
  template <typename Element, typename ElementDiff>
  bool GetElementDiffs(
      google::protobuf::RepeatedPtrField<ElementDiff> *dst,
      const google::protobuf::RepeatedPtrField<Element> &old_elements,
      const google::protobuf::RepeatedPtrField<Element> &new_elements);

 private:
  template <typename Element, typename ElementDiff>
  void GetExtraElementDiffs(
      google::protobuf::RepeatedPtrField<ElementDiff> *dst, int i, int j,
      const google::protobuf::RepeatedPtrField<Element> &old_elements,
      const google::protobuf::RepeatedPtrField<Element> &new_elements);

 protected:
  const T *oldp_;
  const T *newp_;
  const std::set<std::string> &ignore_diff_symbols_;
};

template <typename T, typename TDiff>
class DiffWrapper : public DiffWrapperBase<T, TDiff> {
 public:
  DiffWrapper(const T *oldp, const T *newp,
              const std::set<std::string> &ignored_symbols)
      : DiffWrapperBase<T, TDiff>(oldp, newp, ignored_symbols) { }
  virtual std::unique_ptr<TDiff> Get() override {
    if (!IgnoreSymbol<T>(this->oldp_, this->ignore_diff_symbols_)) {
      return GetInternal();
    }
    return nullptr;
  }

 private:
  std::unique_ptr<TDiff> GetInternal();
};

} // abi_diff_wrappers

#endif
