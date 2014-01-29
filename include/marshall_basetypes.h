/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#ifndef MARSHALL_BASETYPES_H
#define MARSHALL_BASETYPES_H

mrb_int
get_mrb_int(mrb_state* M, mrb_value const& v);

template <class T> T* smoke_ptr(Marshall *m) { return (T*) m->item().s_voidp; }

template<> bool* smoke_ptr<bool>(Marshall *m) { return &m->item().s_bool; }
template<> signed char* smoke_ptr<signed char>(Marshall *m) { return &m->item().s_char; }
template<> unsigned char* smoke_ptr<unsigned char>(Marshall *m) { return &m->item().s_uchar; }
template<> short* smoke_ptr<short>(Marshall *m) { return &m->item().s_short; }
template<> unsigned short* smoke_ptr<unsigned short>(Marshall *m) { return &m->item().s_ushort; }
template<> int* smoke_ptr<int>(Marshall *m) { return &m->item().s_int; }
template<> unsigned int* smoke_ptr<unsigned int>(Marshall *m) { return &m->item().s_uint; }
template<> long* smoke_ptr<long>(Marshall *m) { 	return &m->item().s_long; }
template<> unsigned long* smoke_ptr<unsigned long>(Marshall *m) { return &m->item().s_ulong; }
template<> float* smoke_ptr<float>(Marshall *m) { return &m->item().s_float; }
template<> double* smoke_ptr<double>(Marshall *m) { return &m->item().s_double; }
template<> void* smoke_ptr<void>(Marshall *m) { return m->item().s_voidp; }

template <class T> T ruby_to_primitive(mrb_state*, mrb_value);
template <class T> mrb_value primitive_to_ruby(mrb_state*, T);

template <class T>
static void marshall_from_ruby(Marshall *m)
{
	mrb_value obj = *(m->var());
	(*smoke_ptr<T>(m)) = ruby_to_primitive<T>(m->M, obj);
}

template <class T>
static void marshall_to_ruby(Marshall *m)
{
	*(m->var()) = primitive_to_ruby<T>(m->M, *smoke_ptr<T>(m) );
}

#include "marshall_primitives.h"
#include "marshall_complex.h"

#include <mruby/variable.h>

// Special case marshallers

template <>
void marshall_from_ruby<char *>(Marshall *m)
{
	m->item().s_voidp = ruby_to_primitive<char*>(m->M, *(m->var()));
}

template <>
void marshall_from_ruby<unsigned char *>(Marshall *m)
{
	m->item().s_voidp = ruby_to_primitive<unsigned char*>(m->M, *(m->var()));
}

template <>
void marshall_from_ruby<SmokeEnumWrapper>(Marshall *m)
{
	m->item().s_enum = get_mrb_int(m->M, *(m->var()));
}

template <>
void marshall_to_ruby<SmokeEnumWrapper>(Marshall *m)
{
  RObject* const ret = (RObject*)mrb_obj_alloc(
      m->M, MRB_TT_OBJECT, mrb_class_get_under(m->M, mrb_class_get(m->M, "Qt"), "Enum"));
  mrb_obj_iv_set(m->M, ret, mrb_intern_lit(m->M, "@type"),
                 mrb_symbol_value(mrb_intern_cstr(m->M, m->type().name())));
  mrb_obj_iv_set(m->M, ret, mrb_intern_lit(m->M, "@value"), mrb_fixnum_value(m->item().s_enum));
  *(m->var()) = mrb_obj_value(ret);
}

extern QHash<QByteArray, TypeHandler*> type_handlers;

template <>
void marshall_from_ruby<SmokeClassWrapper>(Marshall *m)
{
	mrb_value v = *(m->var());

	if (mrb_nil_p(v)) {
		m->item().s_class = 0;
		return;
	}

	smokeruby_object *o = value_obj_info(m->M, v);
	if (o == 0 || o->ptr == 0) {
		if(m->type().isRef()) {
			mrb_warn(m->M, "References can't be nil\n");
			m->unsupported();
		}

		m->item().s_class = 0;
		return;
	}

	void *ptr = o->ptr;
	if (!m->cleanup() && m->type().isStack()) {
		ptr = construct_copy(o);
		if (do_debug & qtdb_gc) {
			qWarning("copying %s %p to %p", resolve_classname(o), o->ptr, ptr);
		}
	}


	const Smoke::Class &cl = m->smoke()->classes[m->type().classId()];

	ptr = o->smoke->cast(
		ptr,				// pointer
		o->classId,				// from
		o->smoke->idClass(cl.className, true).index	// to
		);

	m->item().s_class = ptr;
	return;
}

template <>
void marshall_to_ruby<SmokeClassWrapper>(Marshall *m)
{
	if (m->item().s_voidp == 0) {
		*(m->var()) = mrb_nil_value();
		return;
	}
	void *p = m->item().s_voidp;
	mrb_value obj = getPointerObject(m->M, p);
	if (not mrb_nil_p(obj)) {
    const char* ruby_class  = mrb_obj_classname(m->M, obj);
    const char* smoke_class = m->type().name();

    if ((strcmp(ruby_class, "Qt::TextEdit::ExtraSelection") == 0) && (strcmp(smoke_class, "QTextCursor&") == 0))
    {
      /* Intentional Fall Through for ExtraSelection returning a cursor */
    }
    else
    {
		  *(m->var()) = obj;
		  return ;
    }
	}

	smokeruby_object  * o = alloc_smokeruby_object(m->M, false, m->smoke(), m->type().classId(), p);

	const char * classname = resolve_classname(o);
	if (m->type().isConst() && m->type().isRef()) {
		p = construct_copy( o );
		if (do_debug & qtdb_gc) {
			qWarning("copying %s %p to %p", classname, o->ptr, p);
		}

		if (p) {
			o->ptr = p;
			o->allocated = true;
		}
	}

	obj = set_obj_info(m->M, classname, o);
	if (do_debug & qtdb_gc) {
		qWarning("allocating %s %p -> %s", classname, o->ptr, mrb_string_value_ptr(m->M, obj));
	}

	if(m->type().isStack()) {
		o->allocated = true;
		// Keep a mapping of the pointer so that it is only wrapped once as a ruby VALUE
		mapPointer(m->M, obj, o, o->classId, 0);
	}

	*(m->var()) = obj;
}

template <>
void marshall_to_ruby<char *>(Marshall *m)
{
	char *sv = (char*)m->item().s_voidp;
	mrb_value obj;
	if(sv)
		obj = mrb_str_new_cstr(m->M, sv);
	else
		obj = mrb_nil_value();

	if(m->cleanup())
		delete[] sv;

	*(m->var()) = obj;
}

template <>
void marshall_to_ruby<unsigned char *>(Marshall *m)
{
	m->unsupported();
}

#endif
// kate: space-indent false;
