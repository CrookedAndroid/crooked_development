// Copyright (C) 2018 The Android Open Source Project
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

#include "repr/json/ir_reader.h"

#include "repr/ir_dumper.h"
#include "repr/ir_reader.h"
#include "repr/ir_representation_internal.h"
#include "repr/json/converter.h"

#include <json/reader.h>
#include <json/writer.h>

#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>


namespace header_checker {
namespace repr {


JsonObjectRef::JsonObjectRef(const Json::Value &json_value, bool &ok)
    : object_(json_value.isObject() ? json_value : json_empty_object), ok_(ok) {
  if (!json_value.isObject()) {
    ok_ = false;
  }
}

const Json::Value &
JsonObjectRef::Get(const std::string &key, const Json::Value &default_value,
                   IsExpectedJsonType is_expected_type) const {
  if (!object_.isMember(key)) {
    return default_value;
  }
  const Json::Value &value = object_[key];
  if (!(value.*is_expected_type)()) {
    ok_ = false;
    return default_value;
  }
  return value;
}

bool JsonObjectRef::GetBool(const std::string &key) const {
  return Get(key, json_false, &Json::Value::isBool).asBool();
}

int64_t JsonObjectRef::GetInt(const std::string &key) const {
  return Get(key, json_0, &Json::Value::isIntegral).asInt64();
}

uint64_t JsonObjectRef::GetUint(const std::string &key) const {
  return Get(key, json_0, &Json::Value::isIntegral).asUInt64();
}

std::string JsonObjectRef::GetString(const std::string &key) const {
  return Get(key, json_empty_string, &Json::Value::isString).asString();
}

JsonObjectRef JsonObjectRef::GetObject(const std::string &key) const {
  return JsonObjectRef(Get(key, json_empty_object, &Json::Value::isObject),
                       ok_);
}

JsonArrayRef<JsonObjectRef>
JsonObjectRef::GetObjects(const std::string &key) const {
  return JsonArrayRef<JsonObjectRef>(
      Get(key, json_empty_array, &Json::Value::isArray), ok_);
}

JsonArrayRef<std::string>
JsonObjectRef::GetStrings(const std::string &key) const {
  return JsonArrayRef<std::string>(
      Get(key, json_empty_array, &Json::Value::isArray), ok_);
}

template <>
JsonObjectRef JsonArrayRef<JsonObjectRef>::Iterator::operator*() const {
  return JsonObjectRef(array_[index_], ok_);
}

template <> std::string JsonArrayRef<std::string>::Iterator::operator*() const {
  return array_[index_].asString();
}

static AccessSpecifierIR GetAccess(const JsonObjectRef &type_decl) {
  std::string access(type_decl.GetString("access"));
  if (access.empty()) {
    return default_access_ir;
  }
  return FindInMap(access_json_to_ir, access,
                   "Failed to convert JSON to AccessSpecifierIR");
}

static RecordTypeIR::RecordKind
GetRecordKind(const JsonObjectRef &record_type) {
  std::string kind(record_type.GetString("record_kind"));
  if (kind.empty()) {
    return default_record_kind_ir;
  }
  return FindInMap(record_kind_json_to_ir, kind,
                   "Failed to convert JSON to RecordKind");
}

static VTableComponentIR::Kind
GetVTableComponentKind(const JsonObjectRef &vtable_component) {
  std::string kind(vtable_component.GetString("kind"));
  if (kind.empty()) {
    return default_vtable_component_kind_ir;
  }
  return FindInMap(vtable_component_kind_json_to_ir, kind,
                   "Failed to convert JSON to VTableComponentIR::Kind");
}

static ElfSymbolIR::ElfSymbolBinding
GetElfSymbolBinding(const JsonObjectRef &elf_symbol) {
  std::string binding(elf_symbol.GetString("binding"));
  if (binding.empty()) {
    return default_elf_symbol_binding_ir;
  }
  return FindInMap(elf_symbol_binding_json_to_ir, binding,
                   "Failed to convert JSON to ElfSymbolBinding");
}

bool JsonIRReader::ReadDump(const std::string &dump_file) {
  Json::Value tu_json;
  Json::Reader reader;
  std::ifstream input(dump_file);

  if (!reader.parse(input, tu_json, /* collectComments */ false)) {
    llvm::errs() << "Failed to parse JSON: "
                 << reader.getFormattedErrorMessages() << "\n";
    return false;
  }
  bool ok = true;
  JsonObjectRef tu(tu_json, ok);
  if (!ok) {
    llvm::errs() << "Translation unit is not an object\n";
    return false;
  }

  ReadFunctions(tu);
  ReadGlobalVariables(tu);
  ReadEnumTypes(tu);
  ReadRecordTypes(tu);
  ReadFunctionTypes(tu);
  ReadArrayTypes(tu);
  ReadPointerTypes(tu);
  ReadQualifiedTypes(tu);
  ReadBuiltinTypes(tu);
  ReadLvalueReferenceTypes(tu);
  ReadRvalueReferenceTypes(tu);
  ReadElfFunctions(tu);
  ReadElfObjects(tu);
  if (!ok) {
    llvm::errs() << "Failed to convert JSON to IR\n";
    return false;
  }
  return true;
}

void JsonIRReader::ReadTagTypeInfo(const JsonObjectRef &type_decl,
                                   TagTypeIR *tag_type_ir) {
  tag_type_ir->SetUniqueId(type_decl.GetString("unique_id"));
}

void JsonIRReader::ReadTemplateInfo(const JsonObjectRef &type_decl,
                                    TemplatedArtifactIR *template_ir) {
  TemplateInfoIR template_info_ir;
  for (auto &&referenced_type : type_decl.GetStrings("template_args")) {
    TemplateElementIR template_element_ir(referenced_type);
    template_info_ir.AddTemplateElement(std::move(template_element_ir));
  }
  template_ir->SetTemplateInfo(std::move(template_info_ir));
}

void JsonIRReader::ReadTypeInfo(const JsonObjectRef &type_decl,
                                TypeIR *type_ir) {
  type_ir->SetLinkerSetKey(type_decl.GetString("linker_set_key"));
  type_ir->SetSourceFile(type_decl.GetString("source_file"));
  type_ir->SetName(type_decl.GetString("name"));
  type_ir->SetReferencedType(type_decl.GetString("referenced_type"));
  type_ir->SetSelfType(type_decl.GetString("self_type"));
  type_ir->SetSize(type_decl.GetUint("size"));
  type_ir->SetAlignment(type_decl.GetUint("alignment"));
}

void JsonIRReader::ReadRecordFields(const JsonObjectRef &record_type,
                                    RecordTypeIR *record_ir) {
  for (auto &&field : record_type.GetObjects("fields")) {
    RecordFieldIR record_field_ir(
        field.GetString("field_name"), field.GetString("referenced_type"),
        field.GetUint("field_offset"), GetAccess(field));
    record_ir->AddRecordField(std::move(record_field_ir));
  }
}

void JsonIRReader::ReadBaseSpecifiers(const JsonObjectRef &record_type,
                                      RecordTypeIR *record_ir) {
  for (auto &&base_specifier : record_type.GetObjects("base_specifiers")) {
    CXXBaseSpecifierIR record_base_ir(
        base_specifier.GetString("referenced_type"),
        base_specifier.GetBool("is_virtual"), GetAccess(base_specifier));
    record_ir->AddCXXBaseSpecifier(std::move(record_base_ir));
  }
}

void JsonIRReader::ReadVTableLayout(const JsonObjectRef &record_type,
                                    RecordTypeIR *record_ir) {
  VTableLayoutIR vtable_layout_ir;
  for (auto &&vtable_component : record_type.GetObjects("vtable_components")) {
    VTableComponentIR vtable_component_ir(
        vtable_component.GetString("mangled_component_name"),
        GetVTableComponentKind(vtable_component),
        vtable_component.GetInt("component_value"),
        vtable_component.GetBool("is_pure"));
    vtable_layout_ir.AddVTableComponent(std::move(vtable_component_ir));
  }
  record_ir->SetVTableLayout(std::move(vtable_layout_ir));
}

void JsonIRReader::ReadEnumFields(const JsonObjectRef &enum_type,
                                  EnumTypeIR *enum_ir) {
  for (auto &&field : enum_type.GetObjects("enum_fields")) {
    EnumFieldIR enum_field_ir(field.GetString("name"),
                              field.GetInt("enum_field_value"));
    enum_ir->AddEnumField(std::move(enum_field_ir));
  }
}

void JsonIRReader::ReadFunctionParametersAndReturnType(
    const JsonObjectRef &function, CFunctionLikeIR *function_ir) {
  function_ir->SetReturnType(function.GetString("return_type"));
  for (auto &&parameter : function.GetObjects("parameters")) {
    ParamIR param_ir(parameter.GetString("referenced_type"),
                     parameter.GetBool("default_arg"),
                     parameter.GetBool("is_this_ptr"));
    function_ir->AddParameter(std::move(param_ir));
  }
}

FunctionIR JsonIRReader::FunctionJsonToIR(const JsonObjectRef &function) {
  FunctionIR function_ir;
  function_ir.SetLinkerSetKey(function.GetString("linker_set_key"));
  function_ir.SetName(function.GetString("function_name"));
  function_ir.SetAccess(GetAccess(function));
  function_ir.SetSourceFile(function.GetString("source_file"));
  ReadFunctionParametersAndReturnType(function, &function_ir);
  ReadTemplateInfo(function, &function_ir);
  return function_ir;
}

FunctionTypeIR
JsonIRReader::FunctionTypeJsonToIR(const JsonObjectRef &function_type) {
  FunctionTypeIR function_type_ir;
  ReadTypeInfo(function_type, &function_type_ir);
  ReadFunctionParametersAndReturnType(function_type, &function_type_ir);
  return function_type_ir;
}

RecordTypeIR
JsonIRReader::RecordTypeJsonToIR(const JsonObjectRef &record_type) {
  RecordTypeIR record_type_ir;
  ReadTypeInfo(record_type, &record_type_ir);
  ReadTemplateInfo(record_type, &record_type_ir);
  record_type_ir.SetAccess(GetAccess(record_type));
  ReadVTableLayout(record_type, &record_type_ir);
  ReadRecordFields(record_type, &record_type_ir);
  ReadBaseSpecifiers(record_type, &record_type_ir);
  record_type_ir.SetRecordKind(GetRecordKind(record_type));
  record_type_ir.SetAnonymity(record_type.GetBool("is_anonymous"));
  ReadTagTypeInfo(record_type, &record_type_ir);
  return record_type_ir;
}

EnumTypeIR JsonIRReader::EnumTypeJsonToIR(const JsonObjectRef &enum_type) {
  EnumTypeIR enum_type_ir;
  ReadTypeInfo(enum_type, &enum_type_ir);
  enum_type_ir.SetUnderlyingType(enum_type.GetString("underlying_type"));
  enum_type_ir.SetAccess(GetAccess(enum_type));
  ReadEnumFields(enum_type, &enum_type_ir);
  ReadTagTypeInfo(enum_type, &enum_type_ir);
  return enum_type_ir;
}

void JsonIRReader::ReadGlobalVariables(const JsonObjectRef &tu) {
  for (auto &&global_variable : tu.GetObjects("global_vars")) {
    GlobalVarIR global_variable_ir;
    global_variable_ir.SetName(global_variable.GetString("name"));
    global_variable_ir.SetAccess(GetAccess(global_variable));
    global_variable_ir.SetSourceFile(global_variable.GetString("source_file"));
    global_variable_ir.SetReferencedType(
        global_variable.GetString("referenced_type"));
    global_variable_ir.SetLinkerSetKey(
        global_variable.GetString("linker_set_key"));
    if (!IsLinkableMessageInExportedHeaders(&global_variable_ir)) {
      continue;
    }
    module_->global_variables_.insert(
        {global_variable_ir.GetLinkerSetKey(), std::move(global_variable_ir)});
  }
}

void JsonIRReader::ReadPointerTypes(const JsonObjectRef &tu) {
  for (auto &&pointer_type : tu.GetObjects("pointer_types")) {
    PointerTypeIR pointer_type_ir;
    ReadTypeInfo(pointer_type, &pointer_type_ir);
    if (!IsLinkableMessageInExportedHeaders(&pointer_type_ir)) {
      continue;
    }
    AddToMapAndTypeGraph(std::move(pointer_type_ir), &module_->pointer_types_,
                         &module_->type_graph_);
  }
}

void JsonIRReader::ReadBuiltinTypes(const JsonObjectRef &tu) {
  for (auto &&builtin_type : tu.GetObjects("builtin_types")) {
    BuiltinTypeIR builtin_type_ir;
    ReadTypeInfo(builtin_type, &builtin_type_ir);
    builtin_type_ir.SetSignedness(builtin_type.GetBool("is_unsigned"));
    builtin_type_ir.SetIntegralType(builtin_type.GetBool("is_integral"));
    AddToMapAndTypeGraph(std::move(builtin_type_ir), &module_->builtin_types_,
                         &module_->type_graph_);
  }
}

void JsonIRReader::ReadQualifiedTypes(const JsonObjectRef &tu) {
  for (auto &&qualified_type : tu.GetObjects("qualified_types")) {
    QualifiedTypeIR qualified_type_ir;
    ReadTypeInfo(qualified_type, &qualified_type_ir);
    qualified_type_ir.SetConstness(qualified_type.GetBool("is_const"));
    qualified_type_ir.SetVolatility(qualified_type.GetBool("is_volatile"));
    qualified_type_ir.SetRestrictedness(
        qualified_type.GetBool("is_restricted"));
    if (!IsLinkableMessageInExportedHeaders(&qualified_type_ir)) {
      continue;
    }
    AddToMapAndTypeGraph(std::move(qualified_type_ir),
                         &module_->qualified_types_, &module_->type_graph_);
  }
}

void JsonIRReader::ReadArrayTypes(const JsonObjectRef &tu) {
  for (auto &&array_type : tu.GetObjects("array_types")) {
    ArrayTypeIR array_type_ir;
    ReadTypeInfo(array_type, &array_type_ir);
    if (!IsLinkableMessageInExportedHeaders(&array_type_ir)) {
      continue;
    }
    AddToMapAndTypeGraph(std::move(array_type_ir), &module_->array_types_,
                         &module_->type_graph_);
  }
}

void JsonIRReader::ReadLvalueReferenceTypes(const JsonObjectRef &tu) {
  for (auto &&lvalue_reference_type : tu.GetObjects("lvalue_reference_types")) {
    LvalueReferenceTypeIR lvalue_reference_type_ir;
    ReadTypeInfo(lvalue_reference_type, &lvalue_reference_type_ir);
    if (!IsLinkableMessageInExportedHeaders(&lvalue_reference_type_ir)) {
      continue;
    }
    AddToMapAndTypeGraph(std::move(lvalue_reference_type_ir),
                         &module_->lvalue_reference_types_,
                         &module_->type_graph_);
  }
}

void JsonIRReader::ReadRvalueReferenceTypes(const JsonObjectRef &tu) {
  for (auto &&rvalue_reference_type : tu.GetObjects("rvalue_reference_types")) {
    RvalueReferenceTypeIR rvalue_reference_type_ir;
    ReadTypeInfo(rvalue_reference_type, &rvalue_reference_type_ir);
    if (!IsLinkableMessageInExportedHeaders(&rvalue_reference_type_ir)) {
      continue;
    }
    AddToMapAndTypeGraph(std::move(rvalue_reference_type_ir),
                         &module_->rvalue_reference_types_,
                         &module_->type_graph_);
  }
}

void JsonIRReader::ReadFunctions(const JsonObjectRef &tu) {
  for (auto &&function : tu.GetObjects("functions")) {
    FunctionIR function_ir = FunctionJsonToIR(function);
    if (!IsLinkableMessageInExportedHeaders(&function_ir)) {
      continue;
    }
    module_->functions_.insert(
        {function_ir.GetLinkerSetKey(), std::move(function_ir)});
  }
}

void JsonIRReader::ReadRecordTypes(const JsonObjectRef &tu) {
  for (auto &&record_type : tu.GetObjects("record_types")) {
    RecordTypeIR record_type_ir = RecordTypeJsonToIR(record_type);
    if (!IsLinkableMessageInExportedHeaders(&record_type_ir)) {
      continue;
    }
    auto it = AddToMapAndTypeGraph(std::move(record_type_ir),
                                   &module_->record_types_,
                                   &module_->type_graph_);
    const std::string &key = GetODRListMapKey(&(it->second));
    AddToODRListMap(key, &(it->second));
  }
}

void JsonIRReader::ReadFunctionTypes(const JsonObjectRef &tu) {
  for (auto &&function_type : tu.GetObjects("function_types")) {
    FunctionTypeIR function_type_ir = FunctionTypeJsonToIR(function_type);
    if (!IsLinkableMessageInExportedHeaders(&function_type_ir)) {
      continue;
    }
    auto it = AddToMapAndTypeGraph(std::move(function_type_ir),
                                   &module_->function_types_,
                                   &module_->type_graph_);
    const std::string &key = GetODRListMapKey(&(it->second));
    AddToODRListMap(key, &(it->second));
  }
}

void JsonIRReader::ReadEnumTypes(const JsonObjectRef &tu) {
  for (auto &&enum_type : tu.GetObjects("enum_types")) {
    EnumTypeIR enum_type_ir = EnumTypeJsonToIR(enum_type);
    if (!IsLinkableMessageInExportedHeaders(&enum_type_ir)) {
      continue;
    }
    auto it = AddToMapAndTypeGraph(std::move(enum_type_ir),
                                   &module_->enum_types_,
                                   &module_->type_graph_);
    AddToODRListMap(it->second.GetUniqueId() + it->second.GetSourceFile(),
                    (&it->second));
  }
}

void JsonIRReader::ReadElfFunctions(const JsonObjectRef &tu) {
  for (auto &&elf_function : tu.GetObjects("elf_functions")) {
    ElfFunctionIR elf_function_ir(elf_function.GetString("name"),
                                  GetElfSymbolBinding(elf_function));
    module_->elf_functions_.insert(
        {elf_function_ir.GetName(), std::move(elf_function_ir)});
  }
}

void JsonIRReader::ReadElfObjects(const JsonObjectRef &tu) {
  for (auto &&elf_object : tu.GetObjects("elf_objects")) {
    ElfObjectIR elf_object_ir(elf_object.GetString("name"),
                              GetElfSymbolBinding(elf_object));
    module_->elf_objects_.insert(
        {elf_object_ir.GetName(), std::move(elf_object_ir)});
  }
}


}  // namespace repr
}  // header_checker
