/***************************************************************************
                          Qt.cpp  -  description
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdarg.h>

#include <iostream>

#include <QtCore/qabstractitemmodel.h>
#include <QtCore/qglobal.h>
#include <QtCore/qhash.h>
#include <QtCore/qline.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qobject.h>
#include <QtCore/qrect.h>
#include <QtCore/qregexp.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qmutex.h>
#include <QtGui/qapplication.h>
#include <QtGui/qbitmap.h>
#include <QtGui/qcolor.h>
#include <QtGui/qcursor.h>
#include <QtGui/qfont.h>
#include <QtGui/qicon.h>
#include <QtGui/qitemselectionmodel.h>
#include <QtGui/qpalette.h>
#include <QtGui/qpen.h>
#include <QtGui/qpixmap.h>
#include <QtGui/qpolygon.h>
#include <QtGui/qtextformat.h>
#include <QtGui/qwidget.h>

#ifdef QT_QTDBUS
#include <QtDBus/qdbusargument.h>
#endif

#undef DEBUG
#ifndef __USE_POSIX
#define __USE_POSIX
#endif
#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif
#ifdef _BOOL
#define HAS_BOOL
#endif

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/class.h>
#include <mruby/data.h>

#include <smoke.h>
#include <smoke/qtcore_smoke.h>

#include "marshall.h"
#include "qtruby.h"
#include "smokeruby.h"
#include "smoke.h"
#include "marshall_types.h"
// #define DEBUG

extern "C" {
bool application_terminated = false;
}

QList<Smoke*> smokeList;
QHash<Smoke*, QtRubyModule> qtruby_modules;

#ifdef DEBUG
int do_debug = qtdb_gc;
#else
int do_debug = qtdb_none;
#endif

typedef QHash<void *, SmokeValue> PointerMap;
static QMutex pointer_map_mutex;
Q_GLOBAL_STATIC(PointerMap, pointer_map)
int object_count = 0;

// FIXME:
// Don't the two following hashs create memory leaks by using pointers to Smoke::(Module)Index ?
QHash<QByteArray, Smoke::ModuleIndex *> methcache;
QHash<QByteArray, Smoke::ModuleIndex *> classcache;

QHash<Smoke::ModuleIndex, QByteArray*> IdToClassNameMap;

#define logger logger_backend

Smoke::ModuleIndex _current_method;

smokeruby_object *
alloc_smokeruby_object(mrb_state* M, bool allocated, Smoke * smoke, int classId, void * ptr)
{
  smokeruby_object * o = (smokeruby_object*)mrb_malloc(M, sizeof(smokeruby_object));
	o->classId = classId;
	o->smoke = smoke;
	o->ptr = ptr;
	o->allocated = allocated;
	return o;
}

void
free_smokeruby_object(mrb_state* M, smokeruby_object* o)
{
  if(not o) { return; }

	o->ptr = 0;
	mrb_free(M, o);
	return;
}

void
free_smokeruby_object(mrb_state* M, void* p)
{
  smokeruby_object* o = reinterpret_cast<smokeruby_object*>(p);
  free_smokeruby_object(M, o);
}

mrb_data_type const smokeruby_type = {
  "smokeruby_object", &free_smokeruby_object };

smokeruby_object *value_obj_info(mrb_state* M, mrb_value ruby_value) {  // ptr on success, null on fail
	if (mrb_type(ruby_value) != MRB_TT_DATA) {
		return 0;
	}

    smokeruby_object * o = 0;
    Data_Get_Struct(M, ruby_value, &smokeruby_type, o);
    return o;
}

void *value_to_ptr(mrb_state* M, mrb_value ruby_value) {  // ptr on success, null on fail
  smokeruby_object *o = value_obj_info(M, ruby_value);
    return o;
}

mrb_value getPointerObject(mrb_state* M, void *ptr) {
  return getSmokeValue(M, ptr).value;
}

SmokeValue getSmokeValue(mrb_state* M, void *ptr) {
  pointer_map_mutex.lock();

	if (!pointer_map() || !pointer_map()->contains(ptr)) {
		if (do_debug & qtdb_gc) {
			qWarning("getPointerObject %p -> nil", ptr);
			if (!pointer_map()) {
				qWarning("getPointerObject pointer_map deleted");
			}
		}
      pointer_map_mutex.unlock();
	    return SmokeValue();
	} else {
		if (do_debug & qtdb_gc) {
			qWarning("getPointerObject %p -> %s", ptr, mrb_string_value_ptr(M, pointer_map()->operator[](ptr).value));
		}
    pointer_map_mutex.unlock();
		return pointer_map()->operator[](ptr);
	}
}

void unmapPointer(void *ptr, Smoke *smoke, Smoke::Index fromClassId, Smoke::Index toClassId, void *lastptr) {
  pointer_map_mutex.lock();
	ptr = smoke->cast(ptr, fromClassId, toClassId);

	if (ptr != lastptr) {
		lastptr = ptr;
		if (pointer_map() && pointer_map()->contains(ptr)) {
			mrb_value obj_ptr = pointer_map()->operator[](ptr).value;

			if (do_debug & qtdb_gc) {
				const char *className = smoke->classes[fromClassId].className;
				qWarning("unmapPointer (%s*)%p -> %p size: %d", className, ptr, (void*)(&obj_ptr), pointer_map()->size() - 1);
			}

			pointer_map()->remove(ptr);
		}
	}

	if (smoke->classes[toClassId].external) {
		// encountered external class
		Smoke::ModuleIndex mi = Smoke::findClass(smoke->classes[toClassId].className);
		if (!mi.index || !mi.smoke) return;

		smoke = mi.smoke;
		toClassId = mi.index;
	}

  pointer_map_mutex.unlock();
	for (Smoke::Index *i = smoke->inheritanceList + smoke->classes[toClassId].parents; *i; i++) {
		unmapPointer(ptr, smoke, toClassId, *i, lastptr);
	}

}

void unmapPointer(smokeruby_object *o, Smoke::Index classId, void *lastptr) {
	unmapPointer(o->ptr, o->smoke, o->classId, classId, lastptr);
}

// Store pointer in pointer_map hash : "pointer_to_Qt_object" => weak ref to associated Ruby object
// Recurse to store it also as casted to its parent classes.

void mapPointer(mrb_state* M, mrb_value obj, smokeruby_object* o, void *ptr, Smoke *smoke, Smoke::Index fromClassId, Smoke::Index toClassId, void *lastptr) {
  pointer_map_mutex.unlock();

	ptr = smoke->cast(ptr, fromClassId, toClassId);

    if (ptr != lastptr) {
		lastptr = ptr;

		if (do_debug & qtdb_gc) {
			const char *className = smoke->classes[fromClassId].className;
			qWarning("mapPointer (%s*)%p -> %s size: %d", className, ptr, mrb_string_value_ptr(M, obj), pointer_map()->size() + 1);
		}

        SmokeValue value(obj, o);
		pointer_map()->insert(ptr, value);
    }

	if (smoke->classes[toClassId].external) {
		// encountered external class
		Smoke::ModuleIndex mi = Smoke::findClass(smoke->classes[toClassId].className);
		if (!mi.index || !mi.smoke) return;

		smoke = mi.smoke;
		toClassId = mi.index;
	}

  pointer_map_mutex.unlock();
	for (Smoke::Index *i = smoke->inheritanceList + smoke->classes[toClassId].parents; *i; i++) {
		mapPointer(M, obj, o, ptr, smoke, toClassId, *i, lastptr);
	}

	return;
}

void mapPointer(mrb_state* M, mrb_value obj, smokeruby_object *o, Smoke::Index classId, void *lastptr) {
	mapPointer(M, obj, o, o->ptr, o->smoke, o->classId, classId, lastptr);
}

namespace QtRuby {

Binding::Binding() : SmokeBinding(0), M(NULL) {}
Binding::Binding(mrb_state* M, Smoke *s) : SmokeBinding(s), M(M) {}

void
Binding::deleted(Smoke::Index classId, void *ptr) {
	if (!pointer_map()) {
	return;
	}

	smokeruby_object *o = getSmokeValue(M, ptr).o;
	if (do_debug & qtdb_gc) {
	  	qWarning("unmapping: o = %p, ptr = %p\n", o, ptr);
    	qWarning("%p->~%s()", ptr, smoke->className(classId));
    }
	if (!o || !o->ptr) {
    	return;
	}
	unmapPointer(o, o->classId, 0);
	o->ptr = 0;
}

bool
Binding::callMethod(Smoke::Index method, void *ptr, Smoke::Stack args, bool /*isAbstract*/) {
	mrb_value obj = getPointerObject(M, ptr);
	smokeruby_object *o = value_obj_info(M, obj);

	if (do_debug & qtdb_virtual) {
		const Smoke::Method & meth = smoke->methods[method];
		QByteArray signature(smoke->methodNames[meth.name]);
		signature += "(";
			for (int i = 0; i < meth.numArgs; i++) {
		if (i != 0) signature += ", ";
			signature += smoke->types[smoke->argumentList[meth.args + i]].name;
		}
		signature += ")";
		if (meth.flags & Smoke::mf_const) {
			signature += " const";
		}
		qWarning(	"module: %s virtual %p->%s::%s called",
					smoke->moduleName(),
					ptr,
					smoke->classes[smoke->methods[method].classId].className,
					(const char *) signature );
	}

	if (o == 0) {
    	if( do_debug & qtdb_virtual )   // if not in global destruction
			qWarning("Cannot find object for virtual method %p -> %p", ptr, &obj);
    	return false;
	}
	const char *methodName = smoke->methodNames[smoke->methods[method].name];
	if (qstrncmp(methodName, "operator", sizeof("operator") - 1) == 0) {
		methodName += (sizeof("operator") - 1);
	}

  /*
  // If not in a ruby thread, just call the C++ version
	// During GC, avoid checking for override and just call the C++ version
  // If the virtual method hasn't been overriden, just call the C++ one.
#ifdef HAVE_RUBY_RUBY_H
  int ruby_thread = ruby_native_thread_p();
	if ((ruby_thread == 0) || mrb_during_gc() || mrb_respond_to(obj, mrb_intern(methodName)) == 0) {
    return false;
	}
#else
  if (rb_during_gc() || ruby_stack_check() || mrb_respond_to(obj, mrb_intern(methodName)) == 0) {
    return false;
  }
#endif
  */
  if (mrb_respond_to(M, obj, mrb_intern_cstr(M, methodName)) == 0) {
    return false;
  }

	QtRuby::VirtualMethodCall c(M, smoke, method, args, obj, (mrb_value*)mrb_malloc(M, sizeof(mrb_value) * smoke->methods[method].numArgs));
	c.next();
	return true;
}

char*
Binding::className(Smoke::Index classId) {
	Smoke::ModuleIndex mi(smoke, classId);
	return (char *) (const char *) *(IdToClassNameMap.value(mi));
}

/*
	Converts a C++ value returned by a signal invocation to a Ruby
	reply type
*/
class SignalReturnValue : public Marshall {
    QList<MocArgument*>	_replyType;
    Smoke::Stack _stack;
	mrb_value * _result;
public:
	SignalReturnValue(mrb_state* M, void ** o, mrb_value * result, QList<MocArgument*> replyType)
      : Marshall(M)
	{
		_result = result;
		_replyType = replyType;
		_stack = new Smoke::StackItem[1];
		smokeStackFromQtStack(M, _stack, o, 0, 1, _replyType);
		Marshall::HandlerFn fn = getMarshallFn(type());
		(*fn)(this);
    }

    SmokeType type() {
		return _replyType[0]->st;
	}
    Marshall::Action action() { return Marshall::ToVALUE; }
    Smoke::StackItem &item() { return _stack[0]; }
    mrb_value * var() {
    	return _result;
    }

	void unsupported()
	{
		mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as signal reply-type", mrb_str_new_cstr(M, type().name()));
    }
	Smoke *smoke() { return type().smoke(); }

	void next() {}

	bool cleanup() { return false; }

	~SignalReturnValue() {
		delete[] _stack;
	}
};

/* Note that the SignalReturnValue and EmitSignal classes really belong in
	marshall_types.cpp. However, for unknown reasons they don't link with certain
	versions of gcc. So they were moved here in to work round that bug.
*/
EmitSignal::EmitSignal(mrb_state* M, QObject *obj, int id, int /*items*/, QList<MocArgument*> args, mrb_value *sp, mrb_value * result) : SigSlotBase(M, args),
    _obj(obj), _id(id)
{
	_sp = sp;
	_result = result;
}

Marshall::Action
EmitSignal::action()
{
	return Marshall::FromVALUE;
}

Smoke::StackItem &
EmitSignal::item()
{
	return _stack[_cur];
}

const char *
EmitSignal::mytype()
{
	return "signal";
}

void
EmitSignal::emitSignal()
{
	if (_called) return;
	_called = true;
	void ** o = new void*[_items];
	smokeStackToQtStack(M, _stack, o + 1, 1, _items, _args);
	void * ptr;
	o[0] = &ptr;
	prepareReturnValue(o);

	_obj->metaObject()->activate(_obj, _id, o);

	if (_args[0]->argType != xmoc_void) {
		SignalReturnValue r(M, o, _result, _args);
	}
	delete[] o;
}

void
EmitSignal::mainfunction()
{
	emitSignal();
}

bool
EmitSignal::cleanup()
{
	return true;
}

InvokeNativeSlot::InvokeNativeSlot(mrb_state* M, QObject *obj, int id, int /*items*/, QList<MocArgument*> args, mrb_value *sp, mrb_value * result) : SigSlotBase(M, args),
    _obj(obj), _id(id)
{
	_sp = sp;
	_result = result;
}

Marshall::Action
InvokeNativeSlot::action()
{
	return Marshall::FromVALUE;
}

Smoke::StackItem &
InvokeNativeSlot::item()
{
	return _stack[_cur];
}

const char *
InvokeNativeSlot::mytype()
{
	return "slot";
}

void
InvokeNativeSlot::invokeSlot()
{
	if (_called) return;
	_called = true;
	void ** o = new void*[_items];
	smokeStackToQtStack(M, _stack, o + 1, 1, _items, _args);
	void * ptr;
	o[0] = &ptr;
	prepareReturnValue(o);

	_obj->qt_metacall(QMetaObject::InvokeMetaMethod, _id, o);

	if (_args[0]->argType != xmoc_void) {
		SignalReturnValue r(M, o, _result, _args);
	}
	delete[] o;
}

void
InvokeNativeSlot::mainfunction()
{
	invokeSlot();
}

bool
InvokeNativeSlot::cleanup()
{
	return true;
}

}

mrb_value rb_str_catf(mrb_state* M, mrb_value self, const char *format, ...)
{
#define CAT_BUFFER_SIZE 2048
static char p[CAT_BUFFER_SIZE];
	va_list ap;
	va_start(ap, format);
    qvsnprintf(p, CAT_BUFFER_SIZE, format, ap);
	p[CAT_BUFFER_SIZE - 1] = '\0';
	mrb_str_cat_cstr(M, self, p);
	va_end(ap);
	return self;
}

const char *
resolve_classname(smokeruby_object * o)
{
	if (Smoke::isDerivedFrom(o->smoke->classes[o->classId].className, "QObject")) {
		QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
		const QMetaObject * meta = qobject->metaObject();

		while (meta != 0) {
			Smoke::ModuleIndex mi = o->smoke->findClass(meta->className());
			o->smoke = mi.smoke;
			o->classId = mi.index;
			if (o->smoke != 0) {
				if (o->classId != 0) {
					return qtruby_modules[o->smoke].binding->className(o->classId);
				}
			}

			meta = meta->superClass();
		}
	}

    if (o->smoke->classes[o->classId].external) {
        Smoke::ModuleIndex mi = o->smoke->findClass(o->smoke->className(o->classId));
        o->smoke = mi.smoke;
        o->classId = mi.index;
        return qtruby_modules.value(mi.smoke).resolve_classname(o);
    }
    return qtruby_modules.value(o->smoke).resolve_classname(o);
}

mrb_value
findMethod(mrb_state* M, mrb_value /*self*/)
{
  mrb_value c_value, name_value;
  mrb_get_args(M, "oo", &c_value, &name_value);
    char *c = mrb_string_value_ptr(M, c_value);
    char *name = mrb_string_value_ptr(M, name_value);
    mrb_value result = mrb_ary_new(M);
    Smoke::ModuleIndex classId = Smoke::findClass(c);
    Smoke::ModuleIndex meth = Smoke::NullModuleIndex;
    QList<Smoke::ModuleIndex> milist;
    if (classId.smoke != 0) {
        meth = classId.smoke->findMethod(c, name);
    }
#ifdef DEBUG
    if (do_debug & qtdb_calls) qWarning("Found method %s::%s => %d", c, name, meth.index);
#endif
    if(!meth.index) {
        // since every smoke module defines a class 'QGlobalSpace' we can't rely on the classMap,
        // so we search for methods by hand
        foreach (Smoke* s, smokeList) {
            Smoke::ModuleIndex cid = s->idClass("QGlobalSpace");
            Smoke::ModuleIndex mnid = s->idMethodName(name);
            if (!cid.index || !mnid.index) continue;
            meth = s->idMethod(cid.index, mnid.index);
            if (meth.index) milist.append(meth);
        }
#ifdef DEBUG
        if (do_debug & qtdb_calls) qWarning("Found method QGlobalSpace::%s => %d", name, meth.index);
#endif
    }
    else
    {
        milist.append(meth);
    }

    if (milist.count() == 0) {
        return result;
    // empty list
    } else {
        foreach (Smoke::ModuleIndex meth, milist) {
            if (meth.index > 0) {
                Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
                if (i == 0) {		// shouldn't happen
                  mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Corrupt method %S::%S", mrb_str_new_cstr(M, c), mrb_str_new_cstr(M, name));
                } else if(i > 0) {	// single match
                    const Smoke::Method &methodRef = meth.smoke->methods[i];
                    if ((methodRef.flags & Smoke::mf_internal) == 0) {
                      mrb_ary_push(M, result, mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(meth.smoke)), mrb_fixnum_value(i)));
                    }
                } else {		// multiple match
                    i = -i;		// turn into ambiguousMethodList index
                    while (meth.smoke->ambiguousMethodList[i]) {
                        const Smoke::Method &methodRef = meth.smoke->methods[meth.smoke->ambiguousMethodList[i]];
                        if ((methodRef.flags & Smoke::mf_internal) == 0) {
                          mrb_ary_push(M, result, mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(meth.smoke)), mrb_fixnum_value(meth.smoke->ambiguousMethodList[i])));
//#ifdef DEBUG
                            if (do_debug & qtdb_calls) qWarning("Ambiguous Method %s::%s => %d", c, name, meth.smoke->ambiguousMethodList[i]);
//#endif

                        }
                        i++;
                    }
	        }
            }
        }
    }
    return result;
}

// findAllMethods(ModuleIndex [, startingWith]) : returns { "mungedName" => [index in methods, ...], ... }

mrb_value
findAllMethods(mrb_state* M, mrb_value /*self*/)
{
  mrb_value* argv; int argc;
  mrb_get_args(M, "*", &argv, &argc);
    mrb_value rb_mi = argv[0];
    mrb_value result = mrb_hash_new(M);
    if (not mrb_nil_p(rb_mi)) {
      Smoke::Index c = (Smoke::Index) mrb_fixnum(mrb_funcall(M, rb_mi, "index", 0));
      Smoke *smoke = smokeList[mrb_fixnum(mrb_funcall(M, rb_mi, "smoke", 0))];
        if (c > smoke->numClasses) {
          return mrb_nil_value();
        }
        char * pat = 0L;
        if(argc > 1 && mrb_type(argv[1]) == MRB_TT_STRING)
          pat = mrb_string_value_ptr(M, argv[1]);
#ifdef DEBUG
        if (do_debug & qtdb_calls) qWarning("findAllMethods called with classid = %d, pat == %s", c, pat);
#endif
        Smoke::Index imax = smoke->numMethodMaps;
        Smoke::Index imin = 0, icur = -1, methmin, methmax;
        methmin = -1; methmax = -1; // kill warnings
        int icmp = -1;
        while(imax >= imin) {
            icur = (imin + imax) / 2;
            icmp = smoke->leg(smoke->methodMaps[icur].classId, c);
            if (icmp == 0) {
                Smoke::Index pos = icur;
                while (icur && smoke->methodMaps[icur-1].classId == c)
                    icur --;
                methmin = icur;
                icur = pos;
                while(icur < imax && smoke->methodMaps[icur+1].classId == c)
                    icur ++;
                methmax = icur;
                break;
            }
            if (icmp > 0)
                imax = icur - 1;
            else
                imin = icur + 1;
        }
        if (icmp == 0) {
            for (Smoke::Index i = methmin; i <= methmax; i++) {
                Smoke::Index m = smoke->methodMaps[i].name;
                if (pat == 0L || strncmp(smoke->methodNames[m], pat, strlen(pat)) == 0) {
                    Smoke::Index ix = smoke->methodMaps[i].method;
                    mrb_value meths = mrb_ary_new(M);
                    if (ix >= 0) {	// single match
                        const Smoke::Method &methodRef = smoke->methods[ix];
                        if ((methodRef.flags & Smoke::mf_internal) == 0) {
                          mrb_ary_push(M, meths, mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(smoke)), mrb_fixnum_value((int) ix)));
                        }
                    } else {		// multiple match
                        ix = -ix;		// turn into ambiguousMethodList index
                        while (smoke->ambiguousMethodList[ix]) {
                            const Smoke::Method &methodRef = smoke->methods[smoke->ambiguousMethodList[ix]];
                            if ((methodRef.flags & Smoke::mf_internal) == 0) {
                              mrb_ary_push(M, meths, mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(smoke)), mrb_fixnum_value((int)smoke->ambiguousMethodList[ix])));
                            }
                            ix++;
                        }
                    }
                    mrb_hash_set(M, result, mrb_str_new_cstr(M, smoke->methodNames[m]), meths);
                }
            }
        }
    }
    return result;
}

/*
	Flags values
		0					All methods, except enum values and protected non-static methods
		mf_static			Static methods only
		mf_enum				Enums only
		mf_protected		Protected non-static methods only
*/

#define PUSH_QTRUBY_METHOD		\
		if (	(methodRef.flags & (Smoke::mf_internal|Smoke::mf_ctor|Smoke::mf_dtor)) == 0 \
				&& strcmp(s->methodNames[methodRef.name], "operator=") != 0 \
				&& strcmp(s->methodNames[methodRef.name], "operator!=") != 0 \
				&& strcmp(s->methodNames[methodRef.name], "operator--") != 0 \
				&& strcmp(s->methodNames[methodRef.name], "operator++") != 0 \
				&& strncmp(s->methodNames[methodRef.name], "operator ", strlen("operator ")) != 0 \
				&& (	(flags == 0 && (methodRef.flags & (Smoke::mf_static|Smoke::mf_enum|Smoke::mf_protected)) == 0) \
						|| (	flags == Smoke::mf_static \
								&& (methodRef.flags & Smoke::mf_enum) == 0 \
								&& (methodRef.flags & Smoke::mf_static) == Smoke::mf_static ) \
						|| (flags == Smoke::mf_enum && (methodRef.flags & Smoke::mf_enum) == Smoke::mf_enum) \
						|| (	flags == Smoke::mf_protected \
								&& (methodRef.flags & Smoke::mf_static) == 0 \
								&& (methodRef.flags & Smoke::mf_protected) == Smoke::mf_protected ) ) ) { \
			if (strncmp(s->methodNames[methodRef.name], "operator", strlen("operator")) == 0) { \
				if (op_re.indexIn(s->methodNames[methodRef.name]) != -1) { \
					mrb_ary_push(M, result, mrb_str_new_cstr(M, (op_re.cap(1) + op_re.cap(2)).toLatin1())); \
				} else { \
					mrb_ary_push(M, result, mrb_str_new_cstr(M, s->methodNames[methodRef.name] + strlen("operator"))); \
				} \
			} else if (predicate_re.indexIn(s->methodNames[methodRef.name]) != -1 && methodRef.numArgs == 0) { \
				mrb_ary_push(M, result, mrb_str_new_cstr(M, (predicate_re.cap(2).toLower() + predicate_re.cap(3) + "?").toLatin1())); \
			} else if (set_re.indexIn(s->methodNames[methodRef.name]) != -1 && methodRef.numArgs == 1) { \
				mrb_ary_push(M, result, mrb_str_new_cstr(M, (set_re.cap(2).toLower() + set_re.cap(3) + "=").toLatin1())); \
			} else { \
				mrb_ary_push(M, result, mrb_str_new_cstr(M, s->methodNames[methodRef.name])); \
			} \
		}

mrb_value
findAllMethodNames(mrb_state* M, mrb_value /*self*/)
{
  mrb_value result, classid, flags_value;
  mrb_get_args(M, "ooo", &result, &classid, &flags_value);
	QRegExp predicate_re("^(is|has)(.)(.*)");
	QRegExp set_re("^(set)([A-Z])(.*)");
	QRegExp op_re("operator(.*)(([-%~/+|&*])|(>>)|(<<)|(&&)|(\\|\\|)|(\\*\\*))=$");

	unsigned short flags = (unsigned short) mrb_fixnum(flags_value);
	if (not mrb_nil_p(classid)) {
		Smoke::Index c = (Smoke::Index) mrb_fixnum(mrb_funcall(M, classid, "index", 0));
		Smoke* s = smokeList[mrb_fixnum(mrb_funcall(M, classid, "smoke", 0))];
		if (c > s->numClasses) {
			return mrb_nil_value();
		}
#ifdef DEBUG
		if (do_debug & qtdb_calls) qWarning("findAllMethodNames called with classid = %d in module %s", c, s->moduleName());
#endif
		Smoke::Index imax = s->numMethodMaps;
		Smoke::Index imin = 0, icur = -1, methmin, methmax;
		methmin = -1; methmax = -1; // kill warnings
		int icmp = -1;

		while (imax >= imin) {
			icur = (imin + imax) / 2;
			icmp = s->leg(s->methodMaps[icur].classId, c);
			if (icmp == 0) {
				Smoke::Index pos = icur;
				while(icur && s->methodMaps[icur-1].classId == c)
					icur --;
				methmin = icur;
				icur = pos;
				while(icur < imax && s->methodMaps[icur+1].classId == c)
					icur ++;
				methmax = icur;
				break;
			}
			if (icmp > 0)
				imax = icur - 1;
			else
				imin = icur + 1;
		}

        if (icmp == 0) {
 			for (Smoke::Index i=methmin ; i <= methmax ; i++) {
				Smoke::Index ix= s->methodMaps[i].method;
				if (ix > 0) {	// single match
					const Smoke::Method &methodRef = s->methods[ix];
					PUSH_QTRUBY_METHOD
				} else {		// multiple match
					ix = -ix;		// turn into ambiguousMethodList index
					while (s->ambiguousMethodList[ix]) {
						const Smoke::Method &methodRef = s->methods[s->ambiguousMethodList[ix]];
						PUSH_QTRUBY_METHOD
						ix++;
					}
				}
            }
        }
    }
    return result;
}

QByteArray *
find_cached_selector(mrb_state* M, int argc, mrb_value * argv, mrb_value klass, const char * methodName)
{
    // Look in the cache
static QByteArray * mcid = 0;
	if (mcid == 0) {
		mcid = new QByteArray();
	}

*mcid = mrb_obj_classname(M, klass);
	*mcid += ';';
	*mcid += methodName;
	for(int i=4; i<argc ; i++)
	{
		*mcid += ';';
*mcid += value_to_type_flag(M, argv[i]);
	}
	Smoke::ModuleIndex *rcid = methcache.value(*mcid);
#ifdef DEBUG
	if (do_debug & qtdb_calls) qWarning("method_missing mcid: %s", (const char *) *mcid);
#endif

	if (rcid) {
		// Got a hit
#ifdef DEBUG
		if (do_debug & qtdb_calls) qWarning("method_missing cache hit, mcid: %s", (const char *) *mcid);
#endif
		_current_method.smoke = rcid->smoke;
		_current_method.index = rcid->index;
	} else {
		_current_method.smoke = 0;
		_current_method.index = -1;
	}

	return mcid;
}

mrb_value
method_missing(mrb_state* M, int argc, mrb_value* argv, mrb_value self)
{
const char * methodName = mrb_sym2name(M, mrb_symbol(argv[0]));
mrb_value klass = mrb_funcall(M, self, "class", 0);

    mrb_value retval = mrb_nil_value();

	// Look for 'thing?' methods, and try to match isThing() or hasThing() in the Smoke runtime
static QByteArray * pred = 0;
	if (pred == 0) {
		pred = new QByteArray();
	}

	*pred = methodName;
	if (pred->endsWith("?")) {
		smokeruby_object *o = value_obj_info(M, self);
		if(!o || !o->ptr) {
      return mrb_nil_value(); // TODO: mrb_call_super(argc, argv);
		}

		// Drop the trailing '?'
		pred->replace(pred->length() - 1, 1, "");

		pred->replace(0, 1, pred->mid(0, 1).toUpper());
		pred->replace(0, 0, "is");
		Smoke::ModuleIndex meth = o->smoke->findMethod(o->smoke->classes[o->classId].className, (const char *) *pred);

		if (meth.index == 0) {
			pred->replace(0, 2, "has");
			meth = o->smoke->findMethod(o->smoke->classes[o->classId].className, *pred);
		}

		if (meth.index > 0) {
			methodName = (char *) (const char *) *pred;
		}
	}

    mrb_value * temp_stack = (mrb_value*)mrb_malloc(M, sizeof(mrb_value) * (argc+3));
    temp_stack[0] = mrb_str_new_cstr(M, "Qt");
    temp_stack[1] = mrb_str_new_cstr(M, methodName);
    temp_stack[2] = klass;
    temp_stack[3] = self;
    for (int count = 1; count < argc; count++) {
		temp_stack[count+3] = argv[count];
    }

	{
		QByteArray * mcid = find_cached_selector(M, argc+3, temp_stack, klass, methodName);

		if (_current_method.index == -1) {
			// Find the C++ method to call. Do that from Ruby for now

			retval = mrb_funcall_argv(M, mrb_obj_value(qt_internal_module(M)), mrb_intern_lit(M, "do_method_missing"), argc+3, temp_stack);
			if (_current_method.index == -1) {
				const char * op = mrb_sym2name(M, mrb_symbol(argv[0]));
				if (	qstrcmp(op, "-") == 0
						|| qstrcmp(op, "+") == 0
						|| qstrcmp(op, "/") == 0
						|| qstrcmp(op, "%") == 0
						|| qstrcmp(op, "|") == 0 )
				{
					// Look for operator methods of the form 'operator+=', 'operator-=' and so on..
					char op1[3];
					op1[0] = op[0];
					op1[1] = '=';
					op1[2] = '\0';
					temp_stack[1] = mrb_str_new_cstr(M, op1);
					retval = mrb_funcall_argv(M, mrb_obj_value(qt_internal_module(M)), mrb_intern_lit(M, "do_method_missing"), argc+3, temp_stack);
				}

				if (_current_method.index == -1) {
					// Check for property getter/setter calls, and for slots in QObject classes
					// not in the smoke library
					smokeruby_object *o = value_obj_info(M, self);
					if (	o != 0
							&& o->ptr != 0
							&& Smoke::isDerivedFrom(Smoke::ModuleIndex(o->smoke, o->classId), Smoke::findClass("QObject")) )
					{
						QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
static QByteArray * name = 0;
						if (name == 0) {
							name = new QByteArray();
						}

						*name = mrb_sym2name(M, mrb_symbol(argv[0]));
						const QMetaObject * meta = qobject->metaObject();

						if (argc == 1) {
							if (name->endsWith("?")) {
								name->replace(0, 1, pred->mid(0, 1).toUpper());
								name->replace(0, 0, "is");
								if (meta->indexOfProperty(*name) == -1) {
									name->replace(0, 2, "has");
								}
							}

							if (meta->indexOfProperty(*name) != -1) {
								mrb_value qvariant = mrb_funcall(M, self, "property", 1, mrb_str_new_cstr(M, *name));
								return mrb_funcall(M, qvariant, "value", 0);
							}
						}

						if (argc == 2 && name->endsWith("=")) {
							name->replace("=", "");
							if (meta->indexOfProperty(*name) != -1) {
								mrb_value qvariant = mrb_funcall(M, self, "qVariantFromValue", 1, argv[1]);
								return mrb_funcall(M, self, "setProperty", 2, mrb_str_new_cstr(M, *name), qvariant);
							}
						}

						int classId = o->smoke->idClass(meta->className()).index;

						// The class isn't in the Smoke lib. But if it is called 'local::Merged'
						// it is from a QDBusInterface and the slots are remote, so don't try to
						// those.
						while (	classId == 0
								&& qstrcmp(meta->className(), "local::Merged") != 0
								&& qstrcmp(meta->superClass()->className(), "QDBusAbstractInterface") != 0 )
						{
							// Assume the QObject has slots which aren't in the Smoke library, so try
							// and call the slot directly
							for (int id = meta->methodOffset(); id < meta->methodCount(); id++) {
								if (meta->method(id).methodType() == QMetaMethod::Slot) {
									QByteArray signature(meta->method(id).signature());
									QByteArray methodName = signature.mid(0, signature.indexOf('('));

									// Don't check that the types of the ruby args match the c++ ones for now,
									// only that the name and arg count is the same.
									if (*name == methodName && meta->method(id).parameterTypes().count() == (argc - 1)) {
										QList<MocArgument*> args = get_moc_arguments(M,	o->smoke, meta->method(id).typeName(),
																						meta->method(id).parameterTypes() );
										mrb_value result = mrb_nil_value();
										QtRuby::InvokeNativeSlot slot(M, qobject, id, argc - 1, args, argv + 1, &result);
										slot.next();
										return result;
									}
								}
							}
							meta = meta->superClass();
							classId = o->smoke->idClass(meta->className()).index;
						}
					}

					return mrb_nil_value(); // TODO: mrb_call_super(argc, argv);
				}
			}
			// Success. Cache result.
			methcache.insert(*mcid, new Smoke::ModuleIndex(_current_method));
		}
	}
  QtRuby::MethodCall c(M, _current_method.smoke, _current_method.index, self, temp_stack+4, argc-1);
    c.next();
    mrb_value result = *(c.var());
    return result;
}

mrb_value
method_missing(mrb_state* M, mrb_value self)
{
  mrb_value* argv; int argc;
  mrb_get_args(M, "*", &argv, &argc);

  return method_missing(M, argc, argv, self);
}

mrb_value
class_method_missing(mrb_state* M, mrb_value klass)
{
  mrb_value* argv; int argc;
  mrb_get_args(M, "*", &argv, &argc);
	mrb_value result = mrb_nil_value();
	mrb_value retval = mrb_nil_value();
	const char * methodName = mrb_sym2name(M, mrb_symbol(argv[0]));
	mrb_value * temp_stack = (mrb_value*)mrb_malloc(M, sizeof(mrb_value) * (argc+3));
  temp_stack[0] = mrb_str_new_cstr(M, "Qt");
  temp_stack[1] = mrb_str_new_cstr(M, methodName);
    temp_stack[2] = klass;
    temp_stack[3] = mrb_nil_value();
    for (int count = 1; count < argc; count++) {
		temp_stack[count+3] = argv[count];
    }

    {
      QByteArray * mcid = find_cached_selector(M, argc+3, temp_stack, klass, methodName);

		if (_current_method.index == -1) {
			retval = mrb_funcall_argv(M, mrb_obj_value(qt_internal_module(M)), mrb_intern_lit(M, "do_method_missing"), argc+3, temp_stack);
			if (_current_method.index != -1) {
				// Success. Cache result.
				methcache.insert(*mcid, new Smoke::ModuleIndex(_current_method));
			}
		}
	}

    if (_current_method.index == -1) {
static QRegExp * rx = 0;
		if (rx == 0) {
			rx = new QRegExp("[a-zA-Z]+");
		}

		if (rx->indexIn(methodName) == -1) {
			// If an operator method hasn't been found as an instance method,
			// then look for a class method - after 'op(self,a)' try 'self.op(a)'
      mrb_value * method_stack = (mrb_value*) mrb_malloc(M, sizeof(mrb_value) * (argc - 1));
	    	method_stack[0] = argv[0];
	    	for (int count = 1; count < argc - 1; count++) {
				method_stack[count] = argv[count+1];
    		}
        result = method_missing(M, argc-1, method_stack, argv[1]);
			return result;
		} else {
			return mrb_nil_value(); // TODO: mrb_call_super(argc, argv);
		}
    }
    QtRuby::MethodCall c(M, _current_method.smoke, _current_method.index, mrb_nil_value(), temp_stack+4, argc-1);
    c.next();
    result = *(c.var());
    return result;
}

QList<MocArgument*>
get_moc_arguments(mrb_state* M, Smoke* smoke, const char * typeName, QList<QByteArray> methodTypes)
{
static QRegExp * rx = 0;
	if (rx == 0) {
		rx = new QRegExp("^(bool|int|uint|long|ulong|double|char\\*|QString)&?$");
	}
	methodTypes.prepend(QByteArray(typeName));
	QList<MocArgument*> result;

	foreach (QByteArray name, methodTypes) {
		MocArgument *arg = new MocArgument;
		Smoke::Index typeId = 0;

		if (name.isEmpty()) {
			arg->argType = xmoc_void;
			result.append(arg);
		} else {
			name.replace("const ", "");
			QString staticType = (rx->indexIn(name) != -1 ? rx->cap(1) : "ptr");
			if (staticType == "ptr") {
				arg->argType = xmoc_ptr;
				QByteArray targetType = name;
				typeId = smoke->idType(targetType.constData());
				if (typeId == 0 && !name.contains('*')) {
					if (!name.contains("&")) {
						targetType += "&";
					}
					typeId = smoke->idType(targetType.constData());
				}

				// This shouldn't be necessary because the type of the slot arg should always be in the
				// smoke module of the slot being invoked. However, that isn't true for a dataUpdated()
				// slot in a PlasmaScripting::Applet
				if (typeId == 0) {
					QHash<Smoke*, QtRubyModule>::const_iterator it;
					for (it = qtruby_modules.constBegin(); it != qtruby_modules.constEnd(); ++it) {
						smoke = it.key();
						targetType = name;
						typeId = smoke->idType(targetType.constData());
						if (typeId != 0) {
							break;
						}

						if (typeId == 0 && !name.contains('*')) {
							if (!name.contains("&")) {
								targetType += "&";
							}

							typeId = smoke->idType(targetType.constData());

							if (typeId != 0) {
								break;
							}
						}
					}
				}
			} else if (staticType == "bool") {
				arg->argType = xmoc_bool;
				smoke = qtcore_Smoke;
				typeId = smoke->idType(name.constData());
			} else if (staticType == "int") {
				arg->argType = xmoc_int;
				smoke = qtcore_Smoke;
				typeId = smoke->idType(name.constData());
			} else if (staticType == "uint") {
				arg->argType = xmoc_uint;
				smoke = qtcore_Smoke;
				typeId = smoke->idType("unsigned int");
			} else if (staticType == "long") {
				arg->argType = xmoc_long;
				smoke = qtcore_Smoke;
				typeId = smoke->idType(name.constData());
			} else if (staticType == "ulong") {
				arg->argType = xmoc_ulong;
				smoke = qtcore_Smoke;
				typeId = smoke->idType("unsigned long");
			} else if (staticType == "double") {
				arg->argType = xmoc_double;
				smoke = qtcore_Smoke;
				typeId = smoke->idType(name.constData());
			} else if (staticType == "char*") {
				arg->argType = xmoc_charstar;
				smoke = qtcore_Smoke;
				typeId = smoke->idType(name.constData());
			} else if (staticType == "QString") {
				arg->argType = xmoc_QString;
				name += "*";
				smoke = qtcore_Smoke;
				typeId = smoke->idType(name.constData());
			}

			if (typeId == 0) {
				mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as slot argument\n", mrb_str_new_cstr(M, name.constData()));
				return result;
			}

			arg->st.set(smoke, typeId);
			result.append(arg);
		}
	}

	return result;
}

// ----------------   Helpers -------------------

//---------- All functions except fully qualified statics & enums ---------

mrb_value
mapObject(mrb_state* M, mrb_value self)
{
  mrb_value obj;
  mrb_get_args(M, "o", &obj);
  return mapObject(M, self, obj);
}

mrb_value
mapObject(mrb_state* M, mrb_value self, mrb_value obj)
{
  smokeruby_object *o = value_obj_info(M, obj);
    if(!o)
      return mrb_nil_value();
    mapPointer(M, obj, o, o->classId, 0);
    return self;
}

Q_DECL_EXPORT mrb_value
qobject_metaobject(mrb_state* M, mrb_value self)
{
	smokeruby_object * o = value_obj_info(M, self);
	QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
	QMetaObject * meta = (QMetaObject *) qobject->metaObject();
	mrb_value obj = getPointerObject(M, meta);
	if (not mrb_nil_p(obj)) {
		return obj;
	}

	smokeruby_object  * m = alloc_smokeruby_object(	M, false,
													o->smoke,
													o->smoke->idClass("QMetaObject").index,
													meta );

	obj = set_obj_info(M, "Qt::MetaObject", m);
	return obj;
}

mrb_value
set_obj_info(mrb_state* M, const char * className, smokeruby_object * o)
{
  mrb_value klass = mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "find_class",
                                1, mrb_str_new_cstr(M, className) );
  if (mrb_nil_p(klass)) {
		mrb_raisef(M, mrb_class_get(M, "RuntimeError"), "Class '%S' not found", mrb_str_new_cstr(M, className));
	}

	Smoke::ModuleIndex *r = classcache.value(className);
	if (r != 0) {
		o->classId = (int) r->index;
	}
	// If the instance is a subclass of QObject, then check to see if the
	// className from its QMetaObject is in the Smoke library. If not then
	// create a Ruby class for it dynamically. Remove the first letter from
	// any class names beginning with 'Q' or 'K' and put them under the Qt::
	// or KDE:: modules respectively.
	if (o->smoke->isDerivedFrom(o->smoke, o->classId, o->smoke->idClass("QObject").smoke, o->smoke->idClass("QObject").index)) {
		QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
		const QMetaObject * meta = qobject->metaObject();
		int classId = o->smoke->idClass(meta->className()).index;
		// The class isn't in the Smoke lib..
		if (classId == 0) {
			RClass* new_klass = NULL;
			QByteArray className(meta->className());

			if (className == "QTableModel") {
				new_klass = qtablemodel_class(M);
			} else if (className == "QListModel") {
				new_klass = qlistmodel_class(M);
			} else if (className.startsWith("Q")) {
				className.replace("Q", "");
				className = className.mid(0, 1).toUpper() + className.mid(1);
        new_klass = mrb_define_class_under(M, qt_module(M), className, mrb_class_ptr(klass));
			} else {
        new_klass = mrb_define_class(M, className, mrb_class_ptr(klass));
			}
      MRB_SET_INSTANCE_TT(new_klass, MRB_TT_DATA);

			if (new_klass) {
				klass = mrb_obj_value(new_klass);

				for (int id = meta->enumeratorOffset(); id < meta->enumeratorCount(); id++) {
					// If there are any enum keys with the same scope as the new class then
					// add them
					if (qstrcmp(meta->className(), meta->enumerator(id).scope()) == 0) {
						for (int i = 0; i < meta->enumerator(id).keyCount(); i++) {
							mrb_define_const(M, mrb_class_ptr(klass),
                               meta->enumerator(id).key(i),
                               mrb_fixnum_value(meta->enumerator(id).value(i)) );
						}
					}
				}
			}

			// Add a Qt::Object.metaObject method which will do dynamic despatch on the
			// metaObject() virtual method so that the true QMetaObject of the class
			// is returned, rather than for the one for the parent class that is in
			// the Smoke library.
			mrb_define_method(M, mrb_class_ptr(klass), "metaObject", &qobject_metaobject, MRB_ARGS_ANY());
		}
	}

  return mrb_obj_value(Data_Wrap_Struct(M, mrb_class_ptr(klass), &smokeruby_type, (void *) o));
}

mrb_value
kross2smoke(mrb_state* M, mrb_value /*self*/)
{
  mrb_value krobject, new_klass;
  mrb_get_args(M, "oo", &krobject, &new_klass);
  mrb_value new_klassname = mrb_funcall(M, new_klass, "name", 0);

  Smoke::ModuleIndex * cast_to_id = classcache.value(mrb_string_value_ptr(M, new_klassname));
  if (cast_to_id == 0) {
    mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "unable to find class \"%S\" to cast to\n", mrb_string_value_ptr(M, new_klassname));
  }

  void* o;
  Data_Get_Struct(M, krobject, &smokeruby_type, o);

  smokeruby_object * o_cast = alloc_smokeruby_object(M, false, cast_to_id->smoke, (int) cast_to_id->index, o);

  mrb_value obj = mrb_obj_value(Data_Wrap_Struct(M, mrb_class_ptr(new_klass), &smokeruby_type, o_cast));
  mapPointer(M, obj, o_cast, o_cast->classId, 0);
  return obj;
}

const char *
value_to_type_flag(mrb_state* M, mrb_value ruby_value)
{
	const char * classname = mrb_obj_classname(M, ruby_value);
	const char *r = "";
	if (mrb_nil_p(ruby_value))
		r = "u";
	else if (mrb_type(ruby_value) == MRB_TT_FIXNUM || qstrcmp(classname, "Qt::Integer") == 0)
		r = "i";
	else if (mrb_type(ruby_value) == MRB_TT_FLOAT)
		r = "n";
	else if (mrb_type(ruby_value) == MRB_TT_STRING)
		r = "s";
	else if(mrb_type(ruby_value) == MRB_TT_TRUE || mrb_type(ruby_value) == MRB_TT_FALSE || qstrcmp(classname, "Qt::Boolean") == 0)
		r = "B";
	else if (qstrcmp(classname, "Qt::Enum") == 0) {
		mrb_value temp = mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "get_qenum_type", 1, ruby_value);
		r = mrb_string_value_ptr(M, temp);
	} else if (mrb_type(ruby_value) == MRB_TT_DATA) {
		smokeruby_object *o = value_obj_info(M, ruby_value);
		if (o == 0 || o->smoke == 0) {
			r = "a";
		} else {
			r = o->smoke->classes[o->classId].className;
		}
	} else {
		r = "U";
	}

    return r;
}

mrb_value prettyPrintMethod(mrb_state* M, Smoke::Index id)
{
  mrb_value r = mrb_str_new_cstr(M, "");
    const Smoke::Method &meth = qtcore_Smoke->methods[id];
    const char *tname = qtcore_Smoke->types[meth.ret].name;
    if(meth.flags & Smoke::mf_static) rb_str_catf(M, r, "static ");
    rb_str_catf(M, r, "%s ", (tname ? tname:"void"));
    rb_str_catf(M, r, "%s::%s(", qtcore_Smoke->classes[meth.classId].className, qtcore_Smoke->methodNames[meth.name]);
    for(int i = 0; i < meth.numArgs; i++) {
      if(i) rb_str_catf(M, r, ", ");
	tname = qtcore_Smoke->types[qtcore_Smoke->argumentList[meth.args+i]].name;
	rb_str_catf(M, r, "%s", (tname ? tname:"void"));
    }
    rb_str_catf(M, r, ")");
    if(meth.flags & Smoke::mf_const) rb_str_catf(M, r, " const");
    return r;
}