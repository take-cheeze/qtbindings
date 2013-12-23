#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QtDebug>

#include <smoke/qtdeclarative_smoke.h>

#include <qtruby.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>

#include <iostream>

static mrb_value getClassList(mrb_state* M, mrb_value /*self*/)
{
    mrb_value classList = mrb_ary_new(M);
    for (int i = 1; i <= qtdeclarative_Smoke->numClasses; i++) {
        if (qtdeclarative_Smoke->classes[i].className && !qtdeclarative_Smoke->classes[i].external) {
          mrb_ary_push(M, classList, mrb_str_new_cstr(M, qtdeclarative_Smoke->classes[i].className));
        }
    }
    return classList;
}

const char*
resolve_classname_qtdeclarative(smokeruby_object * o)
{
    return qtruby_modules[o->smoke].binding->className(o->classId);
}

extern TypeHandler QtDeclarative_handlers[];

extern "C" {

static QtRuby::Binding binding;

Q_DECL_EXPORT void
Init_qtdeclarative(mrb_state* M)
{
    init_qtdeclarative_Smoke();

    binding = QtRuby::Binding(M, qtdeclarative_Smoke);

    smokeList << qtdeclarative_Smoke;

    QtRubyModule module = { "QtDeclarative", resolve_classname_qtdeclarative, 0, &binding };
    qtruby_modules[qtdeclarative_Smoke] = module;

    install_handlers(QtDeclarative_handlers);

    RClass* qtdeclarative_module = mrb_define_module(M, "QtDeclarative");
    RClass* qtdeclarative_internal_module = mrb_define_module_under(M, qtdeclarative_module, "Internal");

    mrb_define_module_function(M, qtdeclarative_internal_module, "getClassList", getClassList, MRB_ARGS_NONE());
}

}
