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

#ifdef QT_QTDBUS
#include <smoke/qtdbus_smoke.h>
#endif

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/proc.h>

#include <regex>
#include <iostream>

#include "marshall_types.h"
#include "qtruby.h"

extern "C" mrb_value
mrb_yield_internal(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv, mrb_value self, struct RClass *c);

mrb_value mrb_call_super(mrb_state* M, mrb_value self, int argc, mrb_value* argv)
{
  RClass* sup = mrb_class(M, self)->super;
  RProc* p = mrb_method_search_vm(M, &sup, M->c->ci->mid);
  if(!p) {
    p = mrb_method_search_vm(M, &sup, mrb_intern_lit(M, "method_missing"));
  }

  assert(p);

  return mrb_yield_internal(M, mrb_obj_value(p), argc, argv, self, sup);
}

static mrb_value module_name(mrb_state* M, mrb_value self) {
  return mrb_class_path(M, mrb_class_ptr(self));
}

extern bool qRegisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);
extern bool qUnregisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);

extern TypeHandler Qt_handlers[];
extern const char * resolve_classname_qt(smokeruby_object * o);

extern "C" {

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
	case QVariant::String:
	{
		if (value.toString().isNull()) {
			return QString(" %1=nil").arg(name);
		} else {
			return QString(" %1=%2").arg(name).arg(value.toString());
		}
	}

	case QVariant::Bool:
	{
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
	{
		QColor c = value.value<QColor>();
		return QString(" %1=#<Qt::Color:0x0 %2>").arg(name).arg(c.name());
	}

	case QVariant::Cursor:
	{
		QCursor c = value.value<QCursor>();
		return QString(" %1=#<Qt::Cursor:0x0 shape=%2>").arg(name).arg(c.shape());
	}

	case QVariant::Double:
	{
		return QString(" %1=%2").arg(name).arg(value.toDouble());
	}

	case QVariant::Font:
	{
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

	case QVariant::Line:
	{
		QLine l = value.toLine();
		return QString(" %1=#<Qt::Line:0x0 x1=%2, y1=%3, x2=%4, y2=%5>")
						.arg(name)
						.arg(l.x1())
						.arg(l.y1())
						.arg(l.x2())
						.arg(l.y2());
	}

	case QVariant::LineF:
	{
		QLineF l = value.toLineF();
		return QString(" %1=#<Qt::LineF:0x0 x1=%2, y1=%3, x2=%4, y2=%5>")
						.arg(name)
						.arg(l.x1())
						.arg(l.y1())
						.arg(l.x2())
						.arg(l.y2());
	}

	case QVariant::Point:
	{
		QPoint p = value.toPoint();
		return QString(" %1=#<Qt::Point:0x0 x=%2, y=%3>").arg(name).arg(p.x()).arg(p.y());
	}

	case QVariant::PointF:
	{
		QPointF p = value.toPointF();
		return QString(" %1=#<Qt::PointF:0x0 x=%2, y=%3>").arg(name).arg(p.x()).arg(p.y());
	}

	case QVariant::Rect:
	{
		QRect r = value.toRect();
		return QString(" %1=#<Qt::Rect:0x0 left=%2, right=%3, top=%4, bottom=%5>")
									.arg(name)
									.arg(r.left()).arg(r.right()).arg(r.top()).arg(r.bottom());
	}

	case QVariant::RectF:
	{
		QRectF r = value.toRectF();
		return QString(" %1=#<Qt::RectF:0x0 left=%2, right=%3, top=%4, bottom=%5>")
									.arg(name)
									.arg(r.left()).arg(r.right()).arg(r.top()).arg(r.bottom());
	}

	case QVariant::Size:
	{
		QSize s = value.toSize();
		return QString(" %1=#<Qt::Size:0x0 width=%2, height=%3>")
									.arg(name)
									.arg(s.width()).arg(s.height());
	}

	case QVariant::SizeF:
	{
		QSizeF s = value.toSizeF();
		return QString(" %1=#<Qt::SizeF:0x0 width=%2, height=%3>")
									.arg(name)
									.arg(s.width()).arg(s.height());
	}

	case QVariant::SizePolicy:
	{
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
	{
		return QString(" %1=#<Qt::%2:0x0>").arg(name).arg(value.typeName() + 1);
	}

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
	mrb_value inspect_str = mrb_call_super(M, self, 0, 0);
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
	mrb_value inspect_str = mrb_funcall(M, self, "to_s", 0, 0);
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
			mrb_value parent_inspect_str = mrb_funcall(M, obj, "to_s", 0, 0);
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
  mrb_get_args(M, "iooo", &version, &tree_value, &name_value, &data_value);
	const unsigned char * tree = (const unsigned char *) malloc(RSTRING_LEN(tree_value));
	memcpy((void *) tree, (const void *) RSTRING_PTR(tree_value), RSTRING_LEN(tree_value));

	const unsigned char * name = (const unsigned char *) malloc(RSTRING_LEN(name_value));
	memcpy((void *) name, (const void *) RSTRING_PTR(name_value), RSTRING_LEN(name_value));

	const unsigned char * data = (const unsigned char *) malloc(RSTRING_LEN(data_value));
	memcpy((void *) data, (const void *) RSTRING_PTR(data_value), RSTRING_LEN(data_value));

	return mrb_bool_value(qRegisterResourceData(version, tree, name, data));
}

static mrb_value
q_unregister_resource_data(mrb_state* M, mrb_value /*self*/)
{
  mrb_int version;
  mrb_value tree_value, name_value, data_value;
  mrb_get_args(M, "iooo", &version, &tree_value, &name_value, &data_value);

	const unsigned char * tree = (const unsigned char *) malloc(RSTRING_LEN(tree_value));
	memcpy((void *) tree, (const void *) RSTRING_PTR(tree_value), RSTRING_LEN(tree_value));

	const unsigned char * name = (const unsigned char *) malloc(RSTRING_LEN(name_value));
	memcpy((void *) name, (const void *) RSTRING_PTR(name_value), RSTRING_LEN(name_value));

	const unsigned char * data = (const unsigned char *) malloc(RSTRING_LEN(data_value));
	memcpy((void *) data, (const void *) RSTRING_PTR(data_value), RSTRING_LEN(data_value));

	return mrb_bool_value(qUnregisterResourceData(version, tree, name, data));
}

static mrb_value
qabstract_item_model_rowcount(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;
	if (argc == 0) {
		return mrb_fixnum_value(model->rowCount());
	}

	if (argc == 1) {
		smokeruby_object * mi = value_obj_info(M, argv[0]);
		QModelIndex * modelIndex = (QModelIndex *) mi->ptr;
		return mrb_fixnum_value(model->rowCount(*modelIndex));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

static mrb_value
qabstract_item_model_columncount(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;
	if (argc == 0) {
		return mrb_fixnum_value(model->columnCount());
	}

	if (argc == 1) {
		smokeruby_object * mi = value_obj_info(M, argv[0]);
		QModelIndex * modelIndex = (QModelIndex *) mi->ptr;
		return mrb_fixnum_value(model->columnCount(*modelIndex));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

static mrb_value
qabstract_item_model_data(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object * o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;
    smokeruby_object * mi = value_obj_info(M, argv[0]);
	QModelIndex * modelIndex = (QModelIndex *) mi->ptr;
	QVariant value;
	if (argc == 1) {
		value = model->data(*modelIndex);
	} else if (argc == 2) {
		value = model->data(*modelIndex, mrb_fixnum(mrb_funcall(M, argv[1], "to_i", 0)));
	} else {
		mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
	}


	smokeruby_object  * result = alloc_smokeruby_object(M,	true,
															o->smoke,
															o->smoke->findClass("QVariant").index,
															new QVariant(value) );
	return set_obj_info(M, "Qt::Variant", result);
}

static mrb_value
qabstract_item_model_setdata(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;
    smokeruby_object * mi = value_obj_info(M, argv[0]);
	QModelIndex * modelIndex = (QModelIndex *) mi->ptr;
    smokeruby_object * v = value_obj_info(M, argv[1]);
	QVariant * variant = (QVariant *) v->ptr;

	if (argc == 2) {
		return mrb_bool_value(model->setData(*modelIndex, *variant));
	}

	if (argc == 3) {
		return mrb_bool_value(model->setData(
        *modelIndex, *variant,
        mrb_fixnum(mrb_funcall(M, argv[2], "to_i", 0))));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

static mrb_value
qabstract_item_model_flags(mrb_state* M, mrb_value self)
{
  mrb_value model_index;
  mrb_get_args(M, "o", &model_index);
    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;
    smokeruby_object * mi = value_obj_info(M, model_index);
	const QModelIndex * modelIndex = (const QModelIndex *) mi->ptr;
	return mrb_fixnum_value((int) model->flags(*modelIndex));
}

static mrb_value
qabstract_item_model_insertrows(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;

	if (argc == 2) {
		return mrb_bool_value(model->insertRows(mrb_fixnum(argv[0]), mrb_fixnum(argv[1])));
	}

	if (argc == 3) {
    	smokeruby_object * mi = value_obj_info(M, argv[2]);
		const QModelIndex * modelIndex = (const QModelIndex *) mi->ptr;
		return mrb_bool_value(model->insertRows(mrb_fixnum(argv[0]), mrb_fixnum(argv[1]), *modelIndex));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

static mrb_value
qabstract_item_model_insertcolumns(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;

	if (argc == 2) {
		return mrb_bool_value(model->insertColumns(mrb_fixnum(argv[0]), mrb_fixnum(argv[1])));
	}

	if (argc == 3) {
    	smokeruby_object * mi = value_obj_info(M, argv[2]);
		const QModelIndex * modelIndex = (const QModelIndex *) mi->ptr;
		return mrb_bool_value(model->insertColumns(mrb_fixnum(argv[0]), mrb_fixnum(argv[1]), *modelIndex));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

static mrb_value
qabstract_item_model_removerows(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;

	if (argc == 2) {
		return mrb_bool_value(model->removeRows(mrb_fixnum(argv[0]), mrb_fixnum(argv[1])));
	}

	if (argc == 3) {
    	smokeruby_object * mi = value_obj_info(M, argv[2]);
		const QModelIndex * modelIndex = (const QModelIndex *) mi->ptr;
		return mrb_bool_value(model->removeRows(mrb_fixnum(argv[0]), mrb_fixnum(argv[1]), *modelIndex));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

static mrb_value
qabstract_item_model_removecolumns(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

    smokeruby_object *o = value_obj_info(M, self);
	QAbstractItemModel * model = (QAbstractItemModel *) o->ptr;

	if (argc == 2) {
		return mrb_bool_value(model->removeColumns(mrb_fixnum(argv[0]), mrb_fixnum(argv[1])));
	}

	if (argc == 3) {
    	smokeruby_object * mi = value_obj_info(M, argv[2]);
		const QModelIndex * modelIndex = (const QModelIndex *) mi->ptr;
		return mrb_bool_value(model->removeRows(mrb_fixnum(argv[0]), mrb_fixnum(argv[1]), *modelIndex));
	}

	mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
  return mrb_nil_value();
}

// There is a QByteArray operator method in the Smoke lib that takes a QString
// arg and returns a QString. This is normally the desired behaviour, so
// special case a '+' method here.
static mrb_value
qbytearray_append(mrb_state* M, mrb_value self)
{
  mrb_value str;
  mrb_get_args(M, "o", &str);
    smokeruby_object *o = value_obj_info(M, self);
	QByteArray * bytes = (QByteArray *) o->ptr;
	(*bytes) += (const char *)mrb_string_value_ptr(M, str);
	return self;
}

static mrb_value
qbytearray_data(mrb_state* M, mrb_value self)
{
  smokeruby_object *o = value_obj_info(M, self);
  QByteArray * bytes = (QByteArray *) o->ptr;
  return mrb_str_new(M, bytes->data(), bytes->size());
}

static mrb_value
qimage_bits(mrb_state* M, mrb_value self)
{
  smokeruby_object *o = value_obj_info(M, self);
  QImage * image = static_cast<QImage *>(o->ptr);
  const uchar * bytes = image->bits();
  return mrb_str_new(M, (const char *) bytes, image->numBytes());
}

static mrb_value
qimage_scan_line(mrb_state* M, mrb_value self)
{
  mrb_int ix;
  mrb_get_args(M, "i", &ix);
  smokeruby_object *o = value_obj_info(M, self);
  QImage * image = static_cast<QImage *>(o->ptr);
  const uchar * bytes = image->scanLine(ix);
  return mrb_str_new(M, (const char *) bytes, image->bytesPerLine());
}

#ifdef QT_QTDBUS
static mrb_value
qdbusargument_endarraywrite(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
	QDBusArgument * arg = (QDBusArgument *) o->ptr;
	arg->endArray();
	return self;
}

static mrb_value
qdbusargument_endmapwrite(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
	QDBusArgument * arg = (QDBusArgument *) o->ptr;
	arg->endMap();
	return self;
}

static mrb_value
qdbusargument_endmapentrywrite(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
	QDBusArgument * arg = (QDBusArgument *) o->ptr;
	arg->endMapEntry();
	return self;
}

static mrb_value
qdbusargument_endstructurewrite(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
	QDBusArgument * arg = (QDBusArgument *) o->ptr;
	arg->endStructure();
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
			return mrb_call_super(M, self, argc, argv);
		}

		QtRuby::MethodCall c(M, qtcore_Smoke, _current_method.index, self, argv, argc-1);
		c.next();
		return self;
	}

	return mrb_call_super(M, self, argc, argv);
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

		if (qstrcmp(o->smoke->classes[o->classId].className, "QRectF") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_rectf_vector;
		} else if (qstrcmp(o->smoke->classes[o->classId].className, "QRect") == 0) {
			_current_method.smoke = qtcore_Smoke;
			_current_method.index = drawlines_rect_vector;
		} else {
			return mrb_call_super(M, self, argc, argv);
		}

		QtRuby::MethodCall c(M, qtcore_Smoke, _current_method.index, self, argv, argc-1);
		c.next();
		return self;
	}

	return mrb_call_super(M, self, argc, argv);
}

static mrb_value
qabstractitemmodel_createindex(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	if (argc == 2 || argc == 3) {
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
				stack[1].s_int = mrb_fixnum(argv[0]);
				stack[2].s_int = mrb_fixnum(argv[1]);
				if (argc == 2) {
					stack[3].s_voidp = NULL;
				} else {
					stack[3].s_voidp = mrb_voidp(argv[2]);
				}
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

	return mrb_call_super(M, self, argc, argv);
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
    smokeruby_object *o = value_obj_info(M, self);
	QItemSelection * item = (QItemSelection *) o->ptr;
	return mrb_fixnum_value(item->count());
}

static mrb_value
metaObject(mrb_state* M, mrb_value self)
{
  return mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "getMetaObject", 2, mrb_nil_value(), self);
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

    Smoke::ModuleIndex * cast_to_id = classcache.value(mrb_string_value_ptr(M, new_klassname));
	if (cast_to_id == 0) {
		mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "unable to find class \"%S\" to cast to\n", mrb_string_value_ptr(M, new_klassname));
	}

	smokeruby_object * o_cast = alloc_smokeruby_object(	M, o->allocated,
														cast_to_id->smoke,
														(int) cast_to_id->index,
														o->smoke->cast(o->ptr, o->classId, (int) cast_to_id->index) );

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
	Smoke::ModuleIndex * mi = classcache.value(classname);
	if (mi == 0) {
		return mrb_nil_value();
	}

	QObject* qobj = (QObject*) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
	if (qobj == 0) {
		return mrb_nil_value();
	}

	void* ret = qobj->qt_metacast(mi->smoke->classes[mi->index].className);

	if (ret == 0) {
		return mrb_nil_value();
	}

	smokeruby_object * o_cast = alloc_smokeruby_object(	M, o->allocated,
														mi->smoke,
														(int) mi->index,
														ret );

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
				_current_method.smoke = meth.smoke;
				_current_method.index = meth.smoke->ambiguousMethodList[i];
				QtRuby::MethodCall c(M, meth.smoke, _current_method.index, self, argv, 1);
				c.next();
				return *(c.var());
			}

			i++;
		}
	}

	return mrb_call_super(M, self, argc, argv);
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
				_current_method.smoke = meth.smoke;
				_current_method.index = meth.smoke->ambiguousMethodList[i];
				QtRuby::MethodCall c(M, meth.smoke, _current_method.index, self, argv, 2);
				c.next();
				return *(c.var());
			}

			i++;
		}
	}

	return mrb_call_super(M, self, argc, argv);
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
    Smoke::ModuleIndex * value_class_id = classcache.value(classname);
	if (value_class_id == 0) {
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

	vo = alloc_smokeruby_object(M, true, value_class_id->smoke, value_class_id->index, value_ptr);
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
				_current_method.smoke = meth.smoke;
				_current_method.index = meth.smoke->ambiguousMethodList[i];
				QtRuby::MethodCall c(M, meth.smoke, _current_method.index, self, argv, 0);
				c.next();
				return *(c.var());
			}

			i++;
		}

		if(do_debug & qtdb_gc) printf("No suitable method for signature QVariant::QVariant(%s) found - looking for another suitable constructor\n", mrb_string_value_ptr(M, argv[1]));
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
		Smoke::ModuleIndex nameId = qtcore_Smoke->findMethodName("Qvariant", "QVariant?");
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
		_current_method.smoke = qtcore_Smoke;
		_current_method.index = new_qvariant_qmap;
		QtRuby::MethodCall c(M, qtcore_Smoke, _current_method.index, self, argv, argc-1);
		c.next();
    	return *(c.var());
	} else if (	argc == 1
				&& mrb_type(argv[0]) == MRB_TT_ARRAY
				&& RARRAY_LEN(argv[0]) > 0
				&& mrb_type(mrb_ary_entry(argv[0], 0)) != MRB_TT_STRING )
	{
		_current_method.smoke = qtcore_Smoke;
		_current_method.index = new_qvariant_qlist;
		QtRuby::MethodCall c(M, qtcore_Smoke, _current_method.index, self, argv, argc-1);
		c.next();
		return *(c.var());
	}

	return mrb_call_super(M, self, argc, argv);
}

  static mrb_value module_method_missing(mrb_state* M, mrb_value klass)
{
   return class_method_missing(M, klass);
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

	mrb_value retval = mrb_nil_value();
	mrb_value temp_obj;

	RClass* klass = mrb_class_ptr(mrb_funcall(M, self, "class", 0));
	mrb_value constructor_name = mrb_str_new_cstr(M, "new");

	mrb_value * temp_stack = (mrb_value*)mrb_malloc(M, sizeof(mrb_value) * (argc+4));

	temp_stack[0] = mrb_str_new_cstr(M, "Qt");
	temp_stack[1] = constructor_name;
	temp_stack[2] = mrb_obj_value(klass);
	temp_stack[3] = self;

	for (int count = 0; count < argc; count++) {
		temp_stack[count+4] = argv[count];
	}

	{
		QByteArray * mcid = find_cached_selector(M, argc+4, temp_stack, mrb_obj_value(klass), mrb_string_value_ptr(M, mrb_class_path(M, klass)));

		if (_current_method.index == -1) {
			retval = mrb_funcall_argv(M, mrb_obj_value(qt_internal_module(M)), mrb_intern_lit(M, "do_method_missing"), argc+4, temp_stack);
			if (_current_method.index != -1) {
				// Success. Cache result.
				methcache.insert(*mcid, new Smoke::ModuleIndex(_current_method));
			}
		}
	}

	if (_current_method.index == -1) {
		// Another longjmp here..
		mrb_raisef(M, mrb_class_get(M, "ArgumentError"), "unresolved constructor call %S\n", mrb_class_path(M, klass));
	}

  DATA_TYPE(self) = &smokeruby_type;

	{
		// Allocate the MethodCall within a C block. Otherwise, because the continue_new_instance()
		// call below will longjmp out, it wouldn't give C++ an opportunity to clean up
		QtRuby::MethodCall c(M, _current_method.smoke, _current_method.index, self, temp_stack+4, argc);
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

	// Off with a longjmp, never to return..
	// TODO: mrb_throw("newqt", result);
	/*NOTREACHED*/
	return self;
}

  /*
mrb_value
new_qt(mrb_state* M, mrb_value klass)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);
  mrb_value * temp_stack = (mrb_value*)mrb_malloc(M, sizeof(mrb_value) * (argc + 1));
	temp_stack[0] = mrb_obj_value(mrb_obj_alloc(M, MRB_TT_DATA, mrb_class_ptr(klass)));

	for (int count = 0; count < argc; count++) {
		temp_stack[count+1] = argv[count];
	}

	mrb_value result = mrb_funcall_argv(M, mrb_obj_value(qt_internal_module(M)), mrb_intern_lit(M, "try_initialize"), argc+1, temp_stack);
  mrb_funcall_argv(M, result, mrb_intern_lit(M, "initialize"), argc, argv);

	return result;
}
*/


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

    QLatin1String signalname(mrb_sym2name(M, M->c->ci->mid));

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

static mrb_value
qt_metacall(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv;
  mrb_get_args(M, "*", &argv, &argc);

	// Arguments: QMetaObject::Call _c, int id, void ** _o
	QMetaObject::Call _c = (QMetaObject::Call) mrb_fixnum(	mrb_funcall(M, mrb_obj_value(qt_internal_module(M)),
                                                                      "get_qinteger",
                                                                      1, argv[0] ) );
	int id = mrb_fixnum(argv[1]);
	void ** _o = 0;

	// Note that for a slot with no args and no return type,
	// it isn't an error to get a NULL value of _o here.
	Data_Get_Struct(M, argv[2], &smokeruby_type, _o);
	// Assume the target slot is a C++ one
	smokeruby_object *o = value_obj_info(M, self);
	Smoke::ModuleIndex nameId = o->smoke->idMethodName("qt_metacall$$?");
	Smoke::ModuleIndex classIdx(o->smoke, o->classId);
	Smoke::ModuleIndex meth = nameId.smoke->findMethod(classIdx, nameId);
	if (meth.index > 0) {
		const Smoke::Method &m = meth.smoke->methods[meth.smoke->methodMaps[meth.index].method];
		Smoke::ClassFn fn = meth.smoke->classes[m.classId].classFn;
		Smoke::StackItem i[4];
		i[1].s_enum = _c;
		i[2].s_int = id;
		i[3].s_voidp = _o;
		(*fn)(m.method, o->ptr, i);
		int ret = i[0].s_int;
		if (ret < 0) {
			return mrb_fixnum_value(ret);
		}
	} else {
		// Should never happen..
		mrb_raisef(M, mrb_class_get(M, "RuntimeError"), "Cannot find %S::qt_metacall() method\n",
               mrb_str_new_cstr(M, o->smoke->classes[o->classId].className));
	}

    if (_c != QMetaObject::InvokeMetaMethod) {
		return argv[1];
	}

	QObject * qobj = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject").index);
	// get obj metaobject with a virtual call
	const QMetaObject *metaobject = qobj->metaObject();

	// get method/property count
	int count = 0;
	if (_c == QMetaObject::InvokeMetaMethod) {
		count = metaobject->methodCount();
	} else {
		count = metaobject->propertyCount();
	}

	if (_c == QMetaObject::InvokeMetaMethod) {
		QMetaMethod method = metaobject->method(id);

		if (method.methodType() == QMetaMethod::Signal) {
			metaobject->activate(qobj, id, (void**) _o);
			return mrb_fixnum_value(id - count);
		}

		QList<MocArgument*> mocArgs = get_moc_arguments(M, o->smoke, method.typeName(), method.parameterTypes());

		QString name(method.signature());
static QRegExp * rx = 0;
		if (rx == 0) {
			rx = new QRegExp("\\(.*");
		}
		name.replace(*rx, "");
		QtRuby::InvokeSlot slot(M, self, mrb_intern_cstr(M, name.toLatin1()), mocArgs, _o);
		slot.next();
	}

	return mrb_fixnum_value(id - count);
}

static mrb_value
qobject_connect(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv; mrb_value blk;
  mrb_get_args(M, "&*", &blk, &argv, &argc);

	if (not mrb_nil_p(blk)) {
		if (argc == 1) {
			return mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "signal_connect", 3, self, argv[0], blk);
		} else if (argc == 2) {
			return mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "connect", 4, argv[0], argv[1], self, blk);
		} else if (argc == 3) {
			return mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "connect", 4, argv[0], argv[1], argv[2], blk);
		} else {
			mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
		}
	} else {
		if (argc == 3 && mrb_type(argv[1]) != MRB_TT_STRING) {
			return mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "method_connect", 4, self, argv[0], argv[1], argv[2]);
		} else {
			return mrb_call_super(M, self, argc, argv);
		}
	}

  return mrb_nil_value();
}

static mrb_value
qtimer_single_shot(mrb_state* M, mrb_value self)
{
  int argc; mrb_value* argv; mrb_value blk;
  mrb_get_args(M, "&*", &blk, &argv, &argc);

	if (not mrb_nil_p(blk)) {
		if (argc == 2) {
			return mrb_funcall(M, mrb_obj_value(qt_internal_module(M)), "single_shot_timer_connect", 3, argv[0], argv[1], mrb_block_proc());
		} else {
			mrb_raise(M, mrb_class_get(M, "ArgumentError"), "Invalid argument list");
		}
	} else {
		return mrb_call_super(M, self, argc, argv);
	}

  return mrb_nil_value();
}

// --------------- Ruby C functions for Qt::_internal.* helpers  ----------------


static mrb_value
getMethStat(mrb_state* M, mrb_value /*self*/)
{
    mrb_value result_list = mrb_ary_new(M);
    mrb_ary_push(M, result_list, mrb_fixnum_value((int)methcache.size()));
    mrb_ary_push(M, result_list, mrb_fixnum_value((int)methcache.count()));
    return result_list;
}

static mrb_value
getClassStat(mrb_state* M, mrb_value /*self*/)
{
    mrb_value result_list = mrb_ary_new(M);
    mrb_ary_push(M, result_list, mrb_fixnum_value((int)classcache.size()));
    mrb_ary_push(M, result_list, mrb_fixnum_value((int)classcache.count()));
    return result_list;
}

static mrb_value
getIsa(mrb_state* M, mrb_value /*self*/)
{
  mrb_value classId;
  mrb_get_args(M, "o", &classId);
    mrb_value parents_list = mrb_ary_new(M);

    int id = mrb_fixnum(mrb_funcall(M, classId, "index", 0));
    Smoke* smoke = smokeList[mrb_fixnum(mrb_funcall(M, classId, "smoke", 0))];

    Smoke::Index *parents =
	smoke->inheritanceList +
	smoke->classes[id].parents;

    while(*parents) {
	//logger("\tparent: %s", qtcore_Smoke->classes[*parents].className);
      mrb_ary_push(M, parents_list, mrb_str_new_cstr(M, smoke->classes[*parents++].className));
    }
    return parents_list;
}

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
		return mrb_call_super(M, self, argc, argv);
	}

	Smoke::ModuleIndex * classId = classcache.value(mrb_string_value_ptr(M, argv[0]));

	if (classId == 0) {
		return mrb_call_super(M, self, argc, argv);
	} else {
		mrb_value super_class = mrb_str_new_cstr(M, classId->smoke->classes[classId->index].className);
		return mrb_call_super(M, self, argc, &super_class);
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
  mrb_value on_value;
  mrb_get_args(M, "o", &on_value);
    int on = mrb_fixnum(on_value);
    do_debug = on;
    return self;
}

static mrb_value
debugging(mrb_state*, mrb_value /*self*/)
{
    return mrb_fixnum_value(do_debug);
}

static mrb_value
get_arg_type_name(mrb_state* M, mrb_value /*self*/)
{
  mrb_value method_value, idx_value;
  mrb_get_args(M, "oo", &method_value, &idx_value);
  int method = mrb_fixnum(mrb_funcall(M, method_value, "index", 0));
  int smokeIndex = mrb_fixnum(mrb_funcall(M, method_value, "smoke", 0));
    Smoke * smoke = smokeList[smokeIndex];
    int idx = mrb_fixnum(idx_value);
    const Smoke::Method &m = smoke->methods[method];
    Smoke::Index *args = smoke->argumentList + m.args;
    return mrb_str_new_cstr(M, (char*)smoke->types[args[idx]].name);
}

static mrb_value
classIsa(mrb_state* M, mrb_value /*self*/)
{
  char* className; char* base;
  mrb_get_args(M, "zz", &className, &base);
  return mrb_bool_value(Smoke::isDerivedFrom(className, base));
}

static mrb_value
isEnum(mrb_state* M, mrb_value /*self*/)
{
  mrb_value enumName_value;
  mrb_get_args(M, "o", &enumName_value);
    char *enumName = mrb_string_value_ptr(M, enumName_value);
    Smoke::Index typeId = 0;
    Smoke* s = 0;
    for (int i = 0; i < smokeList.count(); i++) {
         typeId = smokeList[i]->idType(enumName);
         if (typeId > 0) {
             s = smokeList[i];
             break;
         }
    }
    return	mrb_bool_value(typeId > 0
			&& (	(s->types[typeId].flags & Smoke::tf_elem) == Smoke::t_enum
					|| (s->types[typeId].flags & Smoke::tf_elem) == Smoke::t_ulong
					|| (s->types[typeId].flags & Smoke::tf_elem) == Smoke::t_long
					|| (s->types[typeId].flags & Smoke::tf_elem) == Smoke::t_uint
            || (s->types[typeId].flags & Smoke::tf_elem) == Smoke::t_int ));
}

static mrb_value
insert_pclassid(mrb_state* M, mrb_value self)
{
  mrb_value p_value, mi_value;
  mrb_get_args(M, "oo", &p_value, &mi_value);
    char *p = mrb_string_value_ptr(M, p_value);
    int ix = mrb_fixnum(mrb_funcall(M, mi_value, "index", 0));
    int smokeidx = mrb_fixnum(mrb_funcall(M, mi_value, "smoke", 0));
    Smoke::ModuleIndex mi(smokeList[smokeidx], ix);
    classcache.insert(QByteArray(p), new Smoke::ModuleIndex(mi));
    IdToClassNameMap.insert(mi, new QByteArray(p));
    return self;
}

static mrb_value
classid2name(mrb_state* M, mrb_value /*self*/)
{
  mrb_value mi_value;
  mrb_get_args(M, "o", &mi_value);
  int ix = mrb_fixnum(mrb_funcall(M, mi_value, "index", 0));
  int smokeidx = mrb_fixnum(mrb_funcall(M, mi_value, "smoke", 0));
    Smoke::ModuleIndex mi(smokeList[smokeidx], ix);
    return mrb_str_new_cstr(M, IdToClassNameMap[mi]->constData());
}

static mrb_value
find_pclassid(mrb_state* M, mrb_value /*self*/)
{
  mrb_value p_value;
  mrb_get_args(M, "o", &p_value);
    if (mrb_nil_p(p_value)) {
      return mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, 0, 0);
    }

    char *p = mrb_string_value_ptr(M, p_value);
    Smoke::ModuleIndex *r = classcache.value(QByteArray(p));
    if (r != 0) {
      return mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(r->smoke)), mrb_fixnum_value(r->index));
    } else {
      return mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_nil_value(), mrb_nil_value());
    }
}

static mrb_value
get_value_type(mrb_state* M, mrb_value /*self*/)
{
  mrb_value ruby_value;
  mrb_get_args(M, "o", &ruby_value);
  return mrb_str_new_cstr(M, value_to_type_flag(M, ruby_value));
}

static QMetaObject*
parent_meta_object(mrb_state* M, mrb_value obj)
{
	smokeruby_object* o = value_obj_info(M, obj);
	Smoke::ModuleIndex nameId = o->smoke->idMethodName("metaObject");
	Smoke::ModuleIndex classIdx(o->smoke, o->classId);
	Smoke::ModuleIndex meth = o->smoke->findMethod(classIdx, nameId);
	if (meth.index <= 0) {
		// Should never happen..
	}

	const Smoke::Method &methodId = meth.smoke->methods[meth.smoke->methodMaps[meth.index].method];
	Smoke::ClassFn fn = o->smoke->classes[methodId.classId].classFn;
	Smoke::StackItem i[1];
	(*fn)(methodId.method, o->ptr, i);
	return (QMetaObject*) i[0].s_voidp;
}

static mrb_value
make_metaObject(mrb_state* M, mrb_value /*self*/)
{
  mrb_value obj, parentMeta, stringdata_value, data_value;
  mrb_get_args(M, "oooo", &obj, &parentMeta, &stringdata_value, &data_value);
	QMetaObject* superdata = 0;

	if (mrb_nil_p(parentMeta)) {
		// The parent class is a Smoke class, so call metaObject() on the
		// instance to get it via a smoke library call
		superdata = parent_meta_object(M, obj);
	} else {
		// The parent class is a custom Ruby class whose metaObject
		// was constructed at runtime
		smokeruby_object* p = value_obj_info(M, parentMeta);
		superdata = (QMetaObject *) p->ptr;
	}

	char *stringdata = new char[RSTRING_LEN(stringdata_value)];

	int count = RARRAY_LEN(data_value);
	uint * data = new uint[count];

	memcpy(	(void *) stringdata, RSTRING_PTR(stringdata_value), RSTRING_LEN(stringdata_value) );

	for (long i = 0; i < count; i++) {
		mrb_value rv = mrb_ary_entry(data_value, i);
		data[i] = mrb_fixnum(rv);
	}

	QMetaObject ob = {
		{ superdata, stringdata, data, 0 }
	} ;

	QMetaObject * meta = new QMetaObject;
	*meta = ob;

#ifdef DEBUG
	printf("make_metaObject() superdata: %p %s\n", meta->d.superdata, superdata->className());

	printf(
	" // content:\n"
	"       %d,       // revision\n"
	"       %d,       // classname\n"
	"       %d,   %d, // classinfo\n"
	"       %d,   %d, // methods\n"
	"       %d,   %d, // properties\n"
	"       %d,   %d, // enums/sets\n",
	data[0], data[1], data[2], data[3],
	data[4], data[5], data[6], data[7], data[8], data[9]);

	int s = data[3];

	if (data[2] > 0) {
		printf("\n // classinfo: key, value\n");
		for (uint j = 0; j < data[2]; j++) {
			printf("      %d,    %d\n", data[s + (j * 2)], data[s + (j * 2) + 1]);
		}
	}

	s = data[5];
	bool signal_headings = true;
	bool slot_headings = true;

	for (uint j = 0; j < data[4]; j++) {
		if (signal_headings && (data[s + (j * 5) + 4] & 0x04) != 0) {
			printf("\n // signals: signature, parameters, type, tag, flags\n");
			signal_headings = false;
		}

		if (slot_headings && (data[s + (j * 5) + 4] & 0x08) != 0) {
			printf("\n // slots: signature, parameters, type, tag, flags\n");
			slot_headings = false;
		}

		printf("      %d,   %d,   %d,   %d, 0x%2.2x\n",
			data[s + (j * 5)], data[s + (j * 5) + 1], data[s + (j * 5) + 2],
			data[s + (j * 5) + 3], data[s + (j * 5) + 4]);
	}

	s += (data[4] * 5);
	for (uint j = 0; j < data[6]; j++) {
		printf("\n // properties: name, type, flags\n");
		printf("      %d,   %d,   0x%8.8x\n",
			data[s + (j * 3)], data[s + (j * 3) + 1], data[s + (j * 3) + 2]);
	}

	s += (data[6] * 3);
	for (int i = s; i < count; i++) {
		printf("\n       %d        // eod\n", data[i]);
	}

	printf("\nqt_meta_stringdata:\n    \"");

    int strlength = 0;
	for (int j = 0; j < RSTRING_LEN(stringdata_value); j++) {
        strlength++;
		if (meta->d.stringdata[j] == 0) {
			printf("\\0");
			if (strlength > 40) {
				printf("\"\n    \"");
				strlength = 0;
			}
		} else {
			printf("%c", meta->d.stringdata[j]);
		}
	}
	printf("\"\n\n");

#endif
	smokeruby_object  * m = alloc_smokeruby_object(	M, true,
													qtcore_Smoke,
													qtcore_Smoke->idClass("QMetaObject").index,
													meta );

  return mrb_obj_value(Data_Wrap_Struct(M, qmetaobject_class(M), &smokeruby_type, m));
}

static mrb_value
add_metaobject_methods(mrb_state* M, mrb_value self)
{
  mrb_value klass;
  mrb_get_args(M, "o", &klass);
	mrb_define_method(M, mrb_class_ptr(klass), "qt_metacall", qt_metacall, MRB_ARGS_ANY());
	mrb_define_method(M, mrb_class_ptr(klass), "metaObject", metaObject, MRB_ARGS_NONE());
	return self;
}

static mrb_value
add_signal_methods(mrb_state* M, mrb_value self)
{
  mrb_value klass, signalNames;
  mrb_get_args(M, "oo", &klass, &signalNames);
	for (long index = 0; index < RARRAY_LEN(signalNames); index++) {
		mrb_value signal = mrb_ary_entry(signalNames, index);
		mrb_define_method(M, mrb_class_ptr(klass), mrb_string_value_ptr(M, signal), qt_signal, MRB_ARGS_ANY());
	}
	return self;
}

static mrb_value
dispose(mrb_state* M, mrb_value self)
{
    smokeruby_object *o = value_obj_info(M, self);
    if (o == 0 || o->ptr == 0) { return mrb_nil_value(); }

    const char *className = o->smoke->classes[o->classId].className;
	if(do_debug & qtdb_gc) printf("Deleting (%s*)%p\n", className, o->ptr);

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

mrb_value
isQObject(mrb_state* M, mrb_value /*self*/)
{
  mrb_value c;
  mrb_get_args(M, "o", &c);
  return mrb_bool_value(Smoke::isDerivedFrom(mrb_string_value_ptr(M, c), "QObject"));
}

// Returns the Smoke classId of a ruby instance
static mrb_value
idInstance(mrb_state* M, mrb_value /*self*/)
{
  mrb_value instance;
  mrb_get_args(M, "o", &instance);
    smokeruby_object *o = value_obj_info(M, instance);
    if(!o)
      return mrb_nil_value();

    return mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(o->smoke)), mrb_fixnum_value(o->classId));
}

static mrb_value
findClass(mrb_state* M, mrb_value /*self*/)
{
  mrb_value name_value;
  mrb_get_args(M, "o", &name_value);
    char *name = mrb_string_value_ptr(M, name_value);
    Smoke::ModuleIndex mi = Smoke::findClass(name);
    return mrb_funcall(M, mrb_obj_value(moduleindex_class(M)), "new", 2, mrb_fixnum_value(smokeList.indexOf(mi.smoke)), mrb_fixnum_value(mi.index));
}

// static mrb_value
// idMethodName(mrb_value /*self*/, mrb_value name_value)
// {
//     char *name = mrb_string_value_ptr(M, name_value);
//     return mrb_fixnum_value(qtcore_Smoke->idMethodName(name).index);
// }
//
// static mrb_value
// idMethod(mrb_value /*self*/, mrb_value idclass_value, mrb_value idmethodname_value)
// {
//     int idclass = mrb_fixnum(idclass_value);
//     int idmethodname = mrb_fixnum(idmethodname_value);
//     return mrb_fixnum_value(qtcore_Smoke->idMethod(idclass, idmethodname).index);
// }

static mrb_value
dumpCandidates(mrb_state* M, mrb_value /*self*/)
{
  mrb_value rmeths;
  mrb_get_args(M, "o", &rmeths);

  mrb_value errmsg = mrb_str_new_cstr(M, "");
  if(not mrb_nil_p(rmeths)) {
	int count = RARRAY_LEN(rmeths);
        for(int i = 0; i < count; i++) {
          rb_str_catf(M, errmsg, "\t");
	    int id = mrb_fixnum(mrb_funcall(M, mrb_ary_entry(rmeths, i), "index", 0));
	    Smoke* smoke = smokeList[mrb_fixnum(mrb_funcall(M, mrb_ary_entry(rmeths, i), "smoke", 0))];
	    const Smoke::Method &meth = smoke->methods[id];
	    const char *tname = smoke->types[meth.ret].name;
	    if(meth.flags & Smoke::mf_enum) {
        rb_str_catf(M, errmsg, "enum ");
        rb_str_catf(M, errmsg, "%s::%s", smoke->classes[meth.classId].className, smoke->methodNames[meth.name]);
        rb_str_catf(M, errmsg, "\n");
	    } else {
        if(meth.flags & Smoke::mf_static) rb_str_catf(M, errmsg, "static ");
			rb_str_catf(M, errmsg, "%s ", (tname ? tname:"void"));
			rb_str_catf(M, errmsg, "%s::%s(", smoke->classes[meth.classId].className, smoke->methodNames[meth.name]);
			for(int i = 0; i < meth.numArgs; i++) {
        if(i) rb_str_catf(M, errmsg, ", ");
			tname = smoke->types[smoke->argumentList[meth.args+i]].name;
			rb_str_catf(M, errmsg, "%s", (tname ? tname:"void"));
			}
			rb_str_catf(M, errmsg, ")");
			if(meth.flags & Smoke::mf_const) rb_str_catf(M, errmsg, " const");
			rb_str_catf(M, errmsg, "\n");
        	}
        }
    }
    return errmsg;
}

static mrb_value
isConstMethod(mrb_state* M, mrb_value /*self*/)
{
  mrb_value idx;
  mrb_get_args(M, "o", &idx);
	int id = mrb_fixnum(mrb_funcall(M, idx, "index", 0));
	Smoke* smoke = smokeList[mrb_fixnum(mrb_funcall(M, idx, "smoke", 0))];
	const Smoke::Method &meth = smoke->methods[id];
	return mrb_bool_value(meth.flags & Smoke::mf_const);
}

static mrb_value
isObject(mrb_state* M, mrb_value /*self*/)
{
  mrb_value obj;
  mrb_get_args(M, "o", &obj);
    void * ptr = 0;
    ptr = value_to_ptr(M, obj);
    return mrb_bool_value(ptr > 0);
}

static mrb_value
setCurrentMethod(mrb_state* M, mrb_value self)
{
  mrb_value meth_value;
  mrb_get_args(M, "o", &meth_value);
  int smokeidx = mrb_fixnum(mrb_funcall(M, meth_value, "smoke", 0));
  int meth = mrb_fixnum(mrb_funcall(M, meth_value, "index", 0));
    // FIXME: damn, this is lame, and it doesn't handle ambiguous methods
    _current_method.smoke = smokeList[smokeidx];  //qtcore_Smoke->methodMaps[meth].method;
    _current_method.index = meth;
    return self;
}

static mrb_value
getClassList(mrb_state* M, mrb_value /*self*/)
{
    mrb_value class_list = mrb_ary_new(M);

    for (int i = 1; i <= qtcore_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtcore_Smoke->classes[i].className && !qtcore_Smoke->classes[i].external)
          mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtcore_Smoke->classes[i].className));
    }

    for (int i = 1; i <= qtgui_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtgui_Smoke->classes[i].className && !qtgui_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtgui_Smoke->classes[i].className));
    }

    for (int i = 1; i <= qtxml_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtxml_Smoke->classes[i].className && !qtxml_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtxml_Smoke->classes[i].className));
    }

    for (int i = 1; i <= qtsql_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtsql_Smoke->classes[i].className && !qtsql_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtsql_Smoke->classes[i].className));
    }

    for (int i = 1; i <= qtopengl_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtopengl_Smoke->classes[i].className && !qtopengl_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtopengl_Smoke->classes[i].className));
    }

    for (int i = 1; i <= qtnetwork_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtnetwork_Smoke->classes[i].className && !qtnetwork_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtnetwork_Smoke->classes[i].className));
    }

    for (int i = 1; i <= qtsvg_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtsvg_Smoke->classes[i].className && !qtsvg_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtsvg_Smoke->classes[i].className));
    }

#ifdef QT_QTDBUS
    for (int i = 1; i <= qtdbus_Smoke->numClasses; i++) {
      PROTECT_SCOPE();
        if (qtdbus_Smoke->classes[i].className && !qtdbus_Smoke->classes[i].external)
            mrb_ary_push(M, class_list, mrb_str_new_cstr(M, qtdbus_Smoke->classes[i].className));
    }
#endif

    return class_list;
}

static mrb_value
create_qobject_class(mrb_state* M, mrb_value /*self*/)
{
  mrb_value package_value, module_value;
  mrb_get_args(M, "oo", &package_value, &module_value);
	const char *package = strdup(mrb_string_value_ptr(M, package_value));
	// this won't work:
	// strdup(mrb_string_value_ptr(M, rb_funcall(module_value, mrb_intern("name"), 0)))
	// any ideas why?
	mrb_value value_moduleName = mrb_class_path(M, mrb_class_ptr(module_value));
	const char *moduleName = strdup(mrb_string_value_ptr(M, value_moduleName));
	RClass* klass = mrb_class_ptr(module_value);

	QString packageName(package);

	foreach(QString s, packageName.mid(strlen(moduleName) + 2).split("::")) {
		klass = mrb_define_class_under(M, klass, (const char*) s.toLatin1(), qt_base_class(M));
    MRB_SET_INSTANCE_TT(klass, MRB_TT_DATA);
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

	free((void *) package);
	return mrb_obj_value(klass);
}

static mrb_value
create_qt_class(mrb_state* M, mrb_value /*self*/)
{
  mrb_value package_value, module_value;
  mrb_get_args(M, "oo", &package_value, &module_value);
	const char *package = strdup(mrb_string_value_ptr(M, package_value));
	// this won't work:
	// strdup(mrb_string_value_ptr(M, rb_funcall(module_value, mrb_intern("name"), 0)))
	// any ideas why?
	mrb_value value_moduleName = mrb_class_path(M, mrb_class_ptr(module_value));
	const char *moduleName = strdup(mrb_string_value_ptr(M, value_moduleName));
	RClass* klass = mrb_class_ptr(module_value);
	QString packageName(package);

/*
    mrb_define_singleton_method(module_value, "method_missing", (mrb_value (*) (...)) module_method_missing, -1);
    mrb_define_singleton_method(module_value, "const_missing", (mrb_value (*) (...)) module_method_missing, -1);
*/
	foreach(QString s, packageName.mid(strlen(moduleName) + 2).split("::")) {
		klass = mrb_define_class_under(M, klass, (const char*) s.toLatin1(), qt_base_class(M));
    MRB_SET_INSTANCE_TT(klass, MRB_TT_DATA);
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

	free((void *) package);
	return mrb_obj_value(klass);
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

static mrb_value
normalize_classname_default(mrb_state* M, mrb_value)
{
  char* str; int str_len;
  mrb_get_args(M, "s", &str, &str_len);

  std::string ret(str, str_len);

  if(str_len >= 2 and str[0] == 'Q' and str[1] == '3') {
    static std::regex const qt3_regexp("^Q3(\\w*)");
    ret = std::regex_replace(ret, qt3_regexp, "Qt3::$1");
  } else if(str_len >= 1 and str[0] == 'Q') {
    static std::regex const qt_regexp("^Q(\\w+)");
    ret = std::regex_replace(ret, qt_regexp, "Qt::$1");
  }
  return mrb_str_new(M, ret.data(), ret.size());
}

static int
checkarg_int(mrb_state* M, mrb_value self) {
  char* argtype_str; int argtype_len; char* type_name_str; int type_name_len;
  mrb_get_args(M, "ss", &argtype_str, &argtype_len, &type_name_str, &type_name_len);

  std::string const argtype(argtype_str, argtype_len), type_name(type_name_str, type_name_len);

  typedef std::regex r;

  static r const is_const_reg("^const\\s+/"), remove_qualifier("^const\\s+(.*)[&*]$/");
  int const const_point = std::regex_match(type_name, is_const_reg)? -1 : 0;

  if(argtype_len == 1) {
    switch(argtype[0]) {
      case 'i': {
        if(std::regex_match(type_name, r("^int&?$|^signed int&?$|^signed$|^qint32&?$")))
        { return 6 + const_point; }
        if(std::regex_match(type_name, r("^quint32&?$"))
           or std::regex_match(type_name, r("^(?:short|ushort|unsigned short int|unsigned short|uchar|char|unsigned char|uint|long|ulong|unsigned long int|unsigned|float|double|WId|HBITMAP__\\*|HDC__\\*|HFONT__\\*|HICON__\\*|HINSTANCE__\\*|HPALETTE__\\*|HRGN__\\*|HWND__\\*|Q_PID|^quint16&?$|^qint16&?$)$"))
           or std::regex_match(type_name, r("^(quint|qint|qulong|qlong|qreal)")))
        { return 4 + const_point; }

        std::string const t = std::regex_replace(type_name, remove_qualifier, "$1");
        if(mrb_test(mrb_funcall(M, self, "isEnum", 1, mrb_str_new(M, t.data(), t.size()))))
        { return 2; }
      } break;

      case 'n': {
        if(std::regex_match(type_name, r("^double$|^qreal$")))
        { return 6 + const_point; }
        if(std::regex_match(type_name, r("^float$")))
        { return 4 + const_point; }
        if(std::regex_match(type_name, r("^double$|^qreal$"))
           or std::regex_match(type_name, r("^(?:short|ushort|uint|long|ulong|signed|unsigned|float|double)$")))
        { return 2 + const_point; }

        std::string const t = std::regex_replace(type_name, remove_qualifier, "$1");
        if(mrb_test(mrb_funcall(M, self, "isEnum", 1, mrb_str_new(M, t.data(), t.size()))))
        { return 2 + const_point; }
      } break;

      case 'B': {
        if(std::regex_match(type_name, r("^(?:bool)[*&]?$")))
        { return 2 + const_point; }
      } break;

      case 's': {
        if(std::regex_match(type_name, r("^(?:(?:const )?(QString)[*&]?)$")))
        { return 8 + const_point; }
        if(std::regex_match(type_name, r("^(const )?((QChar)[*&]?)$")))
        { return 6 + const_point; }
        if(std::regex_match(type_name, r("^(?:(u(nsigned )?)?char\\*)$")))
        { return 4 + const_point; }
        if(std::regex_match(type_name, r("^(?:const (u(nsigned )?)?char\\*)$")))
        { return 2 + const_point; }
      } break;

      case 'a': {
        if(std::regex_match(type_name, r("^(?:const QCOORD\\*|(?:const )?(?:QStringList[\\*&]?|QValueList<int>[\\*&]?|QRgb\\*|char\\*\\*))$)")))
        { return 2 + const_point; }
      } break;

      case 'u': {
        if(std::regex_match(type_name, r("^(?:u?char\\*|const u?char\\*|(?:const )?((Q(C?)String))[*&]?)$")))
        { return 4 + const_point; }
        if(std::regex_match(type_name, r("^(?:short|ushort|uint|long|ulong|signed|unsigned|int)$")))
        { return -99; }
        else
        { return 2 + const_point; }
      } break;

      case 'U': {
        return std::regex_match(type_name, r("QStringList"))? 4 + const_point : 2 + const_point;
      } break;
    }
  }

  std::string const t = std::regex_replace(std::regex_replace(type_name, remove_qualifier, ""), r("(::)?Ptr$"), "");
  if(argtype == t) { return 4 + const_point; }
  if(mrb_test(mrb_funcall(M, self, "classIsa", 2, mrb_str_new(M, argtype_str, argtype_len), mrb_str_new(M, t.data(), t.size()))))
  { return 2 + const_point; }
  if(mrb_test(mrb_funcall(M, self, "isEnum", 1, mrb_str_new(M, argtype_str, argtype_len)))
     and (std::regex_match(t, r("int|qint32|uint|quint32|long|ulong"))
          or mrb_test(mrb_funcall(M, self, "isEnum", 1, mrb_str_new(M, t.data(), t.size())))))
  { return 2 + const_point; }

  return -99;
}

static mrb_value
checkarg(mrb_state* M, mrb_value self) {
  return mrb_fixnum_value(checkarg_int(M, self));
}

static mrb_value
set_qtruby_embedded_wrapped(mrb_state* M, mrb_value /*self*/)
{
  mrb_value yn;
  mrb_get_args(M, "o", &yn);
  set_qtruby_embedded( mrb_test(yn) );
  return mrb_nil_value();
}

  void mrb_mruby_qt_gem_final(mrb_state*) {}

  extern "C" {
    void Init_qtdeclarative(mrb_state*);
    void Init_qtscript(mrb_state*);
    void Init_qttest(mrb_state*);
    void Init_qtuitools(mrb_state*);
    void Init_qtwebkit(mrb_state*);
  }
  
#define INIT_BINDING(module) \
  static QtRuby::Binding module##_binding = QtRuby::Binding(M, module##_Smoke); \
    QtRubyModule module = { "QtRuby_" #module, resolve_classname_qt, 0, &module##_binding }; \
    qtruby_modules[module##_Smoke] = module; \
    smokeList << module##_Smoke;

extern Q_DECL_EXPORT void
mrb_mruby_qt_gem_init(mrb_state* M)
{
    init_qtcore_Smoke();
    init_qtgui_Smoke();
    init_qtxml_Smoke();
    init_qtsql_Smoke();
    init_qtopengl_Smoke();
    init_qtnetwork_Smoke();
    init_qtsvg_Smoke();
#ifdef QT_QTDBUS
    init_qtdbus_Smoke();
#endif
    install_handlers(Qt_handlers);

    INIT_BINDING(qtcore)
    INIT_BINDING(qtgui)
    INIT_BINDING(qtxml)
    INIT_BINDING(qtsql)
    INIT_BINDING(qtopengl)
    INIT_BINDING(qtnetwork)
    INIT_BINDING(qtsvg)
#ifdef QT_QTDBUS
    INIT_BINDING(qtdbus)
#endif

    if(not mrb_obj_respond_to(M, M->module_class, mrb_intern_lit(M, "name"))) {
      mrb_define_method(M, M->module_class, "name", &module_name, MRB_ARGS_NONE());
    }

    RClass* Qt_module = mrb_define_module(M, "Qt");
		RClass* QtInternal_module = mrb_define_module_under(M, Qt_module, "Internal");
		RClass* QtBase_class = mrb_define_class_under(M, Qt_module, "Base", M->object_class);
		RClass* QtModuleIndex_class = mrb_define_class_under(M, QtInternal_module, "ModuleIndex", M->object_class);

    MRB_SET_INSTANCE_TT(QtBase_class, MRB_TT_DATA);
    MRB_SET_INSTANCE_TT(QtModuleIndex_class, MRB_TT_DATA);

    mrb_define_method(M, QtBase_class, "initialize", initialize_qt, MRB_ARGS_ANY());
    mrb_define_module_function(M, QtBase_class, "method_missing", class_method_missing, MRB_ARGS_ANY());
    mrb_define_module_function(M, Qt_module, "method_missing", module_method_missing, MRB_ARGS_ANY());
    mrb_define_method(M, QtBase_class, "method_missing", method_missing, MRB_ARGS_ANY());

    mrb_define_module_function(M, QtBase_class, "const_missing", class_method_missing, MRB_ARGS_ANY());
    mrb_define_module_function(M, Qt_module, "const_missing", module_method_missing, MRB_ARGS_ANY());
    mrb_define_method(M, QtBase_class, "const_missing", method_missing, MRB_ARGS_ANY());

    mrb_define_method(M, QtBase_class, "dispose", dispose, MRB_ARGS_REQ(0));
    mrb_define_method(M, QtBase_class, "isDisposed", is_disposed, MRB_ARGS_REQ(0));
    mrb_define_method(M, QtBase_class, "disposed?", is_disposed, MRB_ARGS_REQ(0));

	mrb_define_method(M, QtBase_class, "qVariantValue", qvariant_value, MRB_ARGS_REQ(2));
	mrb_define_method(M, QtBase_class, "qVariantFromValue", qvariant_from_value, MRB_ARGS_ANY());

	mrb_define_method(M, M->object_class, "qDebug", qdebug, MRB_ARGS_REQ(1));
	mrb_define_method(M, M->object_class, "qFatal", qfatal, MRB_ARGS_REQ(1));
	mrb_define_method(M, M->object_class, "qWarning", qwarning, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "getMethStat", getMethStat, MRB_ARGS_REQ(0));
    mrb_define_module_function(M, QtInternal_module, "getClassStat", getClassStat, MRB_ARGS_REQ(0));
    mrb_define_module_function(M, QtInternal_module, "getIsa", getIsa, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "setDebug", setDebug, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "debug", debugging, MRB_ARGS_REQ(0));
    mrb_define_module_function(M, QtInternal_module, "get_arg_type_name", get_arg_type_name, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "classIsa", classIsa, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "isEnum", isEnum, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "insert_pclassid", insert_pclassid, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "classid2name", classid2name, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "find_pclassid", find_pclassid, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "get_value_type", get_value_type, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "make_metaObject", make_metaObject, MRB_ARGS_REQ(4));
    mrb_define_module_function(M, QtInternal_module, "addMetaObjectMethods", add_metaobject_methods, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "addSignalMethods", add_signal_methods, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "mapObject", mapObject, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "isQObject", isQObject, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "idInstance", idInstance, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "findClass", findClass, MRB_ARGS_REQ(1));
//     mrb_define_module_function(M, QtInternal_module, "idMethodName", idMethodName, MRB_ARGS_REQ(1));
//     mrb_define_module_function(M, QtInternal_module, "idMethod", idMethod, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "findMethod", findMethod, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "findAllMethods", findAllMethods, MRB_ARGS_ANY());
    mrb_define_module_function(M, QtInternal_module, "findAllMethodNames", findAllMethodNames, MRB_ARGS_REQ(3));
    mrb_define_module_function(M, QtInternal_module, "dumpCandidates", dumpCandidates, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "isConstMethod", isConstMethod, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "isObject", isObject, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "setCurrentMethod", setCurrentMethod, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "getClassList", getClassList, MRB_ARGS_REQ(0));
    mrb_define_module_function(M, QtInternal_module, "create_qt_class", create_qt_class, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "create_qobject_class", create_qobject_class, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "cast_object_to", cast_object_to, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, Qt_module, "dynamic_cast", cast_object_to, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "kross2smoke", kross2smoke, MRB_ARGS_REQ(2));
    mrb_define_module_function(M, QtInternal_module, "set_qtruby_embedded", set_qtruby_embedded_wrapped, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "application_terminated=", set_application_terminated, MRB_ARGS_REQ(1));

    mrb_define_module_function(M, QtInternal_module, "normalize_classname_default", &normalize_classname_default, MRB_ARGS_REQ(1));
    mrb_define_module_function(M, QtInternal_module, "checkarg", &checkarg, MRB_ARGS_REQ(2));

    mrb_define_module_function(M, Qt_module, "version", version, MRB_ARGS_NONE());
  mrb_define_module_function(M, Qt_module, "qtruby_version", qtruby_version, MRB_ARGS_NONE());

    mrb_define_module_function(M, Qt_module, "qRegisterResourceData", q_register_resource_data, MRB_ARGS_REQ(4));
    mrb_define_module_function(M, Qt_module, "qUnregisterResourceData", q_unregister_resource_data, MRB_ARGS_REQ(4));

    rObject_typeId = QMetaType::registerType("rObject", &delete_ruby_object, &create_ruby_object);

  Init_qtdeclarative(M);
  Init_qtscript(M);
  Init_qttest(M);
  Init_qtuitools(M);
  Init_qtwebkit(M);
}

}
// kate: space-indent false;
