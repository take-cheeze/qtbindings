/***************************************************************************
                          qtscript.cpp  -  QtScript ruby extension
                             -------------------
    begin                : 11-07-2008
    copyright            : (C) 2008 by Richard Dale
    email                : richard.j.dale@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QtDebug>

#include <smoke/qtscript_smoke.h>

#include <qtruby.h>

#include <mruby.h>
#include <mruby/array.h>

#include <iostream>

static mrb_value getClassList(mrb_state* M, mrb_value /*self*/)
{
  mrb_value classList = mrb_ary_new(M);
    for (int i = 1; i <= qtscript_Smoke->numClasses; i++) {
        if (qtscript_Smoke->classes[i].className && !qtscript_Smoke->classes[i].external)
          mrb_ary_push(M, classList, mrb_str_new_cstr(M, qtscript_Smoke->classes[i].className));
    }
    return classList;
}

const char*
resolve_classname_qtscript(smokeruby_object * o)
{
    return qtruby_modules[o->smoke].binding->className(o->classId);
}

extern TypeHandler QtScript_handlers[];

extern "C" {

static QtRuby::Binding binding;

Q_DECL_EXPORT void
Init_qtscript(mrb_state* M)
{
    init_qtscript_Smoke();

    binding = QtRuby::Binding(M, qtscript_Smoke);

    smokeList << qtscript_Smoke;

    QtRubyModule module = { "QtScript", resolve_classname_qtscript, 0, &binding };
    qtruby_modules[qtscript_Smoke] = module;

    install_handlers(QtScript_handlers);

    RClass* qtscript_module = mrb_define_module(M, "QtScript");
    RClass* qtscript_internal_module = mrb_define_module_under(M, qtscript_module, "Internal");

    mrb_define_module_function(M, qtscript_internal_module, "getClassList", getClassList, MRB_ARGS_NONE());
}

}
