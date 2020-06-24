/*
 *
 *
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#include "field_info.h"

using namespace AstTypeClassification;

FieldInfo::EleLen::EleLen(FieldInfo& af) : ele_(af.ast_elem_), len_(af.n_elems_)
{
}

bool FieldInfo::EleLen::Cmp::operator()(const EleLen& a, const EleLen& b) const
{
  return a.ele_ != b.ele_ || a.len_ != b.len_;
}

bool FieldInfo::is_anonymous_array(AST_Type& field)
{
  return field.node_type() == AST_Decl::NT_array;
}

bool FieldInfo::is_anonymous_sequence(AST_Type& field)
{
  return field.node_type() == AST_Decl::NT_sequence;
}

bool FieldInfo::is_anonymous_type(AST_Type& field)
{
  return is_anonymous_array(field) || is_anonymous_sequence(field);
}

std::string FieldInfo::get_type_name(AST_Type& field)
{
  std::string n = scoped(field.name());
  if (!is_anonymous_type(field)) {
    return n;
  }
  const std::string scope = n.substr(0, n.find("::") + 2);
  const std::string name = field.local_name()->get_string();
  n = scope + "_" + name;
  if (is_anonymous_sequence(field)) {
    return n + "_seq";
  }
  return n;
}

// for anonymous types
FieldInfo::FieldInfo(AST_Field& field) :
  ast_type_(field.field_type()),
  name_(field.local_name()->get_string())
{
  init();
}

FieldInfo::FieldInfo(UTL_ScopedName* sn, AST_Type* base) :
  ast_type_(base),
  scoped_type_(scoped(sn))
{
  const bool use_cxx11 = (be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11);
  underscores_ = use_cxx11 ? dds_generator::scoped_helper(sn, "_") : "";
  init();
}

void FieldInfo::init()
{
  arr_ = AST_Array::narrow_from_decl(ast_type_);
  seq_ = AST_Sequence::narrow_from_decl(ast_type_);
  ast_elem_ = arr_ ? arr_->base_type() : (seq_ ? seq_->base_type() : nullptr);
  act_ = ast_elem_ ? resolveActualType(ast_elem_) : nullptr;
  cls_ = act_ ? classify(act_) : CL_UNKNOWN;
  if (!act_) {
    throw std::invalid_argument("ast_elem_ is null.");
  }
  if (!name_.empty()) {
    type_ = ast_elem_ ? ("_" + name_) : name_;
    if (seq_) { type_ += "_seq"; }
    const std::string ftn = scoped(ast_type_->name());
    struct_name_ = ftn.substr(0, ftn.find("::"));
    scoped_type_ = struct_name_ + "::" + type_;
  } else if (!scoped_type_.empty()) {
    //name_
    type_ = scoped_type_;
    //struct_name_
  } else {
    throw std::invalid_argument("Both field name and scoped_type are empty.");
  }

  set_element();
  n_elems_ = 1;
  if (arr_) {
    for (size_t i = 0; i < arr_->n_dims(); ++i) {
      n_elems_ *= arr_->dims()[i]->ev()->u.ulval;
    }
    length_ = std::to_string(n_elems_);
    arg_ = "arr";
  } else if (seq_) {
    n_elems_ = !seq_->unbounded() ? seq_->max_size()->ev()->u.ulval : 0;
    length_ = "length";
    arg_ = "seq";
  }
  if (be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11) {
    be_global->header_ << "struct " << underscores_ << "_tag {};\n\n";
    unwrap_ = scoped_type_ + "& " + arg_ + " = wrap;\n  ACE_UNUSED_ARG(" + arg_ + ");\n";
    const_unwrap_ = "  const " + unwrap_;
    unwrap_ = "  " + unwrap_;
    arg_ = "wrap";
    ref_       = "IDL::DistinctType<"       + scoped_type_ + ", " + underscores_ + "_tag>";
    const_ref_ = "IDL::DistinctType<const " + scoped_type_ + ", " + underscores_ + "_tag>";
  } else {
    ref_ = scoped_type_ + (arr_ ? "_forany&" : "&");
    const_ref_ = "const " + ref_;
    ptr_ = scoped_type_ + (arr_ ? "_forany*" : "*");
  }
}

void FieldInfo::set_element()
{
  elem_sz_ = 0;
  if (ast_elem_) {
    if (cls_ & CL_ENUM) {
      elem_sz_ = 4; elem_ = "ACE_CDR::ULong"; return;
    } else if (cls_ & CL_STRING) {
      elem_sz_ = 4; elem_ = string_type(cls_); return; // encoding of str length is 4 bytes
    } else if (cls_ & CL_PRIMITIVE) {
      AST_PredefinedType* p = AST_PredefinedType::narrow_from_decl(act_);
      switch (p->pt()) {
      case AST_PredefinedType::PT_long: elem_sz_ = 4; elem_ = "ACE_CDR::Long"; return;
      case AST_PredefinedType::PT_ulong: elem_sz_ = 4; elem_ = "ACE_CDR::ULong"; return;
      case AST_PredefinedType::PT_longlong: elem_sz_ = 8; elem_ = "ACE_CDR::LongLong"; return;
      case AST_PredefinedType::PT_ulonglong: elem_sz_ = 8; elem_ = "ACE_CDR::ULongLong"; return;
      case AST_PredefinedType::PT_short: elem_sz_ = 2; elem_ = "ACE_CDR::Short"; return;
      case AST_PredefinedType::PT_ushort: elem_sz_ = 2; elem_ = "ACE_CDR::UShort"; return;
      case AST_PredefinedType::PT_float: elem_sz_ = 4; elem_ = "ACE_CDR::Float"; return;
      case AST_PredefinedType::PT_double: elem_sz_ = 8; elem_ = "ACE_CDR::Double"; return;
      case AST_PredefinedType::PT_longdouble: elem_sz_ = 16; elem_ = "ACE_CDR::LongDouble"; return;
      case AST_PredefinedType::PT_char: elem_sz_ = 1; elem_ = "ACE_CDR::Char"; return;
      case AST_PredefinedType::PT_wchar: elem_sz_ = 1; elem_ = "ACE_CDR::WChar"; return; // encoding of wchar length is 1 byte
      case AST_PredefinedType::PT_boolean: elem_sz_ = 1; elem_ = "ACE_CDR::Boolean"; return;
      case AST_PredefinedType::PT_octet: elem_sz_ = 1; elem_ = "ACE_CDR::Octet"; return;
      default: elem_ = get_type_name(*act_); return;
      }
    }
    elem_ = get_type_name(*ast_type_);
  }
}

std::string FieldInfo::string_type(Classification c)
{
  return be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11 ?
    ((c & CL_WIDE) ? "std::wstring" : "std::string") :
    (c & CL_WIDE) ? "TAO::WString_Manager" : "TAO::String_Manager";
}
