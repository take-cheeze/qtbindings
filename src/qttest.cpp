/***************************************************************************
                          qttest.cpp  -  QtTest ruby extension
                             -------------------
    begin                : 29-10-2008
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

#include <smoke/qttest_smoke.h>

#include <qtruby.h>

#include <mruby.h>
#include <mruby/array.h>

#include <iostream>

static mrb_value getClassList(mrb_state* M, mrb_value /*self*/)
{
  mrb_value classList = mrb_ary_new(M);
    for (int i = 1; i <= qttest_Smoke->numClasses; i++) {
        if (qttest_Smoke->classes[i].className && !qttest_Smoke->classes[i].external)
          mrb_ary_push(M, classList, mrb_str_new_cstr(M, qttest_Smoke->classes[i].className));
    }
    return classList;
}

const char*
resolve_classname_qttest(smokeruby_object * o)
{
    return qtruby_modules[o->smoke].binding->className(o->classId);
}

extern TypeHandler QtTest_handlers[];

extern "C" {

static QtRuby::Binding binding;

Q_DECL_EXPORT void
Init_qttest(mrb_state* M)
{
    init_qttest_Smoke();

    binding = QtRuby::Binding(M, qttest_Smoke);

    smokeList << qttest_Smoke;

    QtRubyModule module = { "QtTest", resolve_classname_qttest, 0, &binding };
    qtruby_modules[qttest_Smoke] = module;

    install_handlers(QtTest_handlers);

    RClass* qttest_module = mrb_define_module(M, "QtTest");
    RClass* qttest_internal_module = mrb_define_module_under(M, qttest_module, "Internal");

    mrb_define_module_function(M, qttest_internal_module, "getClassList", getClassList, MRB_ARGS_NONE());
}

}
