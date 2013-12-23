/***************************************************************************
                          qtruby.h  -  description
                             -------------------
    begin                : Fri Jul 4 2003
    copyright            : (C) 2003 by Richard Dale
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

#ifndef QTRUBY_H
#define QTRUBY_H

#include <QtCore/QHash>
#include <smoke.h>

#include "marshall.h"

#include <mruby.h>
#include <mruby/data.h>

struct ScopeProtector {
  ScopeProtector(mrb_state* M) : M(M), arena_idx(mrb_gc_arena_save(M)) {}
  ~ScopeProtector() { mrb_gc_arena_restore(M, arena_idx); }
  mrb_state* const M;
  int const arena_idx;
};

#define PROTECT_SCOPE() ScopeProtector scope_protector_ ## __LINE__(M); (void)scope_protector_ ## __LINE__

extern mrb_data_type const smokeruby_type;

#ifndef QT_VERSION_STR
#define QT_VERSION_STR "Unknown"
#endif
#define QTRUBY_VERSION "2.0.5"

inline uint qHash(const Smoke::ModuleIndex& mi) {
	return qHash(mi.index) ^ qHash(mi.smoke);
}

struct MocArgument;

namespace QtRuby {

class Q_DECL_EXPORT Binding : public SmokeBinding {
public:
	Binding();
	Binding(mrb_state* M, Smoke *s);
	void deleted(Smoke::Index classId, void *ptr);
	bool callMethod(Smoke::Index method, void *ptr, Smoke::Stack args, bool /*isAbstract*/);
	char *className(Smoke::Index classId);

  mrb_state* M;
};

}

struct smokeruby_object {
    void *ptr;
    bool allocated;
    Smoke *smoke;
    int classId;
};

struct SmokeValue
{
  mrb_value value;
  smokeruby_object* o;
  
  SmokeValue()
      : value(mrb_nil_value())
  , o(0) { }
  
  SmokeValue(mrb_value value, smokeruby_object* o)
  : value(value)
  , o(o) { }
};

struct TypeHandler {
    const char *name;
    Marshall::HandlerFn fn;
};

extern Q_DECL_EXPORT int do_debug;   // evil
extern Q_DECL_EXPORT mrb_value rv_qapp;
extern Q_DECL_EXPORT int object_count;

typedef const char* (*ResolveClassNameFn)(smokeruby_object * o);
typedef void (*ClassCreatedFn)(const char* package, mrb_value module, mrb_value klass);

struct QtRubyModule {
    const char *name;
    ResolveClassNameFn resolve_classname;
    ClassCreatedFn class_created;
    QtRuby::Binding *binding;
};

// keep this enum in sync with lib/Qt/qtruby4.rb

enum QtDebugChannel {
    qtdb_none = 0x00,
    qtdb_ambiguous = 0x01,
    qtdb_method_missing = 0x02,
    qtdb_calls = 0x04,
    qtdb_gc = 0x08,
    qtdb_virtual = 0x10,
    qtdb_verbose = 0x20
};

extern "C" {
#define qt_module(M) mrb_class_get(M, "Qt")
#define qt_internal_module(M) mrb_class_get_under(M, qt_module(M), "Internal")

#define qlistmodel_class(M) mrb_class_get_under(M, qt_module(M), "ListModel")
#define qmetaobject_class(M) mrb_class_get_under(M, qt_module(M), "MetaObject")
#define qtablemodel_class(M) mrb_class_get_under(M, qt_module(M), "TableModel")
#define qt_base_class(M) mrb_class_get_under(M, qt_module(M), "Base")
#define qvariant_class(M) mrb_class_get_under(M, qt_module(M), "Variant")
#define moduleindex_class(M) mrb_class_get_under(M, qt_internal_module(M), "ModuleIndex")

  mrb_value mrb_call_super(mrb_state* M, mrb_value self, int argc, mrb_value* argv);

extern Q_DECL_EXPORT bool application_terminated;
extern Q_DECL_EXPORT void set_qtruby_embedded(bool yn);
}


extern Q_DECL_EXPORT Smoke::ModuleIndex _current_method;

extern Q_DECL_EXPORT QHash<Smoke*, QtRubyModule> qtruby_modules;
extern Q_DECL_EXPORT QList<Smoke*> smokeList;

extern Q_DECL_EXPORT QHash<QByteArray, Smoke::ModuleIndex *> methcache;
extern Q_DECL_EXPORT QHash<QByteArray, Smoke::ModuleIndex *> classcache;
// Maps from an int id to classname in the form Qt::Widget
extern Q_DECL_EXPORT QHash<Smoke::ModuleIndex, QByteArray*> IdToClassNameMap;

extern Q_DECL_EXPORT void install_handlers(TypeHandler *);

extern Q_DECL_EXPORT void smokeruby_mark(void * ptr);
extern Q_DECL_EXPORT void smokeruby_free(void * ptr);
extern Q_DECL_EXPORT mrb_value qchar_to_s(mrb_state* M, mrb_value self);

extern Q_DECL_EXPORT smokeruby_object * alloc_smokeruby_object(mrb_state* M, bool allocated, Smoke * smoke, int classId, void * ptr);
extern Q_DECL_EXPORT void free_smokeruby_object(mrb_state* M, smokeruby_object * o);
extern Q_DECL_EXPORT smokeruby_object *value_obj_info(mrb_state* M, mrb_value value);
extern Q_DECL_EXPORT void *value_to_ptr(mrb_state* M, mrb_value ruby_value); // ptr on success, null on fail

extern Q_DECL_EXPORT mrb_value getPointerObject(mrb_state* M, void *ptr);
extern Q_DECL_EXPORT SmokeValue getSmokeValue(mrb_state* M, void *ptr);
extern Q_DECL_EXPORT void mapPointer(mrb_state* M, mrb_value obj, smokeruby_object *o, Smoke::Index classId, void *lastptr);
extern Q_DECL_EXPORT void unmapPointer(smokeruby_object *, Smoke::Index, void*);

extern Q_DECL_EXPORT const char * resolve_classname(smokeruby_object * o);
extern Q_DECL_EXPORT mrb_value rb_str_catf(mrb_state* M, mrb_value self, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

extern Q_DECL_EXPORT mrb_value findMethod(mrb_state* M, mrb_value self);
extern Q_DECL_EXPORT mrb_value findAllMethods(mrb_state* M, mrb_value self);
extern Q_DECL_EXPORT mrb_value findAllMethodNames(mrb_state* M, mrb_value self);

extern Q_DECL_EXPORT QByteArray* find_cached_selector(mrb_state* M, int argc, mrb_value * argv, mrb_value klass, const char * methodName);
extern Q_DECL_EXPORT mrb_value method_missing(mrb_state* M, mrb_value self);
extern Q_DECL_EXPORT mrb_value class_method_missing(mrb_state* M, mrb_value klass);
extern Q_DECL_EXPORT QList<MocArgument*> get_moc_arguments(mrb_state* M, Smoke* smoke, const char * typeName, QList<QByteArray> methodTypes);

extern Q_DECL_EXPORT void * construct_copy(smokeruby_object *o);

extern Q_DECL_EXPORT mrb_value mapObject(mrb_state* M, mrb_value self);
extern Q_DECL_EXPORT mrb_value mapObject(mrb_state* M, mrb_value self, mrb_value);
extern Q_DECL_EXPORT mrb_value qobject_metaobject(mrb_state* M, mrb_value self);
extern Q_DECL_EXPORT mrb_value set_obj_info(mrb_state* M, const char * className, smokeruby_object * o);
extern Q_DECL_EXPORT mrb_value kross2smoke(mrb_state* M, mrb_value self);
extern Q_DECL_EXPORT const char* value_to_type_flag(mrb_state* M, mrb_value ruby_value);
extern Q_DECL_EXPORT mrb_value prettyPrintMethod(mrb_state* M, Smoke::Index id);

#endif