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
	mrb_value v = *(m->var());

	if (mrb_nil_p(v)) {
		m->item().s_enum = 0;
	} else if (mrb_type(v) == MRB_TT_OBJECT) {
		// Both Qt::Enum and Qt::Integer have a value() method, so 'get_qinteger()' can be called ok
		mrb_value temp = mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "get_qinteger", 1, v);
		m->item().s_enum = (long) mrb_fixnum(temp);
	} else {
		m->item().s_enum = (long) mrb_fixnum(v);
	}

}

template <>
void marshall_to_ruby<SmokeEnumWrapper>(Marshall *m)
{
	long val = m->item().s_enum;
  *(m->var()) = mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "create_qenum",
                            2, mrb_fixnum_value(val), mrb_str_new_cstr(m->M, m->type().name()) );
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
    const char* ruby_class  = RSTRING_PTR(mrb_funcall(m->M, mrb_funcall(m->M, obj, "class", 0), "to_s", 0));
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

/*
	if(m->type().isStack()) {
		o->allocated = true;
		// Keep a mapping of the pointer so that it is only wrapped once as a ruby VALUE
		mapPointer(obj, o, o->classId, 0);
	}
*/			

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
