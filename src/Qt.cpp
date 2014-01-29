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

#include <cstdio>
#include <cstdarg>

#include <regex>
#include <string>

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

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/variable.h>

#include <smoke.h>
#include <smoke/qtcore_smoke.h>

#include "marshall.h"
#include "qtruby.h"
#include "smokeruby.h"
#include "smoke.h"
#include "marshall_types.h"

bool application_terminated = false;

QList<Smoke*> smokeList;
QHash<Smoke*, QtRubyModule> qtruby_modules;

int do_debug = -1;

typedef QHash<void *, SmokeValue> PointerMap;
static QMutex pointer_map_mutex;
Q_GLOBAL_STATIC(PointerMap, pointer_map)
int object_count = 0;

QHash<QByteArray, Smoke::ModuleIndex> methcache;
QHash<QByteArray, Smoke::ModuleIndex> classcache;

QHash<Smoke::ModuleIndex, QByteArray*> IdToClassNameMap;

mrb_int
get_mrb_int(mrb_state* M, mrb_value const& v) {
	if (mrb_nil_p(v)) {
		return 0;
	} else if (mrb_type(v) == MRB_TT_OBJECT) {
		// Both Qt::Enum and Qt::Integer have a value() method
    mrb_value const ret = mrb_iv_get(M, v, mrb_intern_lit(M, "@value"));
    assert(mrb_fixnum_p(ret));
    return mrb_fixnum(ret);
	}
  else if(mrb_fixnum_p(v)) { return mrb_fixnum(v); }
  else if(mrb_float_p(v)) { return mrb_float(v); }
  mrb_raisef(M, mrb_class_get(M, "TypeError"), "cannot convert %S to integer", v);
  return 0;
}

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

mrb_data_type const smokeruby_type = { "smokeruby_object", &smokeruby_free };

smokeruby_object *value_obj_info(mrb_state* M, mrb_value ruby_value) {  // ptr on success, null on fail
	if (mrb_type(ruby_value) != MRB_TT_DATA) {
		return 0;
	}

  if(DATA_TYPE(ruby_value) != &smokeruby_type) { return NULL; }

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
    SmokeValue const& ret = pointer_map()->operator[](ptr);
		if (do_debug & qtdb_gc and M->gc_state == GC_STATE_NONE) {
      PROTECT_SCOPE();
			qWarning("getPointerObject %p -> %s", ptr, mrb_string_value_ptr(M, ret.value));
		}
    pointer_map_mutex.unlock();
		return ret;
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

  assert(mrb_type(obj) == MRB_TT_DATA);
  assert(DATA_TYPE(obj) == &smokeruby_type);

	ptr = smoke->cast(ptr, fromClassId, toClassId);

  if (ptr != lastptr) {
		lastptr = ptr;

		if (do_debug & qtdb_gc) {
			const char *className = smoke->classes[fromClassId].className;
			qWarning("mapPointer (%s*)%p -> %s size: %d", className, ptr, mrb_string_value_ptr(M, obj), pointer_map()->size() + 1);
		}

    pointer_map()->insert(ptr, SmokeValue(obj, o));
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
  PROTECT_SCOPE();

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
		if (meth.flags & Smoke::mf_const) { signature += " const"; }
		qWarning("module: %s virtual %p->%s::%s called",
             smoke->moduleName(), ptr,
             smoke->classes[smoke->methods[method].classId].className,
             signature.constData());
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

  if (not mrb_respond_to(M, obj, mrb_intern_cstr(M, methodName))) {
    return false;
  }

  std::vector<mrb_value> sp(smoke->methods[method].numArgs, mrb_nil_value());
	QtRuby::VirtualMethodCall c(M, ModuleIndex(smoke, method), args, obj, sp.data());
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
		mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as signal reply-type", mrb_intern_cstr(M, type().name()));
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
  char p[CAT_BUFFER_SIZE];
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
			if (o->smoke != 0 and o->classId != 0) {
        return qtruby_modules[o->smoke].binding->className(o->classId);
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

const char *
value_to_type_flag(mrb_state* M, mrb_value const& ruby_value)
{
  RClass* const Qt = mrb_class_get(M, "Qt");
  RClass* const cls = mrb_class(M, ruby_value);
	const char *r = NULL;

	if (mrb_nil_p(ruby_value))
		r = "u";
	else if (mrb_fixnum_p(ruby_value) || cls == mrb_class_get_under(M, Qt, "Integer"))
		r = "i";
	else if (mrb_float_p(ruby_value))
		r = "n";
	else if (mrb_string_p(ruby_value))
		r = "s";
	else if(mrb_type(ruby_value) == MRB_TT_TRUE || mrb_type(ruby_value) == MRB_TT_FALSE || cls == mrb_class_get_under(M, Qt, "Boolean"))
		r = "B";
	else if (cls == mrb_class_get_under(M, Qt, "Enum")) {
		mrb_value temp = mrb_iv_get(M, ruby_value, mrb_intern_lit(M, "@type"));
    assert(mrb_symbol_p(temp));
    size_t len;
		r = mrb_sym2name_len(M, mrb_symbol(temp), &len);
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

  assert(r);
  return r;
}

Smoke::ModuleIndex
find_cached_selector(mrb_state* M, int argc, mrb_value * argv, RClass* klass, const char * methodName, QByteArray& mcid)
{
  mrb_value const class_name = mrb_mod_cv_get(M, klass, mrb_intern_lit(M, "QtClassName"));
  assert(mrb_symbol_p(class_name));
  size_t len;
  mcid = mrb_sym2name_len(M, mrb_symbol(class_name), &len);
	mcid += ';';
	mcid += methodName;
	for(int i=0; i<argc ; i++) {
		mcid += ';';
    mcid += value_to_type_flag(M, argv[i]);
	}

	Smoke::ModuleIndex const& rcid = methcache.value(mcid);
	if (do_debug & qtdb_calls) qWarning("method_missing mcid: %s", mcid.constData());
	if (do_debug & qtdb_calls && rcid != Smoke::NullModuleIndex) { // Got a hit
		qWarning("method_missing cache hit, mcid: %s", mcid.constData());
	}
  return rcid;
}


static bool
isEnum(char const* enumName)
{
  for (auto const& s : smokeList) {
    Smoke::Index const typeId = s->idType(enumName);
    if (typeId <= 0) { continue; }
    switch(s->types[typeId].flags & Smoke::tf_elem) {
      case Smoke::t_enum: case Smoke::t_ulong:
      case Smoke::t_long: case Smoke::t_uint:
      case Smoke::t_int: return true;
    }
  }
  return false;
}

static int
checkarg(char const* argtype, char const* type_name) {
  typedef std::regex r;

  static r const is_const_reg("^const\\s+"), remove_qualifier("^(?:const )?([^*&]+)[&*]?$");
  int const const_point = std::regex_search(type_name, is_const_reg)? -1 : 0;

  if(std::strlen(argtype) == 1) {
    switch(argtype[0]) {
      case 'i': {
        if(std::regex_search(type_name, r("^(?:int|signed int|signed|qint32)&?$")))
        { return 6 + const_point; }
        if(std::regex_search(type_name, r("^quint32&?$"))
           or std::regex_search(type_name, r("^(?:short|ushort|unsigned short int|unsigned short|uchar|char|unsigned char|uint|long|ulong|unsigned long int|unsigned|float|double|WId|HBITMAP__\\*|HDC__\\*|HFONT__\\*|HICON__\\*|HINSTANCE__\\*|HPALETTE__\\*|HRGN__\\*|HWND__\\*|Q_PID|^quint16&?$|^qint16&?$)$"))
           or std::regex_search(type_name, r("^(quint|qint|qulong|qlong|qreal)")))
        { return 4 + const_point; }

        if(isEnum(std::regex_replace(type_name, remove_qualifier, "$1").c_str()))
        { return 2; }
      } break;

      case 'n': {
        if(std::regex_search(type_name, r("^(?:double|qreal)$")))
        { return 6 + const_point; }
        if(std::regex_search(type_name, r("^float$")))
        { return 4 + const_point; }
        if(std::regex_search(type_name, r("^double$|^qreal$"))
           or std::regex_search(type_name, r("^(?:short|ushort|uint|long|ulong|signed|unsigned|float|double)$")))
        { return 2 + const_point; }

        if(isEnum(std::regex_replace(type_name, remove_qualifier, "$1").c_str()))
        { return 2 + const_point; }
      } break;

      case 'B': {
        if(std::regex_search(type_name, r("^(?:bool)[*&]?$")))
        { return 2 + const_point; }
      } break;

      case 's': {
        if(std::regex_search(type_name, r("^(?:(?:const )?(QString)[*&]?)$")))
        { return 8 + const_point; }
        if(std::regex_search(type_name, r("^(const )?((QChar)[*&]?)$")))
        { return 6 + const_point; }
        if(std::regex_search(type_name, r("^(?:(u(nsigned )?)?char\\*)$")))
        { return 4 + const_point; }
        if(std::regex_search(type_name, r("^(?:const (u(nsigned )?)?char\\*)$")))
        { return 2 + const_point; }
      } break;

      case 'a': {
        if(std::regex_search(type_name, r("^(?:const QCOORD\\*|(?:const )?(?:QStringList[\\*&]?|QValueList<int>[\\*&]?|QRgb\\*|char\\*\\*))$)")))
        { return 2 + const_point; }
      } break;

      case 'u': {
        if(std::regex_search(type_name, r("^(?:u?char\\*|const u?char\\*|(?:const )?((Q(C?)String))[*&]?)$")))
        { return 4 + const_point; }
        if(std::regex_search(type_name, r("^(?:short|ushort|uint|long|ulong|signed|unsigned|int)$")))
        { return -99; }
        else
        { return 2 + const_point; }
      } break;

      case 'U': {
        return std::regex_search(type_name, r("QStringList"))? 4 + const_point : 2 + const_point;
      } break;
    }
  }

  std::string const t = std::regex_replace(std::regex_replace(type_name, remove_qualifier, "$1"), r("(::)?Ptr$"), "");
  if(argtype == t) { return 4 + const_point; }
  if(Smoke::isDerivedFrom(argtype, t.c_str()))
  { return 2 + const_point; }
  if(isEnum(argtype) and (std::regex_search(t, r("int|qint32|uint|quint32|long|ulong")) or isEnum(t.c_str())))
  { return 2 + const_point; }

  return -99;
}

Smoke::ModuleIndex
do_method_missing(mrb_state* M, char const* pkg, std::string method, RClass* cls, int argc, mrb_value const* argv) {
  mrb_value cpp_name_value = cls == mrb_class_get(M, "Qt")
                             ? mrb_symbol_value(mrb_intern_lit(M, "Qt"))
                             : mrb_mod_cv_get(M, cls, mrb_intern_lit(M, "QtClassName"));
  if(mrb_nil_p(cpp_name_value)) { return Smoke::NullModuleIndex; }
  assert(mrb_symbol_p(cpp_name_value));
  size_t len;
  char const* cpp_name = mrb_sym2name_len(M, mrb_symbol(cpp_name_value), &len);

  // Modify constructor method name from new to the name of the Qt class
  // and remove any namespacing
  if(method == "new") {
    method = std::regex_replace(cpp_name, std::regex("^.*::"), "");
  }

  // If the method contains no letters it must be an operator, append "operator" to the
  // method name
  if(not std::regex_search(method, std::regex("\\w"))) {
    method = "operator" + method;
  }

  // Change foobar= to setFoobar()
  if(method != "operator=" and std::regex_match(method, std::regex(".*[^-+%\\/|=]=$"))) {
    method = std::string("set").append(1, std::toupper(method[0])).append(method.begin() + 1, method.end() - 1);
  }

  // Build list of munged method names which is the methodname followed
  // by symbols that indicate the basic type of the method's arguments
  //
  // Plain scalar = $
  // Object = #
  // Non-scalar (reference to array or hash, undef) = ?
  std::vector<std::string> methods = { method };
  for(mrb_value const* arg = argv; arg < (argv + argc); ++arg) {
    if(mrb_nil_p(*arg)) {
      std::vector<std::string> temp;
      for(std::string& meth : methods) {
        temp.push_back(meth + '?');
        temp.push_back(meth + '#');
        meth += '$';
      }
      methods.insert(methods.end(), temp.begin(), temp.end());
    } else {
      char const appending_char = value_to_ptr(M, *arg)? '#':
                                  mrb_array_p(*arg) or mrb_hash_p(*arg)? '?':
                                  '$';
      for(auto& meth : methods) { meth += appending_char; }
    }
  }

  // Create list of methodIds that match classname and munged method name
  std::vector<Smoke::ModuleIndex> methodIds;
  for(auto const& meth_name : methods) {
    Smoke::ModuleIndex classId = Smoke::findClass(cpp_name);
    Smoke::ModuleIndex meth = Smoke::NullModuleIndex;
    QList<Smoke::ModuleIndex> milist;
    if (classId != Smoke::NullModuleIndex) { meth = classId.smoke->findMethod(cpp_name, meth_name.c_str()); }

    if (do_debug & qtdb_calls && meth != Smoke::NullModuleIndex) qWarning("Found method %s::%s => %d", cpp_name, meth_name.c_str(), meth.index);

    if(meth == Smoke::NullModuleIndex) {
      // since every smoke module defines a class 'QGlobalSpace' we can't rely on the classMap,
      // so we search for methods by hand
      foreach (Smoke* s, smokeList) {
        Smoke::ModuleIndex cid = s->idClass("QGlobalSpace");
        Smoke::ModuleIndex mnid = s->idMethodName(meth_name.c_str());
        if (!cid.index || !mnid.index) continue;
        meth = s->idMethod(cid.index, mnid.index);
        if (meth != Smoke::NullModuleIndex) milist.append(meth);
      }

      if (do_debug & qtdb_calls && meth != Smoke::NullModuleIndex) qWarning("Found method QGlobalSpace::%s => %d", meth_name.c_str(), meth.index);
    } else { milist.append(meth); }

    for (auto const& meth : milist) {
      assert(meth != Smoke::NullModuleIndex);

      Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
      assert(i != 0); // shouldn't happen
      if(i > 0) {	// single match
        const Smoke::Method &methodRef = meth.smoke->methods[i];
        if ((methodRef.flags & Smoke::mf_internal) == 0) {
          methodIds.push_back(Smoke::ModuleIndex(meth.smoke, i));
        }
      } else {		// multiple match
        if (do_debug & qtdb_calls) qWarning("Ambiguous Method %s::%s", cpp_name, meth_name.c_str());
        for (i = -i; meth.smoke->ambiguousMethodList[i]; ++i) { // turn into ambiguousMethodList index
          const Smoke::Method &methodRef = meth.smoke->methods[meth.smoke->ambiguousMethodList[i]];
          if ((methodRef.flags & Smoke::mf_internal) == 0) {
            methodIds.push_back(Smoke::ModuleIndex(meth.smoke, meth.smoke->ambiguousMethodList[i]));

            if (do_debug & qtdb_calls) qWarning("Ambiguous Method %s::%s => %d", cpp_name, meth_name.c_str(), meth.smoke->ambiguousMethodList[i]);
          }
        }
      }
    }
  }

  // If we didn't find any methods and the method name contains an underscore
  // then convert to camelcase and try again
  if(methodIds.empty() and std::regex_search(method, std::regex("._."))) {
    // If the method name contains underscores, convert to camel case
    // form and try again
    std::string new_method;
    new_method.reserve(method.size());
    for(auto i = method.begin(); i < method.end(); ++i) {
      new_method += (*i == '_' and ++i != method.end())? std::toupper(*i) : *i;
    }
    if(do_debug & qtdb_calls) {
      qWarning("falling back snake case to camel case: %s -> %s", method.c_str(), new_method.c_str());
    }
    return do_method_missing(M, pkg, new_method, cls, argc, argv);
  }

  // Find the best match
  Smoke::ModuleIndex chosen;
  int best_match = -1;
  for(auto const& id : methodIds) {
    if(do_debug & qtdb_calls) { qWarning("matching => smoke: %s(%p) index: %d",
                                         id.smoke->moduleName(), id.smoke, id.index); }

    int current_match = id.smoke->methods[id.index].flags & Smoke::mf_const ? 1 : 0;
    for(mrb_value const* arg = argv; arg < (argv + argc); ++arg) {
      Smoke* smoke = id.smoke;
      char const* t = smoke->types[smoke->argumentList[smoke->methods[id.index].args + arg - argv]].name;
      char const* argtype = value_to_type_flag(M, *arg);
      int score = checkarg(argtype, t);
      current_match += score;
      if(do_debug & qtdb_calls) { qWarning("        %s (%s) score: %d", t, argtype, score); }
    }

    // Note that if current_match > best_match, then chosen must be nil
    if(current_match > best_match) {
      best_match = current_match;
      chosen = id;
      // Ties are bad - but it is better to chose something than to fail
    } else if(current_match == best_match && id.smoke == chosen.smoke) {
      if(do_debug & qtdb_calls) { qWarning(" ****** warning: multiple methods with the same score of %d: %d and %d", current_match, chosen.index, id.index); }
      chosen = id;
    }
    if(do_debug & qtdb_calls) { qWarning("        match => smoke: %p index: %d score: %d chosen: %d", id.smoke, id.index, current_match, chosen.index); }
  }

  // Select the chosen method
  return chosen;
}

mrb_value
method_missing(mrb_state* M, mrb_value self)
{
  mrb_sym sym; int argc; mrb_value* argv;
  mrb_get_args(M, "n*", &sym, &argv, &argc);
  size_t methodName_len;
  const char * methodName = mrb_sym2name_len(M, sym, &methodName_len);
  RClass* klass = mrb_class(M, self);

	// Look for 'thing?' methods, and try to match isThing() or hasThing() in the Smoke runtime
  QByteArray pred = methodName;
	pred = methodName;
	if (pred.endsWith("?")) {
		smokeruby_object *o = value_obj_info(M, self);
		if(!o || !o->ptr) { return mrb_call_super(M, self); }

		// Drop the trailing '?', add 'is' prefix
		pred.replace(pred.length() - 1, 1, "").replace(0, 1, pred.mid(0, 1).toUpper()).replace(0, 0, "is");
		Smoke::ModuleIndex meth = o->smoke->findMethod(o->smoke->classes[o->classId].className, pred.constData());

		if (meth == Smoke::NullModuleIndex) {
      // check 'has' prefix instead of 'is'
			meth = o->smoke->findMethod(o->smoke->classes[o->classId].className,
                                  pred.replace(0, 2, "has").constData());
		}

		if (meth != Smoke::NullModuleIndex) { methodName = pred.constData(); }
	}

  QByteArray mcid;
  Smoke::ModuleIndex _current_method = find_cached_selector(M, argc, argv, klass, methodName, mcid);

  if(_current_method != Smoke::NullModuleIndex) {
    QtRuby::MethodCall c(M, _current_method, self, argv, argc);
    return c.next(), *(c.var());
  }

#define return_if_found(meth)                                           \
  do { if(meth != Smoke::NullModuleIndex) {                             \
      methcache[mcid] = meth; /* Success. Cache result. */              \
      QtRuby::MethodCall c(M, meth, self, argv, argc);                  \
      return c.next(), *(c.var());                                      \
    } } while(false)                                                    \

  _current_method = do_method_missing(M, "Qt", methodName, mrb_class(M, self), argc, argv);
  return_if_found(_current_method);

  size_t op_len;
  const char * op = mrb_sym2name_len(M, sym, &op_len);
  if (op_len == 1 && (*op == '-' or *op == '+' or *op == '/' or *op == '%' or *op == '|')) {
    // Look for operator methods of the form 'operator+=', 'operator-=' and so on..
    char const op1[] = { op[0], '=', '\0' };
    _current_method = do_method_missing(M, "Qt", op1, mrb_class(M, self), argc, argv);
    return_if_found(_current_method);
  }

#undef return_if_found

  // Check for property getter/setter calls, and for slots in QObject classes
  // not in the smoke library
  smokeruby_object *o = value_obj_info(M, self);
  if (	o == 0 or o->ptr == 0
        or not Smoke::isDerivedFrom(Smoke::ModuleIndex(o->smoke, o->classId), Smoke::findClass("QObject")) )
  { return mrb_call_super(M, self); }

  QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
  QByteArray name(op, op_len);
  const QMetaObject * meta = qobject->metaObject();

  // check property getter
  if (argc == 0) {
    if (name.endsWith("?")) {
      name.replace(0, 1, pred.mid(0, 1).toUpper()).replace(0, 0, "is");
      if (meta->indexOfProperty(name) == -1) { name.replace(0, 2, "has"); }
    }

    if (meta->indexOfProperty(name) != -1) {
      return mrb_funcall(M, mrb_funcall(
          M, self, "property", 1, mrb_symbol_value(mrb_intern(M, name.constData(), name.size()))), "value", 0);
    }
  }

  // check property setter
  if (argc == 1 && name.endsWith("=") and meta->indexOfProperty(name.replace("=", "")) != -1) {
    mrb_value qvariant = mrb_funcall(M, self, "qVariantFromValue", 1, argv[0]);
    return mrb_funcall(M, self, "setProperty", 2, mrb_symbol_value(mrb_intern(M, name.constData(), name.size())), qvariant);
  }

  // check slot
  Smoke::ModuleIndex classId = o->smoke->idClass(meta->className());
  while (	classId == Smoke::NullModuleIndex
          // The class isn't in the Smoke lib. But if it is called 'local::Merged'
          // it is from a QDBusInterface and the slots are remote, so don't try to
          // those.
          && qstrcmp(meta->className(), "local::Merged") != 0
          && qstrcmp(meta->superClass()->className(), "QDBusAbstractInterface") != 0 )
  {
    // Assume the QObject has slots which aren't in the Smoke library, so try
    // and call the slot directly
    for (int id = meta->methodOffset(); id < meta->methodCount(); id++) {
      if (meta->method(id).methodType() != QMetaMethod::Slot) { continue; }

      QByteArray signature = meta->method(id).signature();
      QByteArray methodName = signature.mid(0, signature.indexOf('('));

      // Don't check that the types of the ruby args match the c++ ones for now,
      // only that the name and arg count is the same.
      if (name == methodName && meta->method(id).parameterTypes().count() == (argc - 1)) {
        QList<MocArgument*> args = get_moc_arguments(M,	o->smoke, meta->method(id).typeName(),
                                                     meta->method(id).parameterTypes() );
        mrb_value result = mrb_nil_value();
        return QtRuby::InvokeNativeSlot(M, qobject, id, argc, args, argv, &result).next(), result;
      }
    }
    meta = meta->superClass();
    classId = o->smoke->idClass(meta->className());
  }

  return mrb_call_super(M, self);
}

mrb_value
class_method_missing(mrb_state* M, mrb_value self)
{
  mrb_sym sym; mrb_value* argv; int argc;
  mrb_get_args(M, "n*", &sym, &argv, &argc);
  size_t methodName_len;
	const char * methodName = mrb_sym2name_len(M, sym, &methodName_len);

  RClass* klass = mrb_type(self) == MRB_TT_MODULE or mrb_type(self) == MRB_TT_CLASS
                  ? mrb_class_ptr(self) : mrb_class(M, self);

  QByteArray mcid;
  Smoke::ModuleIndex _current_method = find_cached_selector(M, argc, argv, klass, methodName, mcid);

  if (_current_method == Smoke::NullModuleIndex) {
    _current_method = do_method_missing(M, "Qt", methodName, klass, argc, argv);
    // Success. Cache result.
    if (_current_method != Smoke::NullModuleIndex) { methcache[mcid] = _current_method; }
  }

  if (_current_method == Smoke::NullModuleIndex) {
    static QRegExp const rx("[a-zA-Z]+");
    // If an operator method hasn't been found as an instance method,
    // then look for a class method - after 'op(self,a)' try 'self.op(a)'
    return rx.indexIn(methodName) == -1? method_missing(M, argv[1]) : mrb_call_super(M, self);
  }
  QtRuby::MethodCall c(M, _current_method, mrb_nil_value(), argv, argc);
  return c.next(), *(c.var());
}

QList<MocArgument*>
get_moc_arguments(mrb_state* M, Smoke* smoke, const char * typeName, QList<QByteArray> methodTypes)
{
  static QRegExp const rx("^(bool|int|uint|long|ulong|double|char\\*|QString)&?$");
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
			QString staticType = (rx.indexIn(name) != -1 ? rx.cap(1) : "ptr");
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
				mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Cannot handle '%S' as slot argument\n", mrb_intern_cstr(M, name.constData()));
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
  mrb_value const className_sym = mrb_symbol_value(mrb_intern_cstr(M, className));
  mrb_value klass = mrb_hash_get(M, mrb_mod_cv_get(
      M, qt_internal_module(M), mrb_intern_lit(M, "Classes")), className_sym);
  if (mrb_nil_p(klass)) {
		mrb_raisef(M, mrb_class_get(M, "RuntimeError"), "Class '%S' not found", className_sym);
	}

	Smoke::ModuleIndex const& r = classcache.value(className);
	if (r != Smoke::NullModuleIndex) {
		o->classId = (int) r.index;
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

  assert(mrb_type(klass) == MRB_TT_CLASS);
  return mrb_obj_value(Data_Wrap_Struct(M, mrb_class_ptr(klass), &smokeruby_type, o));
}
