#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QtDebug>

#include <smoke/qtwebkit_smoke.h>

#include <qtruby.h>

#include <mruby.h>
#include <mruby/array.h>

#include <iostream>

static mrb_value getClassList(mrb_state* M, mrb_value /*self*/)
{
    mrb_value classList = mrb_ary_new(M);
    for (int i = 1; i <= qtwebkit_Smoke->numClasses; i++) {
        if (qtwebkit_Smoke->classes[i].className && !qtwebkit_Smoke->classes[i].external) {
          mrb_ary_push(M, classList, mrb_str_new_cstr(M, qtwebkit_Smoke->classes[i].className));
        }
    }
    return classList;
}

const char*
resolve_classname_qtwebkit(smokeruby_object * o)
{
    return qtruby_modules[o->smoke].binding->className(o->classId);
}

extern TypeHandler QtWebKit_handlers[];

extern "C" {

static QtRuby::Binding binding;

Q_DECL_EXPORT void
Init_qtwebkit(mrb_state* M)
{
    init_qtwebkit_Smoke();

    binding = QtRuby::Binding(M, qtwebkit_Smoke);

    smokeList << qtwebkit_Smoke;

    QtRubyModule module = { "QtWebKit", resolve_classname_qtwebkit, 0, &binding };
    qtruby_modules[qtwebkit_Smoke] = module;

    install_handlers(QtWebKit_handlers);

    RClass* qtwebkit_module = mrb_define_module(M, "QtWebKit");
    RClass* qtwebkit_internal_module = mrb_define_module_under(M, qtwebkit_module, "Internal");

    mrb_define_module_function(M, qtwebkit_internal_module, "getClassList", getClassList, MRB_ARGS_NONE());
}

}
