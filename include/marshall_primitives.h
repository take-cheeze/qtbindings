/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#ifndef MARSHALL_PRIMITIVES_H
#define MARSHALL_PRIMITIVES_H

#include <mruby/variable.h>
#include <mruby/string.h>

template <>
bool ruby_to_primitive<bool>(mrb_state* M, mrb_value v)
{
	if (mrb_type(v) == MRB_TT_OBJECT) {
		// A Qt::Boolean has been passed as a value
		return mrb_test(mrb_iv_get(M, v, mrb_intern_lit(M, "@value")));
	} else {
		return mrb_test(v);
	}
}

template <>
mrb_value primitive_to_ruby<bool>(mrb_state*, bool sv)
{
	return mrb_bool_value(sv);
}

char
get_mrb_char(mrb_state* M, mrb_value const& v) {
  mrb_int len = 0;
  char const* str = NULL;
  if(mrb_symbol_p(v)) {
    str = mrb_sym2name_len(M, mrb_symbol(v), &len);
  } else if(mrb_string_p(v)) {
    len = RSTRING_LEN(v);
    str = RSTRING_PTR(v);
  } else {
    return get_mrb_int(M, v);
  }
  assert(len == 1);
  return str[0];
}

template <>
signed char ruby_to_primitive<signed char>(mrb_state* M, mrb_value v)
{
  return get_mrb_char(M, v);
}

template <>
mrb_value primitive_to_ruby<signed char>(mrb_state*, signed char sv)
{
	return mrb_fixnum_value(sv);
}

template <>
unsigned char ruby_to_primitive<unsigned char>(mrb_state* M, mrb_value v)
{
  return get_mrb_char(M, v);
}

template <>
mrb_value primitive_to_ruby<unsigned char>(mrb_state*, unsigned char sv)
{
	return mrb_fixnum_value(sv);
}

template <>
short ruby_to_primitive<short>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<short>(mrb_state*, short sv)
{
	return mrb_fixnum_value(sv);
}

template <>
unsigned short ruby_to_primitive<unsigned short>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<unsigned short>(mrb_state*, unsigned short sv)
{
	return mrb_fixnum_value((unsigned int) sv);
}

template <>
int ruby_to_primitive<int>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<int>(mrb_state*, int sv)
{
	return mrb_fixnum_value(sv);
}

template <>
unsigned int ruby_to_primitive<unsigned int>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<unsigned int>(mrb_state*, unsigned int sv)
{
	return mrb_fixnum_value(sv);
}

template <>
long ruby_to_primitive<long>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<long>(mrb_state*, long sv)
{
	return mrb_fixnum_value(sv);
}

template <>
unsigned long ruby_to_primitive<unsigned long>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<unsigned long>(mrb_state*, unsigned long sv)
{
	return mrb_fixnum_value(sv);
}

template <>
long long ruby_to_primitive<long long>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<long long>(mrb_state*, long long sv)
{
	return mrb_fixnum_value(sv);
}

template <>
unsigned long long ruby_to_primitive<unsigned long long>(mrb_state* M, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<unsigned long long>(mrb_state*, unsigned long long sv)
{
	return mrb_fixnum_value(sv);
}

template <>
float ruby_to_primitive<float>(mrb_state*, mrb_value v)
{
  assert(mrb_nil_p(v) || mrb_float_p(v));
  return mrb_nil_p(v)? 0.0 : (float) mrb_float(v);
}

template <>
mrb_value primitive_to_ruby<float>(mrb_state* M, float sv)
{
	return mrb_float_value(M, (double) sv);
}

template <>
double ruby_to_primitive<double>(mrb_state*, mrb_value v)
{
  assert(mrb_nil_p(v) || mrb_float_p(v) || mrb_fixnum_p(v));
  return
      mrb_nil_p(v)? 0.0:
      mrb_fixnum_p(v)? mrb_fixnum(v):
      (double) mrb_float(v);
}

template <>
mrb_value primitive_to_ruby<double>(mrb_state* M, double sv)
{
	return mrb_float_value(M, (double) sv);
}

template <>
char* ruby_to_primitive<char *>(mrb_state* M, mrb_value rv)
{
	return mrb_nil_p(rv)? 0 : mrb_string_value_ptr(M, rv);
}

template <>
unsigned char* ruby_to_primitive<unsigned char *>(mrb_state* M, mrb_value rv)
{
	return mrb_nil_p(rv)? 0 : (unsigned char*)mrb_string_value_ptr(M, rv);
}

template <>
mrb_value primitive_to_ruby<int*>(mrb_state* M, int* sv)
{
	return sv? primitive_to_ruby<int>(M, *sv) : mrb_nil_value();
}

#if defined(Q_OS_WIN32)
template <>
WId ruby_to_primitive<WId>(mrb_state*, mrb_value v)
{
  return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<WId>(mrb_state*, WId sv)
{
	return mrb_fixnum_value(sv);
}

template <>
Q_PID ruby_to_primitive<Q_PID>(mrb_state*, mrb_value v)
{
	return get_mrb_int(M, v);
}

template <>
mrb_value primitive_to_ruby<Q_PID>(mrb_state*, Q_PID sv)
{
	return mrb_fixnum_value((unsigned long) sv);
}
#endif

#endif
