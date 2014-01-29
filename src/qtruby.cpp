/***************************************************************************
                          qtruby.cpp  -  description
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

#include <QtCore/qabstractitemmodel.h>
#include <QtCore/qglobal.h>
#include <QtCore/qhash.h>
#include <QtCore/qline.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/private/qobject_p.h>
#include <QtCore/private/qmetaobject_p.h>
#include <QtCore/qobject.h>
#include <QtCore/qrect.h>
#include <QtCore/qregexp.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
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

#include <smoke.h>

#include <smoke/qtcore_smoke.h>
#include <smoke/qtgui_smoke.h>
#include <smoke/qtxml_smoke.h>
#include <smoke/qtsql_smoke.h>
#include <smoke/qtopengl_smoke.h>
#include <smoke/qtnetwork_smoke.h>
#include <smoke/qtsvg_smoke.h>
#include <smoke/qtdeclarative_smoke.h>
#include <smoke/qtscript_smoke.h>
#include <smoke/qttest_smoke.h>
#include <smoke/qtuitools_smoke.h>
#include <smoke/qtwebkit_smoke.h>

#ifdef QT_QTDBUS
#include <smoke/qtdbus_smoke.h>
#endif

#ifdef QT_QSCINTILLA2
#include <smoke/qsci_smoke.h>
#endif

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/proc.h>
#include <mruby/hash.h>

#include <array>
#include <regex>
#include <iostream>
#include <unordered_map>

#include "marshall_types.h"
#include "qtruby.h"

mrb_int
get_mrb_int(mrb_state* M, mrb_value const& v);

extern "C" mrb_value
mrb_yield_internal(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv, mrb_value self, struct RClass *c);

mrb_value mrb_call_super(mrb_state* M, mrb_value self)
{
  RClass* sup = M->c->ci->target_class->super;
  RProc* p = mrb_method_search_vm(M, &sup, M->c->ci->mid);

  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

  if(!p) {
    p = mrb_method_search_vm(M, &sup, mrb_intern_lit(M, "method_missing"));
    assert(p);
    std::vector<mrb_value> args(argc + 1);
    args[0] = mrb_symbol_value(M->c->ci->mid);
    std::copy_n(argv, argc, args.begin() + 1);
    return mrb_yield_internal(M, mrb_obj_value(p), args.size(), args.data(), self, sup);
  }

  return mrb_yield_internal(M, mrb_obj_value(p), argc, argv, self, sup);
}

static mrb_value
module_name(mrb_state* M, mrb_value self)
{
  switch(mrb_type(self)) {
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
      return mrb_class_path(M, mrb_class_ptr(self));
    default:
      return mrb_class_path(M, mrb_class(M, self));
  }
}

typedef Smoke::ModuleIndex ModuleIndex;

static void
getAllParents(ModuleIndex const& id, std::vector<ModuleIndex>& result)
{
  assert(id != Smoke::NullModuleIndex);
  Smoke* const s = id.smoke;
  for(Smoke::Index* p = s->inheritanceList + s->classes[id.index].parents; *p; ++p) {
    result.emplace_back(s, *p);

    getAllParents(ModuleIndex(s, *p), result);
  }
}

static auto const remove_operators = [](mrb_state* M, mrb_value const& v) {
  assert(mrb_symbol_p(v));
  size_t len;
  char const* s = mrb_sym2name_len(M, mrb_symbol(v), &len);
  if(len == 1 or len == 2) {
    switch(s[0] | (len == 2? s[1] << 8 : 0)) {
      // These methods are all defined in Qt::Base, even if they aren't supported by a particular
      // subclass, so remove them to avoid confusion
      case '%': case '&': case '*': case '+': case '-': case '/':
      case '<': case '>': case '|': case '~': case '^':
#define t(f, s) (f | (s << 8))
      case t('*', '*'): case t('-', '@'): case t('<', '<'):
      case t('<', '='): case t('>', '='): case t('>', '>'):
#undef t
        return false;
    }
  }
  return true;
};

/*
	Flags values
		0					All methods, except enum values and protected non-static methods
		mf_static			Static methods only
		mf_enum				Enums only
		mf_protected		Protected non-static methods only
*/

#define PUSH_QTRUBY_METHOD                                              \
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
					mrb_ary_push(M, result, mrb_symbol_value(mrb_intern_cstr(M, (op_re.cap(1) + op_re.cap(2)).toLatin1()))); \
				} else { \
					mrb_ary_push(M, result, mrb_symbol_value(mrb_intern_cstr(M, s->methodNames[methodRef.name] + strlen("operator")))); \
				} \
			} else if (predicate_re.indexIn(s->methodNames[methodRef.name]) != -1 && methodRef.numArgs == 0) { \
				mrb_ary_push(M, result, mrb_symbol_value(mrb_intern_cstr(M, (predicate_re.cap(2).toLower() + predicate_re.cap(3) + "?").toLatin1()))); \
			} else if (set_re.indexIn(s->methodNames[methodRef.name]) != -1 && methodRef.numArgs == 1) { \
				mrb_ary_push(M, result, mrb_symbol_value(mrb_intern_cstr(M, (set_re.cap(2).toLower() + set_re.cap(3) + "=").toLatin1()))); \
			} else { \
				mrb_ary_push(M, result, mrb_symbol_value(mrb_intern_cstr(M, s->methodNames[methodRef.name]))); \
			}                                                                 \
		}

              void
                  findAllMethodNames(mrb_state* M, mrb_value result, ModuleIndex const& classid, int flags)
{
	static QRegExp const
      predicate_re("^(is|has)(.)(.*)"), set_re("^(set)([A-Z])(.*)"),
      op_re("operator(.*)(([-%~/+|&*])|(>>)|(<<)|(&&)|(\\|\\|)|(\\*\\*))=$");

  if(classid == Smoke::NullModuleIndex) { return; }

  Smoke::Index c = classid.index;
  Smoke* const s = classid.smoke;
  if (classid.index > s->numClasses) { return; }
  if (do_debug & qtdb_calls) qWarning("findAllMethodNames called with classid = %d in module %s", c, s->moduleName());
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

static mrb_value
module_qt_methods(mrb_state* M, mrb_value self)
{
  mrb_value meths;
  mrb_int flags;
  mrb_bool inc_super = 1;
  mrb_get_args(M, "Ai|b", &meths, &flags, &inc_super);

  if(mrb_type(self) == MRB_TT_CLASS) { return meths; }
  assert(mrb_type(self) = MRB_TT_MODULE);

  RClass* klass = mrb_class_ptr(self);
  ModuleIndex classid = find_pclassid(mrb_class_name(M, klass));
  for(; classid == Smoke::NullModuleIndex;
      classid = find_pclassid(mrb_class_name(M, klass))) {
    klass = klass->super;
    if(not klass) { return meths; }
  }

  std::vector<ModuleIndex> ids;
  if(inc_super) { getAllParents(classid, ids); }
  ids.push_back(classid);

  for(auto const& i : ids) { findAllMethodNames(M, meths, i, flags); }

  mrb_value* begin = RARRAY_PTR(meths);
  mrb_value* end = RARRAY_PTR(meths) + RARRAY_LEN(meths);
  meths = mrb_ary_new_from_values(M, std::remove_if(begin, end, std::bind(remove_operators, M, std::placeholders::_1)) - begin, begin);
  begin = RARRAY_PTR(meths), end = RARRAY_PTR(meths) + RARRAY_LEN(meths);
  std::sort(begin, end, [](mrb_value const& lhs, mrb_value const& rhs)
            { return mrb_symbol(lhs) < mrb_symbol(rhs); });
  meths = mrb_ary_new_from_values(M, std::unique(begin, end, [](mrb_value const& lhs, mrb_value const& rhs)
  { return mrb_symbol(lhs) == mrb_symbol(rhs); }) - begin, begin);
  return meths;
}

static mrb_value
qt_base_qt_methods(mrb_state* M, mrb_value self)
{
  mrb_value meths; mrb_int flags;
  mrb_get_args(M, "Ai", &meths, &flags);

  std::vector<ModuleIndex> ids;
  smokeruby_object* const obj = value_obj_info(M, self);
  ModuleIndex const classid(obj->smoke, obj->classId);
  getAllParents(classid, ids);
  ids.push_back(classid);

  for(auto const& i : ids) { findAllMethodNames(M, meths, i, flags); }

  mrb_value* begin = RARRAY_PTR(meths);
  mrb_value* end = RARRAY_PTR(meths) + RARRAY_LEN(meths);
  meths = mrb_ary_new_from_values(
      M, std::remove_if(begin, end, std::bind(remove_operators, M, std::placeholders::_1)) - begin, begin);
  begin = RARRAY_PTR(meths), end = RARRAY_PTR(meths) + RARRAY_LEN(meths);
  std::sort(begin, end, [](mrb_value const& lhs, mrb_value const& rhs)
            { return mrb_symbol(lhs) < mrb_symbol(rhs); });
  meths = mrb_ary_new_from_values(M, std::unique(begin, end, [](mrb_value const& lhs, mrb_value const& rhs)
  { return mrb_symbol(lhs) == mrb_symbol(rhs); }) - begin, begin);
  return meths;
}

static mrb_value
qt_base_ancestors(mrb_state* M, mrb_value self)
{
  RClass* klass = mrb_class_ptr(self);
  ModuleIndex classid = find_pclassid(mrb_class_name(M, klass));
  for(; classid == Smoke::NullModuleIndex;
      classid = find_pclassid(mrb_class_name(M, klass))) {
    klass = klass->super;
    if(not klass) { return mrb_call_super(M, self); }
  }

  assert(mrb_type(self) == MRB_TT_CLASS);

  mrb_value const Classes = mrb_mod_cv_get(M, qt_internal_module(M), mrb_intern_lit(M, "Classes"));

  mrb_value klasses = mrb_call_super(M, self);
  std::vector<ModuleIndex> ids;
  getAllParents(classid, ids);
  for(auto const& i : ids) {
    mrb_ary_push(M, klasses, mrb_hash_get(
        M, Classes, mrb_symbol_value(mrb_intern_cstr(M, i.smoke->classes[i.index].className))));
  }
  mrb_ary_push(M, klasses, self);

  mrb_value* begin = RARRAY_PTR(klasses);
  mrb_value* end = RARRAY_PTR(klasses) + RARRAY_LEN(klasses);
  klasses = mrb_ary_new_from_values(M, std::remove_if(begin, end, [M](mrb_value const& v)
  { return mrb_class_ptr(v) == qt_base_class(M); }) - begin, begin);
  std::sort(begin, end, [](mrb_value const& lhs, mrb_value const& rhs)
            { return mrb_class_ptr(lhs) < mrb_class_ptr(rhs); });
  klasses = mrb_ary_new_from_values(M, std::unique(begin, end, [](mrb_value const& lhs, mrb_value const& rhs)
  { return mrb_symbol(lhs) == mrb_symbol(rhs); }) - begin, begin);
  return klasses;
}

extern bool qRegisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);
extern bool qUnregisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);

extern TypeHandler Qt_handlers[];
extern const char * resolve_classname_qt(smokeruby_object * o);

static mrb_value
qdebug(mrb_state* M, mrb_value klass)
{
  mrb_value msg;
  mrb_get_args(M, "o", &msg);
	qDebug("%s", mrb_string_value_ptr(M, msg));
	return klass;
}

static mrb_value
qfatal(mrb_state* M, mrb_value klass)
{
  mrb_value msg;
  mrb_get_args(M, "o", &msg);
	qFatal("%s", mrb_string_value_ptr(M, msg));
	return klass;
}

static mrb_value
qwarning(mrb_state* M, mrb_value klass)
{
  mrb_value msg;
  mrb_get_args(M, "o", &msg);
	qWarning("%s", mrb_string_value_ptr(M, msg));
	return klass;
}

//---------- Ruby methods (for all functions except fully qualified statics & enums) ---------


// Takes a variable name and a QProperty with QVariant value, and returns a '
// variable=value' pair with the value in ruby inspect style
static QString
inspectProperty(QMetaProperty property, const char * name, QVariant & value)
{
	if (property.isEnumType()) {
		QMetaEnum e = property.enumerator();
		return QString(" %1=%2::%3").arg(name).arg(e.scope()).arg(e.valueToKey(value.toInt()));
	}

	switch (value.type()) {
	case QVariant::String: {
		if (value.toString().isNull()) {
			return QString(" %1=nil").arg(name);
		} else {
			return QString(" %1=%2").arg(name).arg(value.toString());
		}
	}

	case QVariant::Bool: {
		QString rubyName;
		QRegExp name_re("^(is|has)(.)(.*)");

		if (name_re.indexIn(name) != -1) {
			rubyName = name_re.cap(2).toLower() + name_re.cap(3) + "?";
		} else {
			rubyName = name;
		}

		return QString(" %1=%2").arg(rubyName).arg(value.toString());
	}

	case QVariant::Color:
		return QString(" %1=#<Qt::Color:0x0 %2>").arg(name).arg(value.value<QColor>().name());

	case QVariant::Cursor:
		return QString(" %1=#<Qt::Cursor:0x0 shape=%2>").arg(name).arg(value.value<QCursor>().shape());

	case QVariant::Double:
		return QString(" %1=%2").arg(name).arg(value.toDouble());

	case QVariant::Font: {
		QFont f = value.value<QFont>();
		return QString(	" %1=#<Qt::Font:0x0 family=%2, pointSize=%3, weight=%4, italic=%5, bold=%6, underline=%7, strikeOut=%8>")
									.arg(name)
									.arg(f.family())
									.arg(f.pointSize())
									.arg(f.weight())
									.arg(f.italic() ? "true" : "false")
									.arg(f.bold() ? "true" : "false")
									.arg(f.underline() ? "true" : "false")
									.arg(f.strikeOut() ? "true" : "false");
	}

	case QVariant::Line: {
		QLine l = value.toLine();
		return QString(" %1=#<Qt::Line:0x0 x1=%2, y1=%3, x2=%4, y2=%5>")
						.arg(name)
						.arg(l.x1())
						.arg(l.y1())
						.arg(l.x2())
						.arg(l.y2());
	}

	case QVariant::LineF: {
		QLineF l = value.toLineF();
		return QString(" %1=#<Qt::LineF:0x0 x1=%2, y1=%3, x2=%4, y2=%5>")
						.arg(name)
						.arg(l.x1())
						.arg(l.y1())
						.arg(l.x2())
						.arg(l.y2());
	}

	case QVariant::Point: {
		QPoint p = value.toPoint();
		return QString(" %1=#<Qt::Point:0x0 x=%2, y=%3>").arg(name).arg(p.x()).arg(p.y());
	}

	case QVariant::PointF: {
		QPointF p = value.toPointF();
		return QString(" %1=#<Qt::PointF:0x0 x=%2, y=%3>").arg(name).arg(p.x()).arg(p.y());
	}

	case QVariant::Rect: {
		QRect r = value.toRect();
		return QString(" %1=#<Qt::Rect:0x0 left=%2, right=%3, top=%4, bottom=%5>")
									.arg(name)
									.arg(r.left()).arg(r.right()).arg(r.top()).arg(r.bottom());
	}

	case QVariant::RectF: {
		QRectF r = value.toRectF();
		return QString(" %1=#<Qt::RectF:0x0 left=%2, right=%3, top=%4, bottom=%5>")
									.arg(name)
									.arg(r.left()).arg(r.right()).arg(r.top()).arg(r.bottom());
	}

	case QVariant::Size: {
		QSize s = value.toSize();
		return QString(" %1=#<Qt::Size:0x0 width=%2, height=%3>")
									.arg(name)
									.arg(s.width()).arg(s.height());
	}

	case QVariant::SizeF: {
		QSizeF s = value.toSizeF();
		return QString(" %1=#<Qt::SizeF:0x0 width=%2, height=%3>")
									.arg(name)
									.arg(s.width()).arg(s.height());
	}

	case QVariant::SizePolicy: {
		QSizePolicy s = value.value<QSizePolicy>();
		return QString(" %1=#<Qt::SizePolicy:0x0 horizontalPolicy=%2, verticalPolicy=%3>")
									.arg(name)
									.arg(s.horizontalPolicy())
									.arg(s.verticalPolicy());
	}

	case QVariant::Brush:
//	case QVariant::ColorGroup:
	case QVariant::Image:
	case QVariant::Palette:
	case QVariant::Pixmap:
	case QVariant::Region:
		return QString(" %1=#<Qt::%2:0x0>").arg(name).arg(value.typeName() + 1);

	default:
		return QString(" %1=%2").arg(name)
									.arg((value.isNull() || value.toString().isNull()) ? "nil" : value.toString() );
	}
}

// Retrieves the properties for a QObject and returns them as 'name=value' pairs
// in a ruby inspect string. For example:
//
//		#<Qt::HBoxLayout:0x30139030 name=unnamed, margin=0, spacing=0, resizeMode=3>
//
static mrb_value
inspect_qobject(mrb_state* M, mrb_value self)
{
	if (mrb_type(self) != MRB_TT_DATA) {
		return mrb_nil_value();
	}

	// Start with #<Qt::HBoxLayout:0x30139030> from the original inspect() call
	// Drop the closing '>'
	mrb_value inspect_str = mrb_str_to_str(M, mrb_call_super(M, self));
	mrb_str_resize(M, inspect_str, RSTRING_LEN(inspect_str) - 1);

	smokeruby_object * o = 0;
  Data_Get_Struct(M, self, &smokeruby_type, o);
	QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);

	QString value_list;
	value_list.append(QString(" objectName=\"%1\"").arg(qobject->objectName()));

	if (qobject->isWidgetType()) {
		QWidget * w = (QWidget *) qobject;
		value_list.append(QString(", x=%1, y=%2, width=%3, height=%4")
												.arg(w->x())
												.arg(w->y())
												.arg(w->width())
												.arg(w->height()) );
	}

	value_list.append(">");
	mrb_str_cat_cstr(M, inspect_str, value_list.toLatin1());

	return inspect_str;
}

// Retrieves the properties for a QObject and pretty_prints them as 'name=value' pairs
// For example:
//
//		#<Qt::HBoxLayout:0x30139030
//		 name=unnamed,
//		 margin=0,
//		 spacing=0,
//		 resizeMode=3>
//
static mrb_value
pretty_print_qobject(mrb_state* M, mrb_value self)
{
  mrb_value pp;
  mrb_get_args(M, "o", &pp);

	if (mrb_type(self) != MRB_TT_DATA) {
		return mrb_nil_value();
	}

	// Start with #<Qt::HBoxLayout:0x30139030>
	// Drop the closing '>'
	mrb_value inspect_str = mrb_funcall(M, self, "to_s", 0);
	mrb_str_resize(M, inspect_str, RSTRING_LEN(inspect_str) - 1);
	mrb_funcall(M, pp, "text", 1, inspect_str);
	mrb_funcall(M, pp, "breakable", 0);

	smokeruby_object * o = 0;
  Data_Get_Struct(M, self, &smokeruby_type, o);
	QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);

	QString value_list;

	if (qobject->parent() != 0) {
		QString parentInspectString;
		mrb_value obj = getPointerObject(M, qobject->parent());
		if (not mrb_nil_p(obj)) {
			mrb_value parent_inspect_str = mrb_funcall(M, obj, "to_s", 0);
			mrb_str_resize(M, parent_inspect_str, RSTRING_LEN(parent_inspect_str) - 1);
			parentInspectString = mrb_string_value_ptr(M, parent_inspect_str);
		} else {
			parentInspectString.sprintf("#<%s:0x0", qobject->parent()->metaObject()->className());
		}

		if (qobject->parent()->isWidgetType()) {
			QWidget * w = (QWidget *) qobject->parent();
			value_list = QString("  parent=%1 objectName=\"%2\", x=%3, y=%4, width=%5, height=%6>,\n")
												.arg(parentInspectString)
												.arg(w->objectName())
												.arg(w->x())
												.arg(w->y())
												.arg(w->width())
												.arg(w->height());
		} else {
			value_list = QString("  parent=%1 objectName=\"%2\">,\n")
												.arg(parentInspectString)
												.arg(qobject->parent()->objectName());
		}

		mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, value_list.toLatin1()));
	}

	if (qobject->children().count() != 0) {
		value_list = QString("  children=Array (%1 element(s)),\n")
								.arg(qobject->children().count());
		mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, value_list.toLatin1()));
	}

	value_list = QString("  metaObject=#<Qt::MetaObject:0x0");
	value_list.append(QString(" className=%1").arg(qobject->metaObject()->className()));

	if (qobject->metaObject()->superClass() != 0) {
		value_list.append(	QString(", superClass=#<Qt::MetaObject:0x0 className=%1>")
							.arg(qobject->metaObject()->superClass()->className()) );
	}

	value_list.append(">,\n");
	mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, value_list.toLatin1()));

	QMetaProperty property = qobject->metaObject()->property(0);
	QVariant value = property.read(qobject);
	value_list = " " + inspectProperty(property, property.name(), value);
	mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, value_list.toLatin1()));

	for (int index = 1; index < qobject->metaObject()->propertyCount(); index++) {
		mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, ",\n"));

		property = qobject->metaObject()->property(index);
		value = property.read(qobject);
		value_list = " " + inspectProperty(property, property.name(), value);
		mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, value_list.toLatin1()));
	}

	mrb_funcall(M, pp, "text", 1, mrb_str_new_cstr(M, ">"));

	return self;
}

static mrb_value
q_register_resource_data(mrb_state* M, mrb_value /*self*/)
{
  mrb_value tree_value, name_value, data_value;
  mrb_int version;
  mrb_get_args(M, "iSSS", &version, &tree_value, &name_value, &data_value);
	const unsigned char * tree = (const unsigned char *) malloc(RSTRING_LEN(tree_value) + 1);
	memcpy((void *) tree, (const void *) RSTRING_PTR(tree_value), RSTRING_LEN(tree_value));

	const unsigned char * name = (const unsigned char *) malloc(RSTRING_LEN(name_value) + 1);
	memcpy((void *) name, (const void *) RSTRING_PTR(name_value), RSTRING_LEN(name_value));

	const unsigned char * data = (const unsigned char *) malloc(RSTRING_LEN(data_value) + 1);
	memcpy((void *) data, (const void *) RSTRING_PTR(data_value), RSTRING_LEN(data_value));

	return mrb_bool_value(qRegisterResourceData(version, tree, name, data));
}

static mrb_value
q_unregister_resource_data(mrb_state* M, mrb_value /*self*/)
{
  mrb_int version;
  mrb_value tree_value, name_value, data_value;
  mrb_get_args(M, "iSSS", &version, &tree_value, &name_value, &data_value);

	const unsigned char * tree = (const unsigned char *) malloc(RSTRING_LEN(tree_value) + 1);
	memcpy((void *) tree, (const void *) RSTRING_PTR(tree_value), RSTRING_LEN(tree_value));

	const unsigned char * name = (const unsigned char *) malloc(RSTRING_LEN(name_value) + 1);
	memcpy((void *) name, (const void *) RSTRING_PTR(name_value), RSTRING_LEN(name_value));

	const unsigned char * data = (const unsigned char *) malloc(RSTRING_LEN(data_value) + 1);
	memcpy((void *) data, (const void *) RSTRING_PTR(data_value), RSTRING_LEN(data_value));

	return mrb_bool_value(qUnregisterResourceData(version, tree, name, data));
}

static mrb_value
qabstract_item_model_rowcount(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
  switch(argc) {
    case 0: return mrb_fixnum_value(model->rowCount());
    case 1: {
      QModelIndex * modelIndex = (QModelIndex *) value_obj_info(M, argv[0])->ptr;
      return mrb_fixnum_value(model->rowCount(*modelIndex));
    }
    default:
      mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
      return mrb_nil_value();
	}
}

static mrb_value
qabstract_item_model_columncount(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
  switch(argc) {
    case 0: return mrb_fixnum_value(model->columnCount());
    case 1: return mrb_fixnum_value(
        model->columnCount(*((QModelIndex *) value_obj_info(M, argv[0])->ptr)));
    default:
      mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
      return mrb_nil_value();
	}
}

static mrb_value
qabstract_item_model_data(mrb_state* M, mrb_value self)
{
  mrb_value model_index_value; mrb_int idx;
  int const argc = mrb_get_args(M, "o|i", &model_index_value, &idx);

  smokeruby_object* o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;
	QModelIndex * modelIndex = (QModelIndex *) value_obj_info(M, model_index_value)->ptr;
	QVariant value;
  switch(argc) {
    case 1: value = model->data(*modelIndex); break;
    case 2: value = model->data(*modelIndex, idx); break;
    default: assert(false); return mrb_nil_value();
  }

	return set_obj_info(M, "Qt::Variant", alloc_smokeruby_object(
      M,	true, o->smoke, o->smoke->findClass("QVariant").index, new QVariant(value)));
}

static mrb_value
qabstract_item_model_setdata(mrb_state* M, mrb_value self)
{
  mrb_value model_index_value, value; mrb_int idx;
  int const argc = mrb_get_args(M, "oo|i", &model_index_value, &value, &idx);

	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
	QModelIndex * modelIndex = (QModelIndex *) value_obj_info(M, model_index_value)->ptr;
	QVariant * variant = (QVariant *) value_obj_info(M, value)->ptr;
  switch(argc) {
    case 2: return mrb_bool_value(model->setData(*modelIndex, *variant));
    case 3: return mrb_bool_value(model->setData(*modelIndex, *variant, idx));
    default: assert(false); return mrb_nil_value();
  }
}

static mrb_value
qabstract_item_model_flags(mrb_state* M, mrb_value self)
{
  mrb_value model_index;
  mrb_get_args(M, "o", &model_index);
	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
	const QModelIndex * modelIndex = (const QModelIndex *) value_obj_info(M, model_index)->ptr;
	return mrb_fixnum_value((int) model->flags(*modelIndex));
}

static mrb_value
qabstract_item_model_insertrows(mrb_state* M, mrb_value self)
{
	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
  mrb_int a, b; mrb_value idx;
  switch(mrb_get_args(M, "ii|o", &a, &b, &idx)) {
    case 2: return mrb_bool_value(model->insertRows(a, b));
    case 3: return mrb_bool_value(model->insertRows(
        a, b, *((const QModelIndex *) value_obj_info(M, idx)->ptr)));
    default: assert(false); return mrb_nil_value();
  }
}

static mrb_value
qabstract_item_model_insertcolumns(mrb_state* M, mrb_value self)
{
  mrb_int a, b; mrb_value idx;
	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
  switch(mrb_get_args(M, "ii|o", &a, &b, &idx)) {
    case 2: return mrb_bool_value(model->insertColumns(a, b));
    case 3: return mrb_bool_value(model->insertColumns(
        a, b, *((const QModelIndex *) value_obj_info(M, idx)->ptr)));
    default: assert(false); return mrb_nil_value();
	}
}

static mrb_value
qabstract_item_model_removerows(mrb_state* M, mrb_value self)
{
  mrb_int a, b; mrb_value idx;
	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
  switch(mrb_get_args(M, "ii|o", &a, &b, &idx)) {
    case 2: return mrb_bool_value(model->removeRows(a, b));
    case 3: return mrb_bool_value(model->removeRows(
        a, b, *((const QModelIndex *) value_obj_info(M, idx)->ptr)));
    default: assert(false); return mrb_nil_value();
  }
}

static mrb_value
qabstract_item_model_removecolumns(mrb_state* M, mrb_value self)
{
	QAbstractItemModel * model = (QAbstractItemModel *) value_obj_info(M, self)->ptr;
  mrb_int a, b; mrb_value idx;
  switch(mrb_get_args(M, "ii|o", &a, &b, &idx)) {
    case 2: return mrb_bool_value(model->removeColumns(a, b));
    case 3: return mrb_bool_value(model->removeColumns(
        a, b, *((const QModelIndex *) value_obj_info(M, idx)->ptr)));
    default: assert(false); return mrb_nil_value();
  }
}

// There is a QByteArray operator method in the Smoke lib that takes a QString
// arg and returns a QString. This is normally the desired behaviour, so
// special case a '+' method here.
static mrb_value
qbytearray_append(mrb_state* M, mrb_value self)
{
  mrb_value str;
  mrb_get_args(M, "o", &str);
	(*((QByteArray *) value_obj_info(M, self)->ptr)) += (const char *)mrb_string_value_ptr(M, str);
	return self;
}

static mrb_value
qbytearray_data(mrb_state* M, mrb_value self)
{
  QByteArray * bytes = (QByteArray *) value_obj_info(M, self)->ptr;
  return mrb_str_new(M, bytes->data(), bytes->size());
}

static mrb_value
qimage_bits(mrb_state* M, mrb_value self)
{
  QImage * image = static_cast<QImage *>(value_obj_info(M, self)->ptr);
  const uchar * bytes = image->bits();
  return mrb_str_new(M, (const char *) bytes, image->numBytes());
}

static mrb_value
qimage_scan_line(mrb_state* M, mrb_value self)
{
  mrb_int ix;
  mrb_get_args(M, "i", &ix);
  QImage * image = static_cast<QImage *>(value_obj_info(M, self)->ptr);
  const uchar * bytes = image->scanLine(ix);
  return mrb_str_new(M, (const char *) bytes, image->bytesPerLine());
}

#ifdef QT_QTDBUS
static mrb_value
qdbusargument_endarraywrite(mrb_state* M, mrb_value self)
{
	((QDBusArgument *) value_obj_info(M, self)->ptr)->endArray();
	return self;
}

static mrb_value
qdbusargument_endmapwrite(mrb_state* M, mrb_value self)
{
	((QDBusArgument *) value_obj_info(M, self)->ptr)->endMap();
	return self;
}

static mrb_value
qdbusargument_endmapentrywrite(mrb_state* M, mrb_value self)
{
	((QDBusArgument *) value_obj_info(M, self)->ptr)->endMapEntry();
	return self;
}

static mrb_value
qdbusargument_endstructurewrite(mrb_state* M, mrb_value self)
{
	((QDBusArgument *) value_obj_info(M, self)->ptr)->endStructure();
	return self;
}

#endif

// The QtRuby runtime's overloaded method resolution mechanism can't currently
// distinguish between Ruby Arrays containing different sort of instances.
// Unfortunately Qt::Painter.drawLines() and Qt::Painter.drawRects() methods can
// be passed a Ruby Array as an argument containing either Qt::Points or Qt::PointFs
// for instance. These methods need to call the correct Qt C++ methods, so special case
// the overload method resolution for now..
static mrb_value
qpainter_drawlines(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

static Smoke::Index drawlines_pointf_vector = 0;
static Smoke::Index drawlines_point_vector = 0;
static Smoke::Index drawlines_linef_vector = 0;
static Smoke::Index drawlines_line_vector = 0;

	if (argc == 1 && mrb_type(argv[0]) == MRB_TT_ARRAY && RARRAY_LEN(argv[0]) > 0) {
		if (drawlines_point_vector == 0) {
			Smoke::ModuleIndex nameId = qtcore_Smoke->findMethodName("QPainter", "drawLines?");
			Smoke::ModuleIndex meth = qtcore_Smoke->findMethod(qtcore_Smoke->findClass("QPainter"), nameId);
			Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
			i = -i;		// turn into ambiguousMethodList index
			while (meth.smoke->ambiguousMethodList[i] != 0) {
				const char * argType = meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args]].name;

				if (qstrcmp(argType, "const QVector<QPointF>&" ) == 0) {
					drawlines_pointf_vector = meth.smoke->ambiguousMethodList[i];
				} else if (qstrcmp(argType, "const QVector<QPoint>&" ) == 0) {
					drawlines_point_vector = meth.smoke->ambiguousMethodList[i];
				} else if (qstrcmp(argType, "const QVector<QLineF>&" ) == 0) {
					drawlines_linef_vector = meth.smoke->ambiguousMethodList[i];
				} else if (qstrcmp(argType, "const QVector<QLine>&" ) == 0) {
					drawlines_line_vector = meth.smoke->ambiguousMethodList[i];
				}

				i++;
			}
		}

		smokeruby_object * o = value_obj_info(M, mrb_ary_entry(argv[0], 0));

    Smoke::ModuleIndex _current_method;
		if (qstrcmp(o->smoke->classes[o->classId].className, "QPointF") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_pointf_vector;
		} else if (qstrcmp(o->smoke->classes[o->classId].className, "QPoint") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_point_vector;
		} else if (qstrcmp(o->smoke->classes[o->classId].className, "QLineF") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_linef_vector;
		} else if (qstrcmp(o->smoke->classes[o->classId].className, "QLine") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_line_vector;
		} else {
			return mrb_call_super(M, self);
		}

		QtRuby::MethodCall c(M, _current_method, self, argv, argc - 1);
		c.next();
		return self;
	}

	return mrb_call_super(M, self);
}

static mrb_value
qpainter_drawrects(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

static Smoke::Index drawlines_rectf_vector = 0;
static Smoke::Index drawlines_rect_vector = 0;

	if (argc == 1 && mrb_type(argv[0]) == MRB_TT_ARRAY && RARRAY_LEN(argv[0]) > 0) {
		if (drawlines_rectf_vector == 0) {
			Smoke::ModuleIndex nameId = qtcore_Smoke->findMethodName("QPainter", "drawRects?");
			Smoke::ModuleIndex meth = qtcore_Smoke->findMethod(qtcore_Smoke->findClass("QPainter"), nameId);
			Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
			i = -i;		// turn into ambiguousMethodList index
			while (meth.smoke->ambiguousMethodList[i] != 0) {
				const char * argType = meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args]].name;

				if (qstrcmp(argType, "const QVector<QRectF>&" ) == 0) {
					drawlines_rectf_vector = meth.smoke->ambiguousMethodList[i];
				} else if (qstrcmp(argType, "const QVector<QRect>&" ) == 0) {
					drawlines_rect_vector = meth.smoke->ambiguousMethodList[i];
				}

				i++;
			}
		}

		smokeruby_object * o = value_obj_info(M, mrb_ary_entry(argv[0], 0));

    Smoke::ModuleIndex _current_method;
		if (qstrcmp(o->smoke->classes[o->classId].className, "QRectF") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_rectf_vector;
		} else if (qstrcmp(o->smoke->classes[o->classId].className, "QRect") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_rect_vector;
		} else {
			return mrb_call_super(M, self);
		}

		QtRuby::MethodCall c(M, _current_method, self, argv, argc-1);
		c.next();
		return self;
	}

	return mrb_call_super(M, self);
}

static mrb_value
qabstractitemmodel_createindex(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc == 2 || argc == 3) {
    mrb_int a, b; mrb_value ptr = mrb_nil_value();
    mrb_get_args(M, "ii|o", &a, &b, &ptr);

		smokeruby_object * o = value_obj_info(M, self);
		Smoke::ModuleIndex nameId = o->smoke->idMethodName("createIndex$$$");
		Smoke::ModuleIndex meth = o->smoke->findMethod(qtcore_Smoke->findClass("QAbstractItemModel"), nameId);
		Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
		i = -i;		// turn into ambiguousMethodList index
		while (o->smoke->ambiguousMethodList[i] != 0) {
			if (	qstrcmp(	o->smoke->types[o->smoke->argumentList[o->smoke->methods[o->smoke->ambiguousMethodList[i]].args + 2]].name,
                      "void*" ) == 0 )
			{
        const Smoke::Method &m = o->smoke->methods[o->smoke->ambiguousMethodList[i]];
				Smoke::ClassFn fn = o->smoke->classes[m.classId].classFn;
				Smoke::StackItem stack[4];
				stack[1].s_int = a;
				stack[2].s_int = b;
        assert(mrb_nil_p(ptr) or mrb_type(argv[2]) == MRB_TT_VOIDP);
        stack[3].s_voidp = mrb_nil_p(ptr)? NULL : mrb_voidp(argv[2]);
				(*fn)(m.method, o->ptr, stack);
				smokeruby_object  * result = alloc_smokeruby_object(	M, true,
                                                              o->smoke,
                                                              o->smoke->idClass("QModelIndex").index,
                                                              stack[0].s_voidp );

				return set_obj_info(M, "Qt::ModelIndex", result);
			}

			i++;
		}
	}

	return mrb_call_super(M, self);
}

static mrb_value
qmodelindex_internalpointer(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
	QModelIndex * index = (QModelIndex *) o->ptr;
	void * ptr = index->internalPointer();
	return ptr != 0 ? mrb_voidp_value(M, ptr) : mrb_nil_value();
}

static mrb_value
qitemselection_at(mrb_state* M, mrb_value self)
{
  mrb_int i;
  mrb_get_args(M, "i", &i);
    smokeruby_object *o = value_obj_info(M, self);
	QItemSelection * item = (QItemSelection *) o->ptr;
	QItemSelectionRange range = item->at(i);

	smokeruby_object  * result = alloc_smokeruby_object(	M, true,
															o->smoke,
															o->smoke->idClass("QItemSelectionRange").index,
															new QItemSelectionRange(range) );

	return set_obj_info(M, "Qt::ItemSelectionRange", result);
}

static mrb_value
qitemselection_count(mrb_state* M, mrb_value self)
{
	return mrb_fixnum_value(((QItemSelection *) value_obj_info(M, self)->ptr)->count());
}

RClass* get_class(mrb_state* M, mrb_value const& self) {
  return mrb_type(self) == MRB_TT_MODULE or mrb_type(self) == MRB_TT_CLASS
      ? mrb_class_ptr(self) : mrb_class(M, self);
}

struct MetaInfo {
  std::unordered_map<std::string, unsigned> methods;
  std::unordered_map<std::string, std::string> method_tags;
  std::vector<std::pair<std::string, std::string>> classinfos;

  bool dirty = true, dbus = false;

  QMetaObject meta_object;

  std::string stringdata;
  std::vector<uint> data;

  static unsigned stringdata_offset(std::unordered_map<std::string, unsigned>& table,
                                    std::string& str, char const* name) {
    auto const i = table.find(name);
    if(i != table.end()) { return i->second; }

    unsigned const ret = str.size(); // offset
    str += name;
    str += std::string("\0", 1);
    table[name] = ret;
    return ret;
  }

  void print_debug_message() const {
    QMetaObject const& meta = meta_object;
    qWarning("metaObject() superdata: %p %s", meta.d.superdata, meta.d.superdata->className());
    auto const& priv_meta = *reinterpret_cast<QMetaObjectPrivate const*>(data.data());
    assert(&priv_meta);

    qWarning(
        " // content:\n"
        "       %d,       // revision\n"
        "       %d,       // classname\n"
        "       %d,   %d, // classinfo\n"
        "       %d,   %d, // methods\n"
        "       %d,   %d, // properties\n"
        "       %d,   %d, // enums/sets",
        priv_meta.revision,
        priv_meta.className,
        priv_meta.classInfoCount, priv_meta.classInfoData,
        priv_meta.methodCount, priv_meta.methodData,
        priv_meta.propertyCount, priv_meta.propertyData,
        priv_meta.enumeratorCount, priv_meta.enumeratorData);

    if (priv_meta.classInfoCount > 0) {
      int const s = priv_meta.classInfoData;
      qWarning(" // classinfo: key, value");
      for (uint j = 0; j < data[2]; j++) {
        qWarning("      %d,    %d", data[s + (j * 2)], data[s + (j * 2) + 1]);
      }
    }

    for (int j = 0; j < priv_meta.methodCount; j++) {
      int const s = priv_meta.methodData;
      qWarning("      %d,   %d,   %d,   %d, 0x%2.2x // %s: signature, parameters, type, tag, flags",
               data[s + (j * 5)], data[s + (j * 5) + 1], data[s + (j * 5) + 2],
               data[s + (j * 5) + 3], data[s + (j * 5) + 4],
               (data[s + (j * 5) + 4] & MethodSignal)? "signal" : "slot");
    }

    for (int j = 0; j < priv_meta.propertyCount; j++) {
      int const s = priv_meta.propertyData;
      qWarning("      %d,   %d,   0x%8.8x, // properties: name, type, flags",
               data[s + (j * 3)], data[s + (j * 3) + 1], data[s + (j * 3) + 2]);
    }

    /* enum/set
    s += (data[6] * 3);
    for (int i = s; i < count; i++) {
      qWarning("\n       %d        // eod\n", data[i]);
    }
    */

    qWarning("qt_meta_stringdata:");
    std::string str = stringdata.data();
    for(size_t idx = 0; idx < stringdata.size();
        idx += str.size() + 1, str = stringdata.data() + idx) {
      qWarning("  offset: %d, string: \"%s\"", unsigned(idx), str.c_str());
    }
  }

  void update(mrb_state* M, mrb_value const& self) {
    if(not dirty) { return; }

    RClass* const cls = get_class(M, self);
    RClass* const parent = cls->super;
    QMetaObject const* const superdata = (QMetaObject*)value_obj_info(
        M, mrb_funcall(M, mrb_obj_value(parent), "staticMetaObject", 0))->ptr;
    assert(superdata);

    std::unordered_map<std::string, unsigned> table;
    std::string str;
    std::vector<unsigned> d = {
      4, // revision
      stringdata_offset(table, str, mrb_class_name(M, cls)), // classname
      static_cast<unsigned>(classinfos.size()), 0, // classinfo
      static_cast<unsigned>(methods.size()), 0, // method -> signal/slot
      0, 0, // property
      0, 0, // enumerator, set
      0, 0, // constructor (revision 2)
      0,  // flags (revision 3)

      // signals count (revision 4)
      // count signals by checking Qt::MethodSignal flag
      unsigned(std::count_if(methods.begin(), methods.end(),
                             [](std::pair<std::string, unsigned> const& i) {
                               return bool(i.second & MethodSignal);
                             })),
    };
    assert(d.size() == 14); // in revision 4 it must be 14
    d[3] = classinfos.empty()? 0 : d.size(); // classinfos offset
    d[5] = d.size() + 2 * classinfos.size(); // methods offset

    unsigned const empty_str = stringdata_offset(table, str, "");

    // classinfo
    for(auto const& i : classinfos) {
      d.push_back(stringdata_offset(table, str, i.first.c_str()));
      d.push_back(stringdata_offset(table, str, i.second.c_str()));
    }

    // method
    for(auto const& i : methods) {
      d.push_back(stringdata_offset(table, str, i.first.c_str())); // signature
      d.push_back(empty_str); // parameters (if empty string is passed paramter types is used instead)
      d.push_back(empty_str); // return type (empty string means void or not defined)

      // method tag string
      auto const tag = method_tags.find(i.first);
      d.push_back(tag == method_tags.end()
                     ? empty_str : stringdata_offset(table, str, tag->second.c_str()));

      // method flags (use special method for dbus interface)
      unsigned const dbus_method_flags = MethodScriptable | AccessPublic;
      d.push_back(dbus
                  ? (dbus_method_flags | ((i.second & MethodSignal)? MethodSignal : MethodSlot))
                  : i.second);
    }

    d.push_back(0); // end of data

    stringdata.swap(str);
    data.swap(d);

    meta_object = { { superdata, stringdata.c_str(), data.data(), NULL } };

    dirty = false;

    if(do_debug & qtdb_gc) { print_debug_message(); }
  }
};

static mrb_value
qt_metacall(mrb_state* M, mrb_value self)
{
	// Arguments: QMetaObject::Call _c, int id, void ** _o
  mrb_int calltype, id; mrb_value args;
  mrb_get_args(M, "iio", &calltype, &id, &args);
	QMetaObject::Call _c = (QMetaObject::Call) calltype;
	// Note that for a slot with no args and no return type,
	// it isn't an error to get a NULL value of _o here.
  assert(mrb_voidp_p(args));
	void ** const _o = (void**)mrb_voidp(args);

	// Assume the target slot is a C++ one
	smokeruby_object *o = value_obj_info(M, self);
	Smoke::ModuleIndex meth =o->smoke->findMethod(
      Smoke::ModuleIndex(o->smoke, o->classId), o->smoke->idMethodName("qt_metacall$$?"));
	assert(meth != Smoke::NullModuleIndex); // qt_metacall must be found

	const Smoke::Method &m = meth.smoke->methods[meth.smoke->methodMaps[meth.index].method];
  Smoke::ClassFn fn = meth.smoke->classes[m.classId].classFn;
  Smoke::StackItem i[4];
  i[1].s_enum = _c;
  i[2].s_int = id;
  i[3].s_voidp = _o;
  (*fn)(m.method, o->ptr, i);
  int ret = i[0].s_int;
  if (ret < 0) { return mrb_fixnum_value(ret); }
  if (_c != QMetaObject::InvokeMetaMethod) { return mrb_fixnum_value(id); }

	QObject * qobj = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
	// get obj metaobject with a virtual call
	const QMetaObject *metaobject = qobj->metaObject();

	// get method/property count
	int const count = _c == QMetaObject::InvokeMetaMethod
                    ? metaobject->methodCount() : metaobject->propertyCount();

	if (_c == QMetaObject::InvokeMetaMethod) {
		QMetaMethod method = metaobject->method(id);

		if (method.methodType() == QMetaMethod::Signal) {
			metaobject->activate(qobj, id, _o);
			return mrb_fixnum_value(id - count);
		}

		QList<MocArgument*> mocArgs =
        get_moc_arguments(M, o->smoke, method.typeName(), method.parameterTypes());

		QString name(method.signature());
    static QRegExp rx("\\(.*");
		name.replace(rx, "");
		QtRuby::InvokeSlot slot(M, self, mrb_intern_cstr(M, name.toLatin1()), mocArgs, _o);
		slot.next();
	}

	return mrb_fixnum_value(id - count);
}

static mrb_value metaObject(mrb_state* M, mrb_value self);
static mrb_value staticMetaObject(mrb_state* M, mrb_value self);

MetaInfo& get_meta_info(mrb_state* M, RClass* cls, bool mark_dirty = true) {
  static std::unordered_map<RClass*, MetaInfo> meta_info_table_;

  if(meta_info_table_.find(cls) == meta_info_table_.end()) {
    assert(mark_dirty);
    mrb_define_method(M, cls, "qt_metacall", qt_metacall, MRB_ARGS_ANY());
    mrb_define_method(M, cls, "metaObject", metaObject, MRB_ARGS_NONE());
    mrb_define_module_function(M, cls, "staticMetaObject", staticMetaObject, MRB_ARGS_NONE());
    auto& ret = meta_info_table_[cls];
    ret.dirty = true;
    return ret;
  }
  auto& ret = meta_info_table_[cls];
  if(mark_dirty) { ret.dirty = true; } // mark dirty
  return ret;
}

static mrb_value
metaObject(mrb_state* M, mrb_value self)
{
  MetaInfo& info = get_meta_info(M, mrb_class(M, self), false); // don't change update dirty state
  info.update(M, self);
	smokeruby_object  * m = alloc_smokeruby_object(
      M, false, qtcore_Smoke, qtcore_Smoke->idClass("QMetaObject").index,
      &info.meta_object);
  return mrb_obj_value(Data_Wrap_Struct(M, qmetaobject_class(M), &smokeruby_type, m));
}

static mrb_value
staticMetaObject(mrb_state* M, mrb_value self)
{
  MetaInfo& info = get_meta_info(M, get_class(M, self), false); // don't change update dirty state
  info.update(M, self);
	smokeruby_object  * m = alloc_smokeruby_object(
      M, false, qtcore_Smoke, qtcore_Smoke->idClass("QMetaObject").index,
      &info.meta_object);
  return mrb_obj_value(Data_Wrap_Struct(M, qmetaobject_class(M), &smokeruby_type, m));
}

static mrb_value
qt_signal(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	smokeruby_object *o = value_obj_info(M, self);
	QObject *qobj = (QObject*)o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
  if (qobj->signalsBlocked()) {
    return mrb_false_value();
  }

  size_t len;
  QByteArray signalname(mrb_sym2name_len(M, M->c->ci->mid, &len), len);

  mrb_value metaObject_value = mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "getMetaObject", 2, mrb_nil_value(), self);

	smokeruby_object *ometa = value_obj_info(M, metaObject_value);
	if (ometa == 0) {
		return mrb_nil_value();
	}

  int i = -1;
	const QMetaObject * m = (QMetaObject*) ometa->ptr;
  for (i = m->methodCount() - 1; i > -1; i--) {
		if (m->method(i).methodType() == QMetaMethod::Signal) {
			QString name(m->method(i).signature());
      static QRegExp * rx = 0;
			if (rx == 0) {
				rx = new QRegExp("\\(.*");
			}
			name.replace(*rx, "");

			if (name == signalname) {
				break;
			}
		}
  }

	if (i == -1) {
		return mrb_nil_value();
	}

	QList<MocArgument*> args = get_moc_arguments(M, o->smoke, m->method(i).typeName(), m->method(i).parameterTypes());

	mrb_value result = mrb_nil_value();
	// Okay, we have the signal info. *whew*
	QtRuby::EmitSignal signal(M, qobj, i, argc, args, argv, &result);
	signal.next();

	return result;
}

void define_signal_slot(mrb_state* M, char const* name, MetaInfo& info, unsigned flags, unsigned& added) {
  if(name[0] == '1' or name[0] == '2') { ++name; }
  if(info.methods.find(name) != info.methods.end()) {
    if(info.methods[name] != flags) {
      mrb_raisef(M, mrb_class_get(M, "RuntimeError"), "%S already defined", mrb_str_new_cstr(M, name));
    }
  } else { ++added; }
  info.methods[name] = flags;
}

// signal/slot
mrb_value qt_base_signals(mrb_state* M, mrb_value self) {
  mrb_value* argv; int argc;
  mrb_get_args(M, "*", &argv, &argc);
  RClass* cls = get_class(M, self);
  MetaInfo& info = get_meta_info(M, cls);
  unsigned added = 0;
  for(mrb_value* i = argv; i < (argv + argc); ++i) {
    char const* name = mrb_string_value_ptr(M, *i);
    define_signal_slot(M, name, info, MethodSignal | AccessProtected, added);
		mrb_define_method(M, cls, std::regex_replace(name, std::regex("[12]?(\\w+)\\(.*\\)"), "$1").c_str(),
                      qt_signal, MRB_ARGS_ANY());
  }
  if(added == 0) { info.dirty = false; } // don't mark dirty if none is added
  return self;
}

template<unsigned Flags>
mrb_value qt_base_slots(mrb_state* M, mrb_value self) {
  mrb_value* argv; int argc;
  mrb_get_args(M, "*", &argv, &argc);
  RClass* cls = get_class(M, self);
  MetaInfo& info = get_meta_info(M, cls);
  unsigned added = 0;
  for(mrb_value* i = argv; i < (argv + argc); ++i) {
    define_signal_slot(M, mrb_string_value_ptr(M, *i), info, Flags, added);
  }
  if(added == 0) { info.dirty = false; } // don't mark dirty if none is added
  return self;
}

#undef signal_slot_name_check

mrb_value qt_base_methodtag(mrb_state* M, mrb_value self) {
  char const* signature; char const* tag;
  mrb_get_args(M, "zz", &signature, &tag);
  MetaInfo& info = get_meta_info(M, get_class(M, self));
  assert(info.methods.find(signature) != info.methods.end());
  assert(info.method_tags.find(signature) == info.method_tags.end());
  info.method_tags[signature] = tag;
  return self;
}

mrb_value qt_base_classinfo(mrb_state* M, mrb_value self) {
  char const* key; char const* value;
  mrb_get_args(M, "zz", &key, &value);
  MetaInfo& info = get_meta_info(M, get_class(M, self));
  info.classinfos.emplace_back(key, value);

  // enable dbus mode if "D-Bus Interface" is defined
  if(std::strcmp(key, "D-Bus Interface") == 0) { info.dbus = true; }

  return self;
}

/* This shouldn't be needed, but kalyptus doesn't generate a staticMetaObject
	method for QObject::staticMetaObject, although it does for all the other
	classes, and it isn't obvious what the problem with it is.
	So add this as a hack to work round the bug.
*/
static mrb_value
qobject_staticmetaobject(mrb_state* M, mrb_value /*klass*/)
{
	QMetaObject * meta = new QMetaObject(QObject::staticMetaObject);

	smokeruby_object  * m = alloc_smokeruby_object(	M, true,
													qtcore_Smoke,
													qtcore_Smoke->idClass("QMetaObject").index,
													meta );

	mrb_value obj = set_obj_info(M, "Qt::MetaObject", m);
	return obj;
}

static mrb_value
cast_object_to(mrb_state* M, mrb_value /*self*/)
{
  mrb_value object, new_klass;
  mrb_get_args(M, "oo", &object, &new_klass);
    smokeruby_object *o = value_obj_info(M, object);

    mrb_value new_klassname = mrb_funcall(M, new_klass, "name", 0);

    Smoke::ModuleIndex const& cast_to_id = classcache.value(mrb_string_value_ptr(M, new_klassname));
    if (cast_to_id == Smoke::NullModuleIndex) {
      mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "unable to find class \"%S\" to cast to\n", mrb_string_value_ptr(M, new_klassname));
    }

	smokeruby_object * o_cast = alloc_smokeruby_object(	M, o->allocated,
														cast_to_id.smoke,
														(int) cast_to_id.index,
														o->smoke->cast(o->ptr, o->classId, (int) cast_to_id.index) );

  assert(mrb_type(new_klass) == MRB_TT_CLASS);
  mrb_value obj = mrb_obj_value(Data_Wrap_Struct(M, mrb_class_ptr(new_klass), &smokeruby_type, o_cast));
  mapPointer(M, obj, o_cast, o_cast->classId, 0);
    return obj;
}

static mrb_value
qobject_qt_metacast(mrb_state* M, mrb_value self)
{
  mrb_value klass;
  mrb_get_args(M, "o", &klass);
    smokeruby_object *o = value_obj_info(M, self);
	if (o == 0 || o->ptr == 0) {
		return mrb_nil_value();
	}

	const char * classname = mrb_obj_classname(M, klass);
	Smoke::ModuleIndex const& mi = classcache.value(classname);
	if (mi == Smoke::NullModuleIndex) { return mrb_nil_value(); }

	QObject* qobj = (QObject*) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
	if (qobj == 0) { return mrb_nil_value(); }

	void* ret = qobj->qt_metacast(mi.smoke->classes[mi.index].className);
	if (ret == 0) { return mrb_nil_value(); }

	smokeruby_object * o_cast = alloc_smokeruby_object(	M, o->allocated,
														mi.smoke,
														(int) mi.index,
														ret );

  assert(mrb_type(klass) == MRB_TT_CLASS);
  mrb_value obj = mrb_obj_value(Data_Wrap_Struct(M, mrb_class_ptr(klass), &smokeruby_type, o_cast));
  mapPointer(M, obj, o_cast, o_cast->classId, 0);
    return obj;
}

static mrb_value
qsignalmapper_mapping(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc == 1 && mrb_type(argv[0]) == MRB_TT_DATA) {
		smokeruby_object *o = value_obj_info(M, self);
		smokeruby_object *a = value_obj_info(M, argv[0]);

		Smoke::ModuleIndex nameId = Smoke::NullModuleIndex;
		nameId = o->smoke->idMethodName("mapping#");
		Smoke::ModuleIndex ci(o->smoke, o->classId);
		Smoke::ModuleIndex meth = o->smoke->findMethod(ci, nameId);
		Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
		i = -i;		// turn into ambiguousMethodList index
		while (meth.smoke->ambiguousMethodList[i] != 0) {
			if (	(	qstrcmp(	meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args]].name,
									"QObject*" ) == 0
						&& Smoke::isDerivedFrom(a->smoke->classes[a->classId].className, "QObject")
						&& !Smoke::isDerivedFrom(a->smoke->classes[a->classId].className, "QWidget") )
					|| (	qstrcmp(	meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args]].name,
										"QWidget*" ) == 0
							&& Smoke::isDerivedFrom(a->smoke->classes[a->classId].className, "QWidget") ) )
			{
				QtRuby::MethodCall c(M, ModuleIndex(meth.smoke, meth.smoke->ambiguousMethodList[i]), self, argv, 1);
				c.next();
				return *(c.var());
			}

			i++;
		}
	}

	return mrb_call_super(M, self);
}

static mrb_value
qsignalmapper_set_mapping(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc == 2 && mrb_type(argv[0]) == MRB_TT_DATA && mrb_type(argv[1]) == MRB_TT_DATA) {
		smokeruby_object *o = value_obj_info(M, self);
		smokeruby_object *a = value_obj_info(M, argv[1]);

		Smoke::ModuleIndex nameId = Smoke::NullModuleIndex;
		nameId = o->smoke->idMethodName("setMapping##");
		Smoke::ModuleIndex ci(o->smoke, o->classId);
		Smoke::ModuleIndex meth = o->smoke->findMethod(ci, nameId);
		Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
		i = -i;		// turn into ambiguousMethodList index
		while (meth.smoke->ambiguousMethodList[i] != 0) {
			if (	(	qstrcmp(	meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args + 1]].name,
									"QObject*" ) == 0
						&& Smoke::isDerivedFrom(a->smoke->classes[a->classId].className, "QObject")
						&& !Smoke::isDerivedFrom(a->smoke->classes[a->classId].className, "QWidget") )
					|| (	qstrcmp(	meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args + 1]].name,
										"QWidget*" ) == 0
							&& Smoke::isDerivedFrom(a->smoke->classes[a->classId].className, "QWidget") ) )
			{
				QtRuby::MethodCall c(M, ModuleIndex(meth.smoke, meth.smoke->ambiguousMethodList[i]), self, argv, 2);
				c.next();
				return *(c.var());
			}

			i++;
		}
	}

	return mrb_call_super(M, self);
}

static int rObject_typeId;

// QMetaType helpers
static void delete_ruby_object(void *ptr)
{
	// mrb_gc_unregister_address((mrb_value*) ptr);
	delete (mrb_value*) ptr;
}

static void *create_ruby_object(const void *copyFrom)
{
	mrb_value *object;

	if (copyFrom) {
		object = new mrb_value(*(mrb_value*) copyFrom);
	} else {
		object = new mrb_value(mrb_nil_value());
	}

	// mrb_gc_register_address(object);
	return object;
}

static mrb_value
qvariant_value(mrb_state* M, mrb_value /*self*/)
{
  mrb_value variant_value_klass, variant_value;
  mrb_get_args(M, "oo", &variant_value_klass, &variant_value);
	void * value_ptr = 0;
	mrb_value result = mrb_nil_value();
	smokeruby_object * vo = 0;

    smokeruby_object *o = value_obj_info(M, variant_value);
	if (o == 0 || o->ptr == 0) {
		return mrb_nil_value();
	}

	QVariant * variant = (QVariant*) o->ptr;

	if (variant->userType() == rObject_typeId) {
		return *(mrb_value*) variant->data();
#ifdef QT_QTDBUS
	} else if (variant->userType() == qMetaTypeId<QDBusObjectPath>()) {
		QString s = qVariantValue<QDBusObjectPath>(*variant).path();
		return mrb_str_new_cstr(M, s.toLatin1());
	} else if (variant->userType() == qMetaTypeId<QDBusSignature>()) {
		QString s = qVariantValue<QDBusSignature>(*variant).signature();
		return mrb_str_new_cstr(M, s.toLatin1());
	} else if (variant->userType() == qMetaTypeId<QDBusVariant>()) {
		QVariant *ptr = new QVariant(qVariantValue<QDBusVariant>(*variant).variant());
		vo = alloc_smokeruby_object(M, true, qtcore_Smoke, qtcore_Smoke->idClass("QVariant").index, ptr);
		return set_obj_info(M, "Qt::Variant", vo);
#endif
	} else if (variant->type() >= QVariant::UserType) {
		// If the QVariant contains a user type, don't bother to look at the Ruby class argument
		value_ptr = QMetaType::construct(QMetaType::type(variant->typeName()), (void *) variant->constData());
		Smoke::ModuleIndex mi = o->smoke->findClass(variant->typeName());
		vo = alloc_smokeruby_object(M, true, mi.smoke, mi.index, value_ptr);
		return set_obj_info(M, qtruby_modules[mi.smoke].binding->className(mi.index), vo);
	}

	const char * classname = mrb_obj_classname(M, variant_value_klass);
    Smoke::ModuleIndex const& value_class_id = classcache.value(classname);
    if (value_class_id == Smoke::NullModuleIndex) {
		return mrb_nil_value();
	}

	if (qstrcmp(classname, "Qt::Pixmap") == 0) {
		QPixmap v = qVariantValue<QPixmap>(*variant);
		value_ptr = (void *) new QPixmap(v);
	} else if (qstrcmp(classname, "Qt::Font") == 0) {
		QFont v = qVariantValue<QFont>(*variant);
		value_ptr = (void *) new QFont(v);
	} else if (qstrcmp(classname, "Qt::Brush") == 0) {
		QBrush v = qVariantValue<QBrush>(*variant);
		value_ptr = (void *) new QBrush(v);
	} else if (qstrcmp(classname, "Qt::Color") == 0) {
		QColor v = qVariantValue<QColor>(*variant);
		value_ptr = (void *) new QColor(v);
	} else if (qstrcmp(classname, "Qt::Palette") == 0) {
		QPalette v = qVariantValue<QPalette>(*variant);
		value_ptr = (void *) new QPalette(v);
	} else if (qstrcmp(classname, "Qt::Icon") == 0) {
		QIcon v = qVariantValue<QIcon>(*variant);
		value_ptr = (void *) new QIcon(v);
	} else if (qstrcmp(classname, "Qt::Image") == 0) {
		QImage v = qVariantValue<QImage>(*variant);
		value_ptr = (void *) new QImage(v);
	} else if (qstrcmp(classname, "Qt::Polygon") == 0) {
		QPolygon v = qVariantValue<QPolygon>(*variant);
		value_ptr = (void *) new QPolygon(v);
	} else if (qstrcmp(classname, "Qt::Region") == 0) {
		QRegion v = qVariantValue<QRegion>(*variant);
		value_ptr = (void *) new QRegion(v);
	} else if (qstrcmp(classname, "Qt::Bitmap") == 0) {
		QBitmap v = qVariantValue<QBitmap>(*variant);
		value_ptr = (void *) new QBitmap(v);
	} else if (qstrcmp(classname, "Qt::Cursor") == 0) {
		QCursor v = qVariantValue<QCursor>(*variant);
		value_ptr = (void *) new QCursor(v);
	} else if (qstrcmp(classname, "Qt::SizePolicy") == 0) {
		QSizePolicy v = qVariantValue<QSizePolicy>(*variant);
		value_ptr = (void *) new QSizePolicy(v);
	} else if (qstrcmp(classname, "Qt::KeySequence") == 0) {
		QKeySequence v = qVariantValue<QKeySequence>(*variant);
		value_ptr = (void *) new QKeySequence(v);
	} else if (qstrcmp(classname, "Qt::Pen") == 0) {
		QPen v = qVariantValue<QPen>(*variant);
		value_ptr = (void *) new QPen(v);
	} else if (qstrcmp(classname, "Qt::TextLength") == 0) {
		QTextLength v = qVariantValue<QTextLength>(*variant);
		value_ptr = (void *) new QTextLength(v);
	} else if (qstrcmp(classname, "Qt::TextFormat") == 0) {
		QTextFormat v = qVariantValue<QTextFormat>(*variant);
		value_ptr = (void *) new QTextFormat(v);
	} else if (qstrcmp(classname, "Qt::Variant") == 0) {
		value_ptr = (void *) new QVariant(*((QVariant *) variant->constData()));
	} else {
		// Assume the value of the Qt::Variant can be obtained
		// with a call such as Qt::Variant.toPoint()
		QByteArray toValueMethodName(classname);
		if (toValueMethodName.startsWith("Qt::")) {
			toValueMethodName.remove(0, strlen("Qt::"));
		}
		toValueMethodName.prepend("to");
		return mrb_funcall(M, variant_value, toValueMethodName, 1, variant_value);
	}

	vo = alloc_smokeruby_object(M, true, value_class_id.smoke, value_class_id.index, value_ptr);
	result = set_obj_info(M, classname, vo);

	return result;
}

static mrb_value
qvariant_from_value(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc == 2) {
		Smoke::ModuleIndex nameId = Smoke::NullModuleIndex;
		const char *typeName = mrb_string_value_ptr(M, argv[1]);

		if (mrb_type(argv[0]) == MRB_TT_DATA) {
			nameId = qtcore_Smoke->idMethodName("QVariant#");
		} else if (mrb_type(argv[0]) == MRB_TT_ARRAY || qstrcmp(typeName, "long long") == 0 || qstrcmp(typeName, "unsigned long long") == 0) {
			nameId = qtcore_Smoke->idMethodName("QVariant?");
		} else {
			nameId = qtcore_Smoke->idMethodName("QVariant$");
		}

		Smoke::ModuleIndex meth = qtcore_Smoke->findMethod(qtcore_Smoke->idClass("QVariant"), nameId);
		Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
		i = -i;		// turn into ambiguousMethodList index
		while (meth.smoke->ambiguousMethodList[i] != 0) {
			if (	qstrcmp(	meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args]].name,
								typeName ) == 0 )
			{
				QtRuby::MethodCall c(M, ModuleIndex(meth.smoke, meth.smoke->ambiguousMethodList[i]), self, argv, 0);
				c.next();
				return *(c.var());
			}

			i++;
		}

		if(do_debug & qtdb_gc) qWarning("No suitable method for signature QVariant::QVariant(%s) found - looking for another suitable constructor\n", mrb_string_value_ptr(M, argv[1]));
	}

	QVariant * v = 0;

	const char * classname = mrb_obj_classname(M, argv[0]);
    smokeruby_object *o = value_obj_info(M, argv[0]);
	int type = 0;

	if (qstrcmp(classname, "Qt::Enum") == 0) {
		return mrb_funcall(M, mrb_obj_value(qvariant_class(M)), "new", 1, mrb_funcall(M, argv[0], "to_i", 0));
	} else if (o && o->ptr && (type = QVariant::nameToType(o->smoke->className(o->classId)))) {
		v = new QVariant(type, o->ptr);
	} else {
		int error = 0;
		mrb_value result = mrb_funcall(M, mrb_obj_value(qvariant_class(M)), "new", 1, argv[0]);
		if (!error) {
			return result;
		} else {
			mrb_value lasterr = mrb_gv_get(M, mrb_intern_lit(M, "$!"));
			char const* klass = mrb_obj_classname(M, lasterr);
			if (qstrcmp(klass, "ArgumentError") == 0) {
				// ArgumentError - no suitable constructor found
				// Create a QVariant that contains an rObject
				v = new QVariant(rObject_typeId, &argv[0]);
			} else {
				mrb_exc_raise(M, lasterr); // , "while creating the QVariant");
			}
		}
	}

	smokeruby_object * vo = alloc_smokeruby_object(M, true, qtcore_Smoke, qtcore_Smoke->idClass("QVariant").index, v);
	mrb_value result = set_obj_info(M, "Qt::Variant", vo);

	return result;
}

static mrb_value
new_qvariant(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

static Smoke::Index new_qvariant_qlist = 0;
static Smoke::Index new_qvariant_qmap = 0;

	if (new_qvariant_qlist == 0) {
		Smoke::ModuleIndex nameId = qtcore_Smoke->findMethodName("QVariant", "QVariant?");
		Smoke::ModuleIndex meth = qtcore_Smoke->findMethod(qtcore_Smoke->findClass("QVariant"), nameId);
		Smoke::Index i = meth.smoke->methodMaps[meth.index].method;
		i = -i;		// turn into ambiguousMethodList index
		while (qtcore_Smoke->ambiguousMethodList[i] != 0) {
			const char * argType = meth.smoke->types[meth.smoke->argumentList[meth.smoke->methods[meth.smoke->ambiguousMethodList[i]].args]].name;

			if (qstrcmp(argType, "const QList<QVariant>&" ) == 0) {
				new_qvariant_qlist = meth.smoke->ambiguousMethodList[i];
			} else if (qstrcmp(argType, "const QMap<QString,QVariant>&" ) == 0) {
				new_qvariant_qmap = meth.smoke->ambiguousMethodList[i];
			}

			i++;
		}
	}

	if (argc == 1 && mrb_type(argv[0]) == MRB_TT_HASH) {
		QtRuby::MethodCall c(M, ModuleIndex(qtcore_Smoke, new_qvariant_qmap), self, argv, argc-1);
		c.next();
    	return *(c.var());
	} else if (	argc == 1
				&& mrb_type(argv[0]) == MRB_TT_ARRAY
				&& RARRAY_LEN(argv[0]) > 0
				&& mrb_type(mrb_ary_entry(argv[0], 0)) != MRB_TT_STRING )
	{
		QtRuby::MethodCall c(M, ModuleIndex(qtcore_Smoke, new_qvariant_qlist), self, argv, argc-1);
		c.next();
		return *(c.var());
	}

	return mrb_call_super(M, self);
}

/*

class LCDRange < Qt::Widget

	def initialize(s, parent, name)
		super(parent, name)
		init()
		...

For a case such as the above, the QWidget can't be instantiated until
the initializer has been run up to the point where 'super(parent, name)'
is called. Only then, can the number and type of arguments passed to the
constructor be known. However, the rest of the intializer
can't be run until 'self' is a proper MRB_TT_DATA object with a wrapped C++
instance.

The solution is to run the initialize code twice. First, only up to the
'super(parent, name)' call, where the QWidget would get instantiated in
initialize_qt(). And then mrb_throw() jumps out of the
initializer returning the wrapped object as a result.

The second time round 'self' will be the wrapped instance of type MRB_TT_DATA,
so initialize() can be allowed to proceed to the end.
*/
static mrb_value
initialize_qt(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_value blk;
  mrb_get_args(M, "&*", &blk, &argv, &argc);

	mrb_value temp_obj;

	RClass* klass = mrb_class(M, self);

  QByteArray mcid;
  Smoke::ModuleIndex _current_method = find_cached_selector(M, argc, argv, klass, "new", mcid);
  if (_current_method == Smoke::NullModuleIndex) {
    _current_method = do_method_missing(M, "Qt", "new", klass, argc, argv);
	}
  if (_current_method != Smoke::NullModuleIndex) {
    // Success. Cache result.
    methcache.insert(mcid, _current_method);
  } else {
    if (do_debug & qtdb_calls) qWarning("unresolved constructor call %s", mrb_class_name(M, klass));
		mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "unresolved constructor call %S\n", mrb_class_path(M, klass));
	}

  DATA_TYPE(self) = &smokeruby_type;

	{
		// Allocate the MethodCall within a C block. Otherwise, because the continue_new_instance()
		// call below will longjmp out, it wouldn't give C++ an opportunity to clean up
		QtRuby::MethodCall c(M, _current_method, self, argv, argc);
		c.next();
		temp_obj = *(c.var());
	}

	smokeruby_object * p = 0;
	Data_Get_Struct(M, temp_obj, &smokeruby_type, p);

	smokeruby_object  * o = alloc_smokeruby_object(	M, true,
													p->smoke,
													p->classId,
													p->ptr );
	p->ptr = 0;
	p->allocated = false;

  assert(mrb_type(self) == MRB_TT_DATA);
  DATA_PTR(self) = o;
	mapObject(M, self, self);

  // If a ruby block was passed then run that now
  if (not mrb_nil_p(blk)) {
    mrb_yield_internal(M, blk, 0, NULL, self, mrb_class(M, self));
  }

	return self;
}


// Returns $qApp.ARGV() - the original ARGV array with Qt command line options removed
static mrb_value
qapplication_argv(mrb_state* M, mrb_value /*self*/)
{
	mrb_value result = mrb_ary_new(M);
	// Drop argv[0], as it isn't included in the ruby global ARGV
	for (int index = 1; index < qApp->argc(); index++) {
		mrb_ary_push(M, result, mrb_str_new_cstr(M, qApp->argv()[index]));
	}

	return result;
}

//----------------- Sig/Slot ------------------

static QByteArray
to_normalized_invoke_signature(char const* raw) {
  static std::regex const args_rex("([12]?).+\\((.*)\\)");
  return QMetaObject::normalizedSignature(std::regex_replace(raw, args_rex, "1invoke($2)").c_str());
}

static mrb_value
qobject_connect(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv; mrb_value blk;
  mrb_get_args(M, "&*", &blk, &argv, &argc);
  std::array<mrb_value, 4> args;
  RClass* const Qt = mrb_class_get(M, "Qt");

	if (mrb_nil_p(blk)) {
		if (argc == 3 and not mrb_string_p(argv[1])) {
      // Handles calls of the form:
      //  connect(:mysig, mytarget, :mymethod))
      //  connect(SIGNAL('mysignal(int)'), mytarget, :mymethod))
      QByteArray const signature = to_normalized_invoke_signature(mrb_string_value_ptr(M, argv[0]));
      static std::regex const method_name_rex("[12]?(.+)\\(.*\\)");
      std::string const method_name = std::regex_replace(
          mrb_string_value_ptr(M, argv[2]), method_name_rex, "$1");
      args = { {
          self, argv[0], mrb_funcall(
              M, mrb_obj_value(mrb_class_get_under(M, Qt, "MethodInvocation")), "new", 3,
              argv[1], mrb_symbol_value(mrb_intern(M, method_name.data(), method_name.size())),
              mrb_symbol_value(mrb_intern(M, signature.constData(), signature.size()))),
          mrb_symbol_value(mrb_intern(M, signature.constData(), signature.size())) } };
		} else { return mrb_call_super(M, self); }
  } else {
    switch(argc) {
      case 1: {
        // Handles calls of the form:
        //  connect(SIGNAL(:mysig)) { ...}
        //  connect(SIGNAL('mysig(int)')) {|arg(s)| ...}
        QByteArray const signature = to_normalized_invoke_signature(mrb_string_value_ptr(M, argv[0]));
        args = { {
            self, argv[0], mrb_funcall(
                M, mrb_obj_value(mrb_class_get_under(M, Qt, "SignalBlockInvocation")), "new", 3,
                self, mrb_funcall(M, blk, "to_proc", 0), mrb_symbol_value(mrb_intern(M, signature.constData(), signature.size()))),
            mrb_symbol_value(mrb_intern(M, signature.constData(), signature.size())) } };
        break;
      }

      case 2:
      case 3: {
        // Handles calls of the form:
        //  connect(myobj, SIGNAL('mysig(int)'), mytarget) {|arg(s)| ...}
        //  connect(myobj, SIGNAL('mysig(int)')) {|arg(s)| ...}
        //  connect(myobj, SIGNAL(:mysig), mytarget) { ...}
        //  connect(myobj, SIGNAL(:mysig)) { ...}
        QByteArray const signature = to_normalized_invoke_signature(mrb_string_value_ptr(M, argv[1]));
        args = { {
            argv[0], argv[1], mrb_funcall(
                M, mrb_obj_value(mrb_class_get_under(M, Qt, "BlockInvocation")), "new", 3,
                argc == 3? argv[2] : self, mrb_funcall(M, blk, "to_proc", 0),
                mrb_symbol_value(mrb_intern(M, signature.constData(), signature.size()))),
            mrb_symbol_value(mrb_intern(M, signature.constData(), signature.size())) } };
        break;
      }
      default:
        mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list: %S", mrb_fixnum_value(argc));
		}
	}

  return mrb_funcall_argv(M, mrb_obj_value(mrb_class_get_under(M, Qt, "Object")),
                          mrb_intern_lit(M, "connect"), 4, args.data());
}

static mrb_value
qtimer_single_shot(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv; mrb_value blk;
  mrb_get_args(M, "&*", &blk, &argv, &argc);
	if (not mrb_nil_p(blk) and argc == 2) {
    return mrb_funcall(
        M, self, "singleShot", 3, argv[0],
        mrb_funcall(M, mrb_obj_value(mrb_class_get_under(M, mrb_class_get(M, "Qt"), "BlockInvocation")),
                    "new", 2, argv[1], blk, mrb_str_new_cstr(M, "invoke()")),
        mrb_str_new_cstr(M, SLOT(invoke())));
	} else { return mrb_call_super(M, self); }
}

// --------------- Ruby C functions for Qt::_internal.* helpers  ----------------

// Return the class name of a QObject. Note that the name will be in the
// form of Qt::Widget rather than QWidget. Is this a bug or a feature?
static mrb_value
class_name(mrb_state* M, mrb_value self)
{
  mrb_value klass = mrb_funcall(M, self, "class", 0);
  return mrb_funcall(M, klass, "name", 0);
}

// Allow classnames in both 'Qt::Widget' and 'QWidget' formats to be
// used as an argument to Qt::Object.inherits()
static mrb_value
inherits_qobject(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc != 1) {
		return mrb_call_super(M, self);
	}

	Smoke::ModuleIndex const& classId = classcache.value(mrb_string_value_ptr(M, argv[0]));

	if (classId == Smoke::NullModuleIndex) {
		return mrb_call_super(M, self);
	} else {
		// mrb_value super_class = mrb_str_new_cstr(M, classId.smoke->classes[classId.index].className);
		return mrb_call_super(M, self);
	}
}

/* Adapted from the internal function qt_qFindChildren() in qobject.cpp */
static void
rb_qFindChildren_helper(mrb_state* M, mrb_value parent, const QString &name, mrb_value re,
                         const QMetaObject &mo, mrb_value list)
{
  if (mrb_nil_p(parent) || mrb_nil_p(list))
        return;
	mrb_value children = mrb_funcall(M, parent, "children", 0);
  mrb_value rv = mrb_nil_value();
    for (int i = 0; i < RARRAY_LEN(children); ++i) {
        rv = RARRAY_PTR(children)[i];
		smokeruby_object *o = value_obj_info(M, rv);
		QObject * obj = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);

		// The original code had 'if (mo.cast(obj))' as a test, but it doesn't work here
        if (obj->qt_metacast(mo.className()) != 0) {
          if (not mrb_nil_p(re)) {
            mrb_value re_test = mrb_funcall(M, re, "=~", 1, mrb_funcall(M, rv, "objectName", 0));
            if (not mrb_nil_p(re_test) && mrb_type(re_test) != MRB_TT_FALSE) {
              mrb_ary_push(M, list, rv);
				}
            } else {
                if (name.isNull() || obj->objectName() == name) {
                  mrb_ary_push(M, list, rv);
				}
            }
        }
        rb_qFindChildren_helper(M, rv, name, re, mo, list);
    }
	return;
}

/* Should mimic Qt4's QObject::findChildren method with this syntax:
     obj.findChildren(Qt::Widget, "Optional Widget Name")
*/
static mrb_value
find_qobject_children(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc < 1 || argc > 2 || mrb_type(argv[0]) != MRB_TT_CLASS) mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");

	QString name;
	mrb_value re = mrb_nil_value();
	if (argc == 2) {
		// If the second arg isn't a String, assume it's a regular expression
		if (mrb_type(argv[1]) == MRB_TT_STRING) {
			name = QString::fromLatin1(mrb_string_value_ptr(M, argv[1]));
		} else {
			re = argv[1];
		}
	}

	mrb_value metaObject = mrb_funcall(M, argv[0], "staticMetaObject", 0);
	smokeruby_object *o = value_obj_info(M, metaObject);
	QMetaObject * mo = (QMetaObject*) o->ptr;
	mrb_value result = mrb_ary_new(M);
	rb_qFindChildren_helper(M, self, name, re, *mo, result);
	return result;
}

/* Adapted from the internal function qt_qFindChild() in qobject.cpp */
static mrb_value
rb_qFindChild_helper(mrb_state* M, mrb_value parent, const QString &name, const QMetaObject &mo)
{
  if (mrb_nil_p(parent))
    return mrb_nil_value();
	mrb_value children = mrb_funcall(M, parent, "children", 0);
    mrb_value rv;
	int i;
    for (i = 0; i < RARRAY_LEN(children); ++i) {
        rv = RARRAY_PTR(children)[i];
		smokeruby_object *o = value_obj_info(M, rv);
		QObject * obj = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
        if (obj->qt_metacast(mo.className()) != 0 && (name.isNull() || obj->objectName() == name))
            return rv;
    }
    for (i = 0; i < RARRAY_LEN(children); ++i) {
      rv = rb_qFindChild_helper(M, RARRAY_PTR(children)[i], name, mo);
      if (not mrb_nil_p(rv))
            return rv;
    }
    return mrb_nil_value();
}

static mrb_value
find_qobject_child(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc < 1 || argc > 2 || mrb_type(argv[0]) != MRB_TT_CLASS) mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");

	QString name;
	if (argc == 2) {
		name = QString::fromLatin1(mrb_string_value_ptr(M, argv[1]));
	}

	mrb_value metaObject = mrb_funcall(M, argv[0], "staticMetaObject", 0);
	smokeruby_object *o = value_obj_info(M, metaObject);
	QMetaObject * mo = (QMetaObject*) o->ptr;
	return rb_qFindChild_helper(M, self, name, *mo);
}

static mrb_value
setDebug(mrb_state* M, mrb_value self)
{
  mrb_int on_value;
  mrb_get_args(M, "i", &on_value);
  do_debug = on_value;
  return self;
}

static mrb_value
debugging(mrb_state*, mrb_value /*self*/)
{
    return mrb_fixnum_value(do_debug);
}

ModuleIndex
find_pclassid(char const* p) {
  return p? classcache.value(p) : Smoke::NullModuleIndex;
}

static mrb_value
dispose(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
    if (o == 0 || o->ptr == 0) { return mrb_nil_value(); }

    const char *className = o->smoke->classes[o->classId].className;
	if(do_debug & qtdb_gc) qWarning("Deleting (%s*)%p\n", className, o->ptr);

	unmapPointer(o, o->classId, 0);
	object_count--;

	char *methodName = new char[strlen(className) + 2];
	methodName[0] = '~';
	strcpy(methodName + 1, className);
	Smoke::ModuleIndex nameId = o->smoke->findMethodName(className, methodName);
	Smoke::ModuleIndex classIdx(o->smoke, o->classId);
	Smoke::ModuleIndex meth = nameId.smoke->findMethod(classIdx, nameId);
	if(meth.index > 0) {
		const Smoke::Method &m = meth.smoke->methods[meth.smoke->methodMaps[meth.index].method];
		Smoke::ClassFn fn = meth.smoke->classes[m.classId].classFn;
		Smoke::StackItem i[1];
		(*fn)(m.method, o->ptr, i);
	}
	delete[] methodName;
	o->ptr = 0;
	o->allocated = false;

	return mrb_nil_value();
}

static mrb_value
is_disposed(mrb_state* M, mrb_value self)
{
	smokeruby_object *o = value_obj_info(M, self);
	return mrb_bool_value(not (o != 0 && o->ptr != 0));
}

static RClass*
create_qobject_class(mrb_state* M, char const* package, RClass* klass)
{
  mrb_value const module_value = mrb_obj_value(klass);
	QString packageName(package);
	for(QString const& s : packageName.mid(RSTRING_LEN(mrb_class_path(M, klass)) + 2).split("::")) {
		klass = mrb_define_class_under(M, klass, s.toLatin1().constData(), qt_base_class(M));
	}

	if (packageName == "Qt::Application" || packageName == "Qt::CoreApplication" ) {
		mrb_define_method(M, klass, "ARGV", qapplication_argv, MRB_ARGS_NONE());
	} else if (packageName == "Qt::Object") {
		mrb_define_module_function(M, klass, "staticMetaObject", qobject_staticmetaobject, MRB_ARGS_NONE());
	} else if (packageName == "Qt::AbstractTableModel") {
		RClass* QTableModel_class = mrb_define_class_under(M, qt_module(M), "TableModel", klass);
    MRB_SET_INSTANCE_TT(QTableModel_class, MRB_TT_DATA);
		mrb_define_method(M, QTableModel_class, "rowCount", qabstract_item_model_rowcount, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "row_count", qabstract_item_model_rowcount, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "columnCount", qabstract_item_model_columncount, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "column_count", qabstract_item_model_columncount, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "data", qabstract_item_model_data, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "setData", qabstract_item_model_setdata, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "set_data", qabstract_item_model_setdata, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "flags", qabstract_item_model_flags, 1);
		mrb_define_method(M, QTableModel_class, "insertRows", qabstract_item_model_insertrows, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "insert_rows", qabstract_item_model_insertrows, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "insertColumns", qabstract_item_model_insertcolumns, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "insert_columns", qabstract_item_model_insertcolumns, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "removeRows", qabstract_item_model_removerows, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "remove_rows", qabstract_item_model_removerows, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "removeColumns", qabstract_item_model_removecolumns, MRB_ARGS_ANY());
		mrb_define_method(M, QTableModel_class, "remove_columns", qabstract_item_model_removecolumns, MRB_ARGS_ANY());

		RClass* QListModel_class = mrb_define_class_under(M, qt_module(M), "ListModel", klass);
    MRB_SET_INSTANCE_TT(QListModel_class, MRB_TT_DATA);
		mrb_define_method(M, QListModel_class, "rowCount", qabstract_item_model_rowcount, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "row_count", qabstract_item_model_rowcount, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "columnCount", qabstract_item_model_columncount, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "column_count", qabstract_item_model_columncount, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "data", qabstract_item_model_data, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "setData", qabstract_item_model_setdata, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "set_data", qabstract_item_model_setdata, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "flags", qabstract_item_model_flags, 1);
		mrb_define_method(M, QListModel_class, "insertRows", qabstract_item_model_insertrows, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "insert_rows", qabstract_item_model_insertrows, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "insertColumns", qabstract_item_model_insertcolumns, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "insert_columns", qabstract_item_model_insertcolumns, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "removeRows", qabstract_item_model_removerows, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "remove_rows", qabstract_item_model_removerows, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "removeColumns", qabstract_item_model_removecolumns, MRB_ARGS_ANY());
		mrb_define_method(M, QListModel_class, "remove_columns", qabstract_item_model_removecolumns, MRB_ARGS_ANY());
	}
	else if (packageName == "Qt::AbstractItemModel") {
		mrb_define_method(M, klass, "createIndex", qabstractitemmodel_createindex, MRB_ARGS_ANY());
		mrb_define_method(M, klass, "create_index", qabstractitemmodel_createindex, MRB_ARGS_ANY());
	} else if (packageName == "Qt::Timer") {
		mrb_define_module_function(M, klass, "singleShot", qtimer_single_shot, MRB_ARGS_ANY());
		mrb_define_module_function(M, klass, "single_shot", qtimer_single_shot, MRB_ARGS_ANY());
	}


	mrb_define_method(M, klass, "qobject_cast", qobject_qt_metacast, MRB_ARGS_REQ(1));
	mrb_define_method(M, klass, "inspect", inspect_qobject, MRB_ARGS_NONE());
	mrb_define_method(M, klass, "pretty_print", pretty_print_qobject, MRB_ARGS_REQ(1));
	mrb_define_method(M, klass, "className", class_name, MRB_ARGS_NONE());
	mrb_define_method(M, klass, "class_name", class_name, MRB_ARGS_NONE());
	mrb_define_method(M, klass, "inherits", inherits_qobject, MRB_ARGS_ANY());
	mrb_define_method(M, klass, "findChildren", find_qobject_children, MRB_ARGS_ANY());
	mrb_define_method(M, klass, "find_children", find_qobject_children, MRB_ARGS_ANY());
	mrb_define_method(M, klass, "findChild", find_qobject_child, MRB_ARGS_ANY());
	mrb_define_method(M, klass, "find_child", find_qobject_child, MRB_ARGS_ANY());
	mrb_define_method(M, klass, "connect", qobject_connect, MRB_ARGS_ANY());
	mrb_define_module_function(M, klass, "connect", qobject_connect, MRB_ARGS_ANY());

	foreach(QtRubyModule m, qtruby_modules.values()) {
		if (m.class_created)
			m.class_created(package, module_value, mrb_obj_value(klass));
	}

	return klass;
}

static RClass*
create_qt_class(mrb_state* M, char const* package, RClass* klass)
{
  mrb_value const module_value = mrb_obj_value(klass);
	QString packageName(package);
  foreach(QString s, packageName.mid(RSTRING_LEN(mrb_class_path(M, klass)) + 2).split("::")) {
		klass = mrb_define_class_under(M, klass, (const char*) s.toLatin1(), qt_base_class(M));
	}

	if (packageName == "Qt::Variant") {
		RClass* QVariant_class = qvariant_class(M);
		mrb_define_module_function(M, QVariant_class, "fromValue", qvariant_from_value, MRB_ARGS_ANY());
		mrb_define_module_function(M, QVariant_class, "from_value", qvariant_from_value, MRB_ARGS_ANY());
    mrb_define_module_function(M, QVariant_class, "new", new_qvariant, MRB_ARGS_ANY());
	} else if (packageName == "Qt::ByteArray") {
		mrb_define_method(M, klass, "+", qbytearray_append, MRB_ARGS_REQ(1));
    mrb_define_method(M, klass, "data", qbytearray_data, MRB_ARGS_NONE());
    mrb_define_method(M, klass, "constData", qbytearray_data, MRB_ARGS_NONE());
    mrb_define_method(M, klass, "const_data", qbytearray_data, MRB_ARGS_NONE());
	} else if (packageName == "Qt::Char") {
		mrb_define_method(M, klass, "to_s", qchar_to_s, MRB_ARGS_NONE());
	} else if (packageName == "Qt::Image") {
		mrb_define_method(M, klass, "bits", qimage_bits, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "scanLine", qimage_scan_line, MRB_ARGS_REQ(1));
	} else if (packageName == "Qt::ItemSelection") {
		mrb_define_method(M, klass, "[]", qitemselection_at, MRB_ARGS_REQ(1));
		mrb_define_method(M, klass, "at", qitemselection_at, MRB_ARGS_REQ(1));
		mrb_define_method(M, klass, "count", qitemselection_count, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "length", qitemselection_count, MRB_ARGS_NONE());
	} else if (packageName == "Qt::Painter") {
		mrb_define_method(M, klass, "drawLines", qpainter_drawlines, MRB_ARGS_ANY());
		mrb_define_method(M, klass, "draw_lines", qpainter_drawlines, MRB_ARGS_ANY());
		mrb_define_method(M, klass, "drawRects", qpainter_drawrects, MRB_ARGS_ANY());
		mrb_define_method(M, klass, "draw_rects", qpainter_drawrects, MRB_ARGS_ANY());
	} else if (packageName == "Qt::ModelIndex") {
		mrb_define_method(M, klass, "internalPointer", qmodelindex_internalpointer, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "internal_pointer", qmodelindex_internalpointer, MRB_ARGS_NONE());
	} else if (packageName == "Qt::SignalMapper") {
		mrb_define_method(M, klass, "mapping", qsignalmapper_mapping, MRB_ARGS_ANY());
		mrb_define_method(M, klass, "setMapping", qsignalmapper_set_mapping, MRB_ARGS_ANY());
		mrb_define_method(M, klass, "set_mapping", qsignalmapper_set_mapping, MRB_ARGS_ANY());
#ifdef QT_QTDBUS
	} else if (packageName == "Qt::DBusArgument") {
		mrb_define_method(M, klass, "endArrayWrite", qdbusargument_endarraywrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "end_array_write", qdbusargument_endarraywrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "endMapEntryWrite", qdbusargument_endmapentrywrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "end_map_entry_write", qdbusargument_endmapentrywrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "endMapWrite", qdbusargument_endmapwrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "end_map_write", qdbusargument_endmapwrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "endStructureWrite", qdbusargument_endstructurewrite, MRB_ARGS_NONE());
		mrb_define_method(M, klass, "end_structure_write", qdbusargument_endstructurewrite, MRB_ARGS_NONE());
#endif
	}

	foreach(QtRubyModule m, qtruby_modules.values()) {
		if (m.class_created)
			m.class_created(package, module_value, mrb_obj_value(klass));
	}

	return klass;
}

static mrb_value
version(mrb_state* M, mrb_value /*self*/)
{
    return mrb_str_new_cstr(M, QT_VERSION_STR);
}

static mrb_value
qtruby_version(mrb_state* M, mrb_value /*self*/)
{
    return mrb_str_new_cstr(M, QTRUBY_VERSION);
}

static mrb_value
set_application_terminated(mrb_state* M, mrb_value /*self*/)
{
  mrb_value yn;
  mrb_get_args(M, "o", &yn);
  application_terminated = mrb_test(yn);
	return mrb_nil_value();
}

static std::string
normalize_classname(std::string str)
{
  if(str.size() >= 2 and str.substr(0, 2) == "Q3") {
    static std::regex const qt3_regexp("^Q3(\\w*)$");
    return std::regex_replace(str, qt3_regexp, "Qt3::$1");
  } else if(str.size() >= 4 and str.substr(0, 4) == "Qsci") {
    static std::regex const qsci_regexp("^Qsci(\\w+)$");
    return std::regex_replace(str, qsci_regexp, "Qsci::$1");
  } else if(str.size() >= 1 and str[0] == 'Q') {
    static std::regex const qt_regexp("^Q(\\w+)$");
    return std::regex_replace(str, qt_regexp, "Qt::$1");
  }
  return str;
}

static void
init_class_list(mrb_state* M, Smoke* s, RClass* const mod)
{
  RClass* const internal = qt_internal_module(M);
  mrb_sym const QtClassName = mrb_intern_lit(M, "QtClassName");
  mrb_value const
      Classes = mrb_mod_cv_get(M, internal, mrb_intern_lit(M, "Classes")),
      CppNames = mrb_mod_cv_get(M, internal, mrb_intern_lit(M, "CppNames")),
      IdClass = mrb_mod_cv_get(M, internal, mrb_intern_lit(M, "IdClass"));

  assert(mrb_hash_p(Classes) and mrb_hash_p(CppNames) and mrb_array_p(IdClass));

  for(int id = 1; id < s->numClasses; ++id) {
    PROTECT_SCOPE();
    if (!s->classes[id].className or s->classes[id].external) { continue; }

    char const* name = s->classes[id].className;

    if (strlen(name) == 0 or
        strcmp(name, "Qt") == 0 or
        strcmp(name, "QInternal") == 0 or strcmp(name, "WebCore") == 0 or
        strcmp(name, "std") == 0 or strcmp(name, "QGlobalSpace") == 0
        ) { continue; }

    std::string const normalized = normalize_classname(name);
    mrb_value const
        normalized_mruby = mrb_symbol_value(mrb_intern(M, normalized.data(), normalized.size())),
        classname_mruby = mrb_symbol_value(mrb_intern_cstr(M, name));
    mrb_ary_set(M, IdClass, id, normalized_mruby);
    mrb_hash_set(M, CppNames, normalized_mruby, classname_mruby);

    classcache.insert(normalized.c_str(), Smoke::ModuleIndex(s, id));
    IdToClassNameMap.insert(Smoke::ModuleIndex(s, id), new QByteArray(normalized.c_str()));

    RClass* const klass = (Smoke::isDerivedFrom(name, "QObject")
                           ? create_qobject_class : create_qt_class)(M, normalized.c_str(), mod);
    assert(klass);
    mrb_hash_set(M, Classes, normalized_mruby, mrb_obj_value(klass));
    mrb_mod_cv_set(M, klass, QtClassName, classname_mruby);
  }
}

static mrb_value
set_qtruby_embedded_wrapped(mrb_state* M, mrb_value /*self*/)
{
  mrb_value yn;
  mrb_get_args(M, "o", &yn);
  set_qtruby_embedded( mrb_test(yn) );
  return mrb_nil_value();
}

extern "C" void mrb_mruby_qt_gem_final(mrb_state*) {}

extern TypeHandler QtDeclarative_handlers[];
extern TypeHandler QtScript_handlers[];
extern TypeHandler QtTest_handlers[];
extern TypeHandler QtUiTools_handlers[];
extern TypeHandler QtWebKit_handlers[];

extern "C" Q_DECL_EXPORT void
mrb_mruby_qt_gem_init(mrb_state* M)
{
    RClass* Qt_module = mrb_define_module(M, "Qt");
		RClass* QtInternal_module = mrb_define_module_under(M, Qt_module, "Internal");
    mrb_mod_cv_set(M, QtInternal_module, mrb_intern_lit(M, "Classes"), mrb_hash_new(M));
    mrb_mod_cv_set(M, QtInternal_module, mrb_intern_lit(M, "CppNames"), mrb_hash_new(M));
    mrb_mod_cv_set(M, QtInternal_module, mrb_intern_lit(M, "IdClass"), mrb_ary_new(M));

    {
      mrb_value str = mrb_symbol_value(mrb_intern_lit(M, "Qt"));
      mrb_hash_set(M, mrb_mod_cv_get(M, QtInternal_module, mrb_intern_lit(M, "CppNames")), str, str);
      mrb_mod_cv_set(M, Qt_module, mrb_intern_lit(M, "QtClassName"), mrb_symbol_value(mrb_intern_lit(M, "QGlobalSpace")));
    }

		RClass* QtBase_class = mrb_define_class_under(M, Qt_module, "Base", M->object_class);
    MRB_SET_INSTANCE_TT(QtBase_class, MRB_TT_DATA);
    mrb_data_set_gc_marker(M, QtBase_class, &smokeruby_mark);

    RClass* QtModuleIndex_class = mrb_define_class_under(M, QtInternal_module, "ModuleIndex", M->object_class);
    MRB_SET_INSTANCE_TT(QtModuleIndex_class, MRB_TT_DATA);


#define INIT_BINDING(module, mod)  \
    init_ ## module ## _Smoke(); \
    static QtRuby::Binding module##_binding = QtRuby::Binding(M, module##_Smoke); \
    QtRubyModule module = { "QtRuby_" #module, resolve_classname_qt, 0, &module##_binding }; \
    qtruby_modules[module##_Smoke] = module; \
    smokeList << module##_Smoke; \
    init_class_list(M, module ## _Smoke, mod)

    INIT_BINDING(qtcore, Qt_module);
    INIT_BINDING(qtgui, Qt_module);
    INIT_BINDING(qtxml, Qt_module);
    INIT_BINDING(qtsql, Qt_module);
    INIT_BINDING(qtopengl, Qt_module);
    INIT_BINDING(qtnetwork, Qt_module);
    INIT_BINDING(qtsvg, Qt_module);
    INIT_BINDING(qtdeclarative, Qt_module);
    INIT_BINDING(qtscript, Qt_module);
    INIT_BINDING(qttest, Qt_module);
    INIT_BINDING(qtuitools, Qt_module);
    INIT_BINDING(qtwebkit, Qt_module);
#ifdef QT_QTDBUS
    INIT_BINDING(qtdbus, Qt_module);
#endif
#ifdef QT_QSCINTILLA2
    RClass* Qsci_module = mrb_define_module(M, "Qsci");
    INIT_BINDING(qsci, Qsci_module);
#endif

#undef INIT_BINDING

    install_handlers(Qt_handlers);
    install_handlers(QtWebKit_handlers);
    install_handlers(QtUiTools_handlers);
    install_handlers(QtTest_handlers);
    install_handlers(QtScript_handlers);
    install_handlers(QtDeclarative_handlers);

    if(not mrb_obj_respond_to(M, M->module_class, mrb_intern_lit(M, "name"))) {
      mrb_define_module_function(M, M->module_class, "name", &module_name, MRB_ARGS_NONE());
    }

    mrb_define_method(M, QtBase_class, "initialize", initialize_qt, MRB_ARGS_ANY());
    mrb_define_module_function(M, QtBase_class, "method_missing", class_method_missing, MRB_ARGS_ANY());
    mrb_define_module_function(M, Qt_module, "method_missing", class_method_missing, MRB_ARGS_ANY());
    mrb_define_method(M, QtBase_class, "method_missing", method_missing, MRB_ARGS_ANY());

    // signal/slot
    mrb_define_module_function(M, QtBase_class, "signals", qt_base_signals, MRB_ARGS_ANY());
    mrb_define_module_function(M, QtBase_class, "slots", qt_base_slots<MethodSlot | AccessPublic>, MRB_ARGS_ANY());
    mrb_define_module_function(M, QtBase_class, "q_signal", qt_base_signals, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtBase_class, "q_slot", qt_base_slots<MethodSlot | AccessPublic>, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtBase_class, "private_slots", qt_base_slots<MethodSlot | AccessPrivate>, MRB_ARGS_ANY());

    mrb_define_module_function(M, QtBase_class, "q_methodtag", qt_base_methodtag, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtBase_class, "q_classinfo", qt_base_classinfo, MRB_ARGS_REQ(2));

    mrb_define_module_function(M, QtBase_class, "const_missing", class_method_missing, MRB_ARGS_ANY());
    mrb_define_module_function(M, Qt_module, "const_missing", class_method_missing, MRB_ARGS_ANY());
    mrb_define_method(M, QtBase_class, "const_missing", method_missing, MRB_ARGS_ANY());

    mrb_define_method(M, QtBase_class, "dispose", dispose, MRB_ARGS_REQ(0));
    mrb_define_method(M, QtBase_class, "disposed?", is_disposed, MRB_ARGS_REQ(0));

    mrb_define_method(M, QtBase_class, "qVariantValue", qvariant_value, MRB_ARGS_REQ(2));
    mrb_define_method(M, QtBase_class, "qVariantFromValue", qvariant_from_value, MRB_ARGS_ANY());

    mrb_define_method(M, M->object_class, "qDebug", qdebug, MRB_ARGS_REQ(1));
    mrb_define_method(M, M->object_class, "qFatal", qfatal, MRB_ARGS_REQ(1));
    mrb_define_method(M, M->object_class, "qWarning", qwarning, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "debug=", setDebug, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "debug", debugging, MRB_ARGS_REQ(0));

    mrb_define_module_function(M, QtInternal_module, "mapObject", mapObject, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "cast_object_to", cast_object_to, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, Qt_module, "dynamic_cast", cast_object_to, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "set_qtruby_embedded", set_qtruby_embedded_wrapped, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "application_terminated=", set_application_terminated, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, Qt_module, "version", version, MRB_ARGS_NONE());
    mrb_define_module_function(M, Qt_module, "qtruby_version", qtruby_version, MRB_ARGS_NONE());

    mrb_define_module_function(M, Qt_module, "qRegisterResourceData", q_register_resource_data, MRB_ARGS_REQ(4));
    mrb_define_module_function(M, Qt_module, "qUnregisterResourceData", q_unregister_resource_data, MRB_ARGS_REQ(4));

    // TODO: make this method private
    mrb_define_method(M, M->module_class, "qt_methods", module_qt_methods, MRB_ARGS_REQ(2) | MRB_ARGS_OPT(1));

    mrb_define_module_function(M, QtBase_class, "ancestors", qt_base_ancestors, MRB_ARGS_NONE());
    mrb_define_method(M, QtBase_class, "qt_methods", qt_base_qt_methods, MRB_ARGS_REQ(2));

    rObject_typeId = QMetaType::registerType("rObject", &delete_ruby_object, &create_ruby_object);
}
// kate: space-indent false;

