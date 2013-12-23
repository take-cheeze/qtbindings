/***************************************************************************
                          qtuitoolshandlers.cpp  -  QtUiTools specific marshallers
                             -------------------
    begin                : Sat Jun 28 2008
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

#include <smoke/qtuitools_smoke.h>

#include <qtruby.h>

#include <mruby.h>
#include <mruby/array.h>

#include <iostream>

static mrb_value getClassList(mrb_state* M, mrb_value /*self*/)
{
    mrb_value classList = mrb_ary_new(M);
    for (int i = 1; i <= qtuitools_Smoke->numClasses; i++) {
        if (qtuitools_Smoke->classes[i].className && !qtuitools_Smoke->classes[i].external)
          mrb_ary_push(M, classList, mrb_str_new_cstr(M, qtuitools_Smoke->classes[i].className));
    }
    return classList;
}

const char*
resolve_classname_qtuitools(smokeruby_object * o)
{
    return qtruby_modules[o->smoke].binding->className(o->classId);
}

extern TypeHandler QtUiTools_handlers[];

extern "C" {

static QtRuby::Binding binding;

Q_DECL_EXPORT void
Init_qtuitools(mrb_state* M)
{
    init_qtuitools_Smoke();

    binding = QtRuby::Binding(M, qtuitools_Smoke);

    smokeList << qtuitools_Smoke;

    QtRubyModule module = { "QtUiTools", resolve_classname_qtuitools, 0, &binding };
    qtruby_modules[qtuitools_Smoke] = module;

    install_handlers(QtUiTools_handlers);

    RClass* qtuitools_module = mrb_define_module(M, "QtUiTools");
    RClass* qtuitools_internal_module = mrb_define_module_under(M, qtuitools_module, "Internal");

    mrb_define_module_function(M, qtuitools_internal_module, "getClassList", getClassList, MRB_ARGS_NONE());
}

}
