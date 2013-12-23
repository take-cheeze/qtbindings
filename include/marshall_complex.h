/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#ifndef MARSHALL_COMPLEX_H
#define MARSHALL_COMPLEX_H

#include <QtCore/qlist.h>
#include <QtCore/qlinkedlist.h>
#include <QtCore/qvector.h>

template <>
void marshall_from_ruby<long long>(Marshall *m) 
{
	mrb_value obj = *(m->var());
	m->item().s_voidp = new long long;
	*(long long *)m->item().s_voidp = ruby_to_primitive<long long>(m->M, obj);
	
	m->next();
	
	if(m->cleanup() && m->type().isConst()) {
		delete (long long int *) m->item().s_voidp;
	}	
}

template <>
void marshall_from_ruby<unsigned long long>(Marshall *m) 
{
	mrb_value obj = *(m->var());
	m->item().s_voidp = new unsigned long long;
	*(long long *)m->item().s_voidp = ruby_to_primitive<unsigned long long>(m->M, obj);

	m->next();
	
	if(m->cleanup() && m->type().isConst()) {
		delete (long long int *) m->item().s_voidp;
	}	
}

template <>
void marshall_from_ruby<int *>(Marshall *m) 
{
	mrb_value rv = *(m->var());
	int *i = new int;
	
	if (mrb_nil_p(rv)) {
		m->item().s_voidp = 0;
		return;
	} else if (mrb_type(rv) == MRB_TT_OBJECT) {
		// A Qt::Integer has been passed as an integer value
		mrb_value temp = mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "get_qinteger", 1, rv);
		*i = mrb_fixnum(temp);
		m->item().s_voidp = i;
		m->next();
		mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "set_qinteger", 2, rv, mrb_fixnum_value(*i));
		rv = temp;
	} else {
		*i = mrb_fixnum(rv);
		m->item().s_voidp = i;
		m->next();
	}

	if(m->cleanup() && m->type().isConst()) {
		delete i;
	} else {
		m->item().s_voidp = new int((int)mrb_fixnum(rv));
	}
}

template <>
void marshall_to_ruby<int *>(Marshall *m)
{
	int *ip = (int*)m->item().s_voidp;
	mrb_value rv = *(m->var());
	if(!ip) {
		rv = mrb_nil_value();
		return;
	}
	
	*(m->var()) = mrb_fixnum_value(*ip);
	m->next();
	if(!m->type().isConst())
		*ip = mrb_fixnum(*(m->var()));
}

template <>
void marshall_from_ruby<unsigned int *>(Marshall *m) 
{
	mrb_value rv = *(m->var());
	unsigned int *i = new unsigned int;
	
	if (mrb_nil_p(rv)) {
		m->item().s_voidp = 0;
		return;
	} else if (mrb_type(rv) == MRB_TT_OBJECT) {
		// A Qt::Integer has been passed as an integer value
		mrb_value temp = mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "get_qinteger", 1, rv);
		*i = mrb_fixnum(temp);
		m->item().s_voidp = i;
		m->next();
		mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "set_qinteger", 2, rv, mrb_fixnum_value(*i));
		rv = temp;
	} else {
		*i = mrb_fixnum(rv);
		m->item().s_voidp = i;
		m->next();
	}

	if(m->cleanup() && m->type().isConst()) {
		delete i;
	} else {
		m->item().s_voidp = new int((int)mrb_fixnum(rv));
	}
}

template <>
void marshall_to_ruby<unsigned int *>(Marshall *m)
{
	unsigned int *ip = (unsigned int*) m->item().s_voidp;
	mrb_value rv = *(m->var());
	if (ip == 0) {
		rv = mrb_nil_value();
		return;
	}
	
	*(m->var()) = mrb_fixnum_value(*ip);
	m->next();
	if(!m->type().isConst())
		*ip = mrb_fixnum(*(m->var()));
}

template <>
void marshall_from_ruby<bool *>(Marshall *m) 
{
   mrb_value rv = *(m->var());
	bool * b = new bool;

	if (mrb_type(rv) == MRB_TT_OBJECT) {
		// A Qt::Boolean has been passed as a value
		mrb_value temp = mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "get_qboolean", 1, rv);
		*b = mrb_test(temp);
		m->item().s_voidp = b;
		m->next();
		mrb_funcall(m->M, mrb_obj_value(qt_internal_module(m->M)), "set_qboolean", 2, rv, mrb_bool_value(*b));
	} else {
		*b = mrb_test(rv);
		m->item().s_voidp = b;
		m->next();
	}

	if(m->cleanup() && m->type().isConst()) {
		delete b;
	}
}

template <>
void marshall_to_ruby<bool *>(Marshall *m)
{
	bool *ip = (bool*)m->item().s_voidp;
	if(!ip) {
		*(m->var()) = mrb_nil_value();
		return;
	}
	*(m->var()) = mrb_bool_value(*ip);
	m->next();
	if(!m->type().isConst())
    *ip = mrb_test(*(m->var()));
}

#endif
