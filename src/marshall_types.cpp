/***************************************************************************
    marshall_types.cpp - Derived from the PerlQt sources, see AUTHORS
                         for details
                             -------------------
    begin                : Fri Jul 4 2003
    copyright            : (C) 2003-2006 by Richard Dale
    email                : Richard_Dale@tipitina.demon.co.uk
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#include "marshall_types.h"
#include <smoke/qtcore_smoke.h>

#include <QtCore/qvector.h>
#include <QtCore/qlist.h>
#include <QtCore/qhash.h>
#include <QtCore/qmap.h>

#include <mruby/variable.h>
#include <mruby/array.h>
#include <mruby/string.h>

#include <typeinfo>

#ifdef QT_QTDBUS
#include <QtDBus/QtDBus>
#endif

static bool qtruby_embedded = false;

Q_DECL_EXPORT void
set_qtruby_embedded(bool yn) {
#if !defined(RUBY_INIT_STACK)
    if (yn) {
        qWarning("ERROR: set_qtruby_embedded(true) called but RUBY_INIT_STACK is undefined");
        qWarning("       Upgrade to Ruby 1.8.6 or greater");
	}
#endif
    qtruby_embedded = yn;
}

/*
//
// This function was borrowed from the kross code. It puts out
// an error message and stacktrace on stderr for the current exception.
//
static void
show_exception_message(mrb_state* M)
{
  mrb_value info = mrb_gv_get(M, mrb_intern_lit(M, "$!"));
  mrb_value bt = mrb_funcall(M, info, "backtrace", 0);
    mrb_value message = RARRAY_PTR(bt)[0];
    mrb_value message2 = mrb_str_to_str(M, info);

    QString errormessage = QString("%1: %2 (%3)")
                            .arg( RSTRING_PTR(message) )
                            .arg( RSTRING_PTR(message2) )
                            .arg( mrb_obj_classname(M, info) );
    fprintf(stderr, "%s\n", errormessage.toLatin1().data());

    QString tracemessage;
    for(int i = 1; i < RARRAY_LEN(bt); ++i) {
        if( mrb_type(RARRAY_PTR(bt)[i]) == MRB_TT_STRING ) {
          QString s = QString("%1\n").arg( mrb_string_value_ptr(M, RARRAY_PTR(bt)[i]) );
            Q_ASSERT( ! s.isNull() );
            tracemessage += s;
            fprintf(stderr, "\t%s", s.toLatin1().data());
        }
    }
}
*/

void
smokeStackToQtStack(mrb_state* M, Smoke::Stack stack, void ** o, int start, int end, QList<MocArgument> const& args)
{
	for (int i = start, j = 0; i < end; i++, j++) {
		Smoke::StackItem *si = stack + j;
		switch(args[i].argType) {
		case xmoc_bool:
			o[j] = &si->s_bool;
			break;
		case xmoc_int:
			o[j] = &si->s_int;
			break;
		case xmoc_uint:
			o[j] = &si->s_uint;
			break;
		case xmoc_long:
			o[j] = &si->s_long;
			break;
		case xmoc_ulong:
			o[j] = &si->s_ulong;
			break;
		case xmoc_double:
			o[j] = &si->s_double;
			break;
		case xmoc_charstar:
			o[j] = &si->s_voidp;
			break;
		case xmoc_QString:
			o[j] = si->s_voidp;
			break;
		default:
		{
			const SmokeType &t = args[i].st;
			void *p;
			switch(t.elem()) {
			case Smoke::t_bool:
				p = &si->s_bool;
				break;
			case Smoke::t_char:
				p = &si->s_char;
				break;
			case Smoke::t_uchar:
				p = &si->s_uchar;
				break;
			case Smoke::t_short:
				p = &si->s_short;
				break;
			case Smoke::t_ushort:
				p = &si->s_ushort;
				break;
			case Smoke::t_int:
				p = &si->s_int;
				break;
			case Smoke::t_uint:
				p = &si->s_uint;
				break;
			case Smoke::t_long:
				p = &si->s_long;
				break;
			case Smoke::t_ulong:
				p = &si->s_ulong;
				break;
			case Smoke::t_float:
				p = &si->s_float;
				break;
			case Smoke::t_double:
				p = &si->s_double;
				break;
			case Smoke::t_enum:
			{
				// allocate a new enum value
				Smoke::EnumFn fn = SmokeClass(t).enumFn();
				if (!fn) {
					mrb_warn(M, "Unknown enumeration %S\n", mrb_intern_cstr(M, t.name()));
					p = new int((int)si->s_enum);
					break;
				}
				Smoke::Index id = t.typeId();
				(*fn)(Smoke::EnumNew, id, p, si->s_enum);
				(*fn)(Smoke::EnumFromLong, id, p, si->s_enum);
				// FIXME: MEMORY LEAK
				break;
			}
			case Smoke::t_class:
			case Smoke::t_voidp:
				if (strchr(t.name(), '*') != 0) {
					p = &si->s_voidp;
				} else {
					p = si->s_voidp;
				}
				break;
			default:
				p = 0;
				break;
			}
			o[j] = p;
		}
		}
	}
}

void
smokeStackFromQtStack(mrb_state* M, Smoke::Stack stack, void ** _o, int start, int end, QList<MocArgument> const& args)
{
	for (int i = start, j = 0; i < end; i++, j++) {
		void *o = _o[j];
		switch(args[i].argType) {
		case xmoc_bool:
			stack[j].s_bool = *(bool*)o;
			break;
		case xmoc_int:
			stack[j].s_int = *(int*)o;
			break;
		case xmoc_uint:
			stack[j].s_uint = *(uint*)o;
			break;
		case xmoc_long:
			stack[j].s_long = *(long*)o;
			break;
		case xmoc_ulong:
			stack[j].s_ulong = *(ulong*)o;
			break;
		case xmoc_double:
			stack[j].s_double = *(double*)o;
			break;
		case xmoc_charstar:
			stack[j].s_voidp = o;
			break;
		case xmoc_QString:
			stack[j].s_voidp = o;
			break;
		default:	// case xmoc_ptr:
		{
			const SmokeType &t = args[i].st;
			switch(t.elem()) {
			case Smoke::t_bool:
			stack[j].s_bool = *(bool*)o;
			break;
			case Smoke::t_char:
			stack[j].s_char = *(char*)o;
			break;
			case Smoke::t_uchar:
			stack[j].s_uchar = *(unsigned char*)o;
			break;
			case Smoke::t_short:
			stack[j].s_short = *(short*)o;
			break;
			case Smoke::t_ushort:
			stack[j].s_ushort = *(unsigned short*)o;
			break;
			case Smoke::t_int:
			stack[j].s_int = *(int*)o;
			break;
			case Smoke::t_uint:
			stack[j].s_uint = *(unsigned int*)o;
			break;
			case Smoke::t_long:
			stack[j].s_long = *(long*)o;
			break;
			case Smoke::t_ulong:
			stack[j].s_ulong = *(unsigned long*)o;
			break;
			case Smoke::t_float:
			stack[j].s_float = *(float*)o;
			break;
			case Smoke::t_double:
			stack[j].s_double = *(double*)o;
			break;
			case Smoke::t_enum:
			{
				Smoke::EnumFn fn = SmokeClass(t).enumFn();
				if (!fn) {
					mrb_warn(M, "Unknown enumeration %S\n", mrb_intern_cstr(M, t.name()));
					stack[j].s_enum = *(int*)o;
					break;
				}
				Smoke::Index id = t.typeId();
				(*fn)(Smoke::EnumToLong, id, o, stack[j].s_enum);
			}
			break;
			case Smoke::t_class:
			case Smoke::t_voidp:
				if (strchr(t.name(), '*') != 0) {
					stack[j].s_voidp = *(void **)o;
				} else {
					stack[j].s_voidp = o;
				}
			break;
			}
		}
		}
	}
}

namespace QtRuby {

MethodReturnValueBase::MethodReturnValueBase(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack) :
    Marshall(M),
	_module_index(idx), _stack(stack)
{
	_st.set(idx.smoke, method().ret);
}

const Smoke::Method&
MethodReturnValueBase::method()
{
	return _module_index.smoke->methods[_module_index.index];
}

Smoke::StackItem&
MethodReturnValueBase::item()
{
	return _stack[0];
}

Smoke *
MethodReturnValueBase::smoke()
{
	return _module_index.smoke;
}

SmokeType
MethodReturnValueBase::type()
{
	return _st;
}

void
MethodReturnValueBase::next() {}

bool
MethodReturnValueBase::cleanup()
{
	return false;
}

void
MethodReturnValueBase::unsupported()
{
	mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as return-type of %S::%S",
             mrb_intern_cstr(M, type().name()),
             mrb_intern_cstr(M, classname()),
             mrb_intern_cstr(M, _module_index.smoke->methodNames[method().name]));
}

mrb_value *
MethodReturnValueBase::var()
{
	return _retval;
}

const char *
MethodReturnValueBase::classname()
{
	return _module_index.smoke->className(method().classId);
}


VirtualMethodReturnValue::VirtualMethodReturnValue(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack, mrb_value retval) :
    MethodReturnValueBase(M, idx, stack), _retval2(retval)
{
	_retval = &_retval2;
	Marshall::HandlerFn fn = getMarshallFn(type());
	(*fn)(this);
}

Marshall::Action
VirtualMethodReturnValue::action()
{
	return Marshall::FromVALUE;
}

MethodReturnValue::MethodReturnValue(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack, mrb_value * retval) :
    MethodReturnValueBase(M, idx, stack)
{
	_retval = retval;
	Marshall::HandlerFn fn = getMarshallFn(type());
	(*fn)(this);
}

Marshall::Action
MethodReturnValue::action()
{
	return Marshall::ToVALUE;
}

const char *
MethodReturnValue::classname()
{
	return qstrcmp(MethodReturnValueBase::classname(), "QGlobalSpace") == 0 ? "" : MethodReturnValueBase::classname();
}


MethodCallBase::MethodCallBase(mrb_state* M, ModuleIndex const& idx) : Marshall(M),
	_module_index(idx), _cur(-1), _called(false), _sp(0)
{
}

MethodCallBase::MethodCallBase(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack) :
    Marshall(M),
	_module_index(idx), _stack(stack), _cur(-1), _called(false), _sp(0)
{
}

Smoke *
MethodCallBase::smoke()
{
	return _module_index.smoke;
}

SmokeType
MethodCallBase::type()
{
	return SmokeType(_module_index.smoke, _args[_cur]);
}

Smoke::StackItem &
MethodCallBase::item()
{
	return _stack[_cur + 1];
}

const Smoke::Method &
MethodCallBase::method()
{
	return _module_index.smoke->methods[_module_index.index];
}

void
MethodCallBase::next()
{
	int oldcur = _cur;
	_cur++;
	while(!_called && _cur < items() ) {
		Marshall::HandlerFn fn = getMarshallFn(type());
		(*fn)(this);
		_cur++;
	}

	callMethod();
	_cur = oldcur;
}

void
MethodCallBase::unsupported()
{
	mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as argument of %S::%S",
             mrb_intern_cstr(M, type().name()),
             mrb_intern_cstr(M, classname()),
             mrb_intern_cstr(M, _module_index.smoke->methodNames[method().name]));
}

const char*
MethodCallBase::classname()
{
	return _module_index.smoke->className(method().classId);
}


VirtualMethodCall::VirtualMethodCall(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack, mrb_value obj, mrb_value *sp) :
    MethodCallBase(M, idx, stack), _obj(obj)
{
	_sp = sp;
	_args = idx.smoke->argumentList + method().args;
}

VirtualMethodCall::~VirtualMethodCall()
{
}

Marshall::Action
VirtualMethodCall::action()
{
	return Marshall::ToVALUE;
}

mrb_value *
VirtualMethodCall::var()
{
	return _sp + _cur;
}

int
VirtualMethodCall::items()
{
	return method().numArgs;
}

void
VirtualMethodCall::callMethod()
{
	if (_called) return;
	_called = true;

	mrb_value _retval = mrb_funcall_argv(M, _obj, mrb_intern_cstr(
      M, _module_index.smoke->methodNames[method().name]), method().numArgs, _sp);

  VirtualMethodReturnValue r(M, _module_index, _stack, _retval);
}

bool
VirtualMethodCall::cleanup()
{
	return false;
}

MethodCall::MethodCall(mrb_state* M, ModuleIndex const& idx, mrb_value target, mrb_value *sp, int items) :
    MethodCallBase(M,idx), _target(target), _o(0), _sp(sp), _items(items)
{
	if (not mrb_nil_p(_target)) {
		smokeruby_object *o = value_obj_info(M, _target);
		if (o != 0 && o->ptr != 0) {
			_o = o;
		}
	}

	_args = idx.smoke->argumentList + idx.smoke->methods[idx.index].args;
	_items = idx.smoke->methods[idx.index].numArgs;
	_stack = new Smoke::StackItem[items + 1];
	_retval = mrb_nil_value();
}

MethodCall::~MethodCall()
{
	delete[] _stack;
}

Marshall::Action
MethodCall::action()
{
	return Marshall::FromVALUE;
}

mrb_value *
MethodCall::var()
{
	if (_cur < 0) return &_retval;
	return _sp + _cur;
}

int
MethodCall::items()
{
	return _items;
}

bool
MethodCall::cleanup()
{
	return true;
}

const char *
MethodCall::classname()
{
	return qstrcmp(MethodCallBase::classname(), "QGlobalSpace") == 0 ? "" : MethodCallBase::classname();
}

void MethodCall::callMethod() {
  if(_called) return;
  _called = true;

  if (mrb_nil_p(_target) && !(method().flags & Smoke::mf_static)) {
    mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "%s is not a class method\n", _module_index.smoke->methodNames[method().name]);
  }
	
  Smoke::ClassFn fn = _module_index.smoke->classes[method().classId].classFn;
  void * ptr = 0;

  if (_o != 0) {
    const Smoke::Class &cl = _module_index.smoke->classes[method().classId];

    ptr = _o->smoke->cast(	_o->ptr,
                            _o->classId,
                            _o->smoke->idClass(cl.className, true).index );
  }

  _items = -1;
  (*fn)(method().method, ptr, _stack);
  if (method().flags & Smoke::mf_ctor) {
    Smoke::StackItem s[2];
    s[1].s_voidp = qtruby_modules[_module_index.smoke].binding;
    (*fn)(0, _stack[0].s_voidp, s);
  }
  MethodReturnValue r(M, _module_index, _stack, &_retval);
}

SigSlotBase::SigSlotBase(mrb_state* M, QList<MocArgument> const& args) : Marshall(M), _args(args), _cur(-1), _called(false)
{
	_stack = new Smoke::StackItem[args.count() - 1];
}

SigSlotBase::~SigSlotBase()
{
	delete[] _stack;
}

const MocArgument &
SigSlotBase::arg()
{
	return _args[_cur + 1];
}

SmokeType
SigSlotBase::type()
{
	return arg().st;
}

Smoke::StackItem &
SigSlotBase::item()
{
	return _stack[_cur];
}

mrb_value *
SigSlotBase::var()
{
	return _sp + _cur;
}

Smoke *
SigSlotBase::smoke()
{
	return type().smoke();
}

void
SigSlotBase::unsupported()
{
	mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as %S argument\n",
             mrb_intern_cstr(M, type().name()), mrb_intern_cstr(M, mytype()) );
}

void
SigSlotBase::next()
{
	int oldcur = _cur;
	_cur++;

	while(!_called && _cur < _args.size() - 1) {
		Marshall::HandlerFn fn = getMarshallFn(type());
		(*fn)(this);
		_cur++;
	}

	mainfunction();
	_cur = oldcur;
}

void
SigSlotBase::prepareReturnValue(void** o)
{
	if (_args[0].argType == xmoc_ptr) {
		QByteArray type(_args[0].st.name());
		type.replace("const ", "");
		if (!type.endsWith('*')) {  // a real pointer type, so a simple void* will do
			if (type.endsWith('&')) {
				type.resize(type.size() - 1);
			}
			if (type.startsWith("QList")) {
				o[0] = new QList<void*>;
			} else if (type.startsWith("QVector")) {
				o[0] = new QVector<void*>;
			} else if (type.startsWith("QHash")) {
				o[0] = new QHash<void*, void*>;
			} else if (type.startsWith("QMap")) {
				o[0] = new QMap<void*, void*>;
#ifdef QT_QTDBUS
			} else if (type == "QDBusVariant") {
				o[0] = new QDBusVariant;
#endif
			} else {
				Smoke::ModuleIndex ci = qtcore_Smoke->findClass(type);
				if (ci.index != 0) {
					Smoke::ModuleIndex mi = ci.smoke->findMethod(type, type);
					if (mi.index) {
						Smoke::Class& c = ci.smoke->classes[ci.index];
						Smoke::Method& meth = mi.smoke->methods[mi.smoke->methodMaps[mi.index].method];
						Smoke::StackItem _stack[1];
						c.classFn(meth.method, 0, _stack);
						o[0] = _stack[0].s_voidp;
					}
				}
			}
		}
	} else if (_args[0].argType == xmoc_QString) {
		o[0] = new QString;
	}
}

/*
	Converts a ruby value returned by a slot invocation to a Qt slot
	reply type
*/
class SlotReturnValue : public Marshall {
    QList<MocArgument> const&	_replyType;
    Smoke::Stack _stack;
	mrb_value * _result;
public:
	SlotReturnValue(mrb_state* M, void ** o, mrb_value * result, QList<MocArgument> const& replyType)
      : Marshall(M), _replyType(replyType)
	{
		_result = result;
		_stack = new Smoke::StackItem[1];
		Marshall::HandlerFn fn = getMarshallFn(type());
		(*fn)(this);

		QByteArray t(type().name());
		t.replace("const ", "");
		t.replace("&", "");

		if (t == "QDBusVariant") {
#ifdef QT_QTDBUS
			*reinterpret_cast<QDBusVariant*>(o[0]) = *(QDBusVariant*) _stack[0].s_class;
#endif
		} else {
			// Save any address in zeroth element of the arrary of 'void*'s passed to
			// qt_metacall()
			void * ptr = o[0];
			smokeStackToQtStack(M, _stack, o, 0, 1, _replyType);
			// Only if the zeroth element of the array of 'void*'s passed to qt_metacall()
			// contains an address, is the return value of the slot needed.
			if (ptr != 0) {
				*(void**)ptr = *(void**)(o[0]);
			}
		}
    }

    SmokeType type() {
		return _replyType[0].st;
	}
    Marshall::Action action() { return Marshall::FromVALUE; }
    Smoke::StackItem &item() { return _stack[0]; }
    mrb_value * var() {
    	return _result;
    }

	void unsupported()
	{
		mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as slot reply-type", mrb_intern_cstr(M, type().name()));
    }
	Smoke *smoke() { return type().smoke(); }

	void next() {}

	bool cleanup() { return false; }

	~SlotReturnValue() {
		delete[] _stack;
	}
};

InvokeSlot::InvokeSlot(mrb_state* m, mrb_value obj, mrb_sym slotname, QList<MocArgument> const& args, void ** o) : SigSlotBase(m, args),
    _obj(obj), _slotname(slotname), _o(o)
{
	_sp = (mrb_value*)mrb_malloc(M, sizeof(mrb_value) * (args.size() - 1));
	copyArguments();
}

InvokeSlot::~InvokeSlot()
{
	mrb_free(M, _sp);
}

Marshall::Action
InvokeSlot::action()
{
	return Marshall::ToVALUE;
}

const char *
InvokeSlot::mytype()
{
	return "slot";
}

bool
InvokeSlot::cleanup()
{
	return false;
}

void
InvokeSlot::copyArguments()
{
	smokeStackFromQtStack(M, _stack, _o + 1, 1, _args.size(), _args);
}

void
InvokeSlot::invokeSlot()
{
	if (_called) return;
	_called = true;

  mrb_value result = mrb_funcall_argv(M, _obj, _slotname, _args.size() - 1, _sp);

	if (_args[0].argType != xmoc_void) {
		SlotReturnValue r(M, _o, &result, _args);
	}
}

void
InvokeSlot::mainfunction()
{
	invokeSlot();
}

}
