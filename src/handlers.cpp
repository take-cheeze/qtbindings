/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#include <QtCore/qdir.h>
#include <QtCore/qhash.h>
#include <QtCore/qlinkedlist.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qobject.h>
#include <QtCore/qpair.h>
#include <QtCore/qprocess.h>
#include <QtCore/qregexp.h>
#include <QtCore/qstring.h>
#include <QtCore/qtextcodec.h>
#include <QtCore/qurl.h>
#include <QtGui/qabstractbutton.h>
#include <QtGui/qaction.h>
#include <QtGui/qapplication.h>
#include <QtGui/qdockwidget.h>
#include <QtGui/qevent.h>
#include <QtGui/qlayout.h>
#include <QtGui/qlistwidget.h>
#include <QtGui/qpainter.h>
#include <QtGui/qpalette.h>
#include <QtGui/qpixmap.h>
#include <QtGui/qpolygon.h>
#include <QtGui/qtabbar.h>
#include <QtGui/qtablewidget.h>
#include <QtGui/qtextedit.h>
#include <QtGui/qtextlayout.h>
#include <QtGui/qtextobject.h>
#include <QtGui/qtoolbar.h>
#include <QtGui/qtreewidget.h>
#include <QtGui/qwidget.h>
#include <QtNetwork/qhostaddress.h>
#include <QtNetwork/qnetworkinterface.h>
#include <QtNetwork/qurlinfo.h>


#if QT_VERSION >= 0x40200
#include <QtGui/qgraphicsitem.h>
#include <QtGui/qgraphicslayout.h>
#include <QtGui/qgraphicsscene.h>
#include <QtGui/qgraphicswidget.h>
#include <QtGui/qstandarditemmodel.h>
#include <QtGui/qundostack.h>
#endif

#if QT_VERSION >= 0x40300
#include <QtGui/qmdisubwindow.h>
#include <QtNetwork/qsslcertificate.h>
#include <QtNetwork/qsslcipher.h>
#include <QtNetwork/qsslerror.h>
#include <QtXml/qxmlstream.h>
#endif

#if QT_VERSION >= 0x040400
#include <QtGui/qprinterinfo.h>
#include <QtNetwork/qnetworkcookie.h>
#endif

#include <smoke.h>

#include "marshall.h"
#include "qtruby.h"
#include "smokeruby.h"
#include "marshall_basetypes.h"
#include "marshall_macros.h"

void
mark_qobject_children(mrb_state* M, QObject * qobject)
{
	mrb_value obj;

	const QList<QObject*> l = qobject->children();

	if (l.count() == 0) {
		return;
	}

	QObject *child;

	for (int i=0; i < l.size(); ++i) {
		child = l.at(i);
		obj = getPointerObject(M, child);
		if (not mrb_nil_p(obj)) {
			if(do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", child->metaObject()->className(), child, mrb_string_value_ptr(M, obj));
			mrb_gc_mark_value(M, obj);
		}

		mark_qobject_children(M, child);
	}
}

void
mark_qgraphicsitem_children(mrb_state* M, QGraphicsItem * item)
{
	mrb_value obj;

	const QList<QGraphicsItem*> l = item->childItems();

	if (l.count() == 0) {
		return;
	}

	QGraphicsItem *child;

	for (int i=0; i < l.size(); ++i) {
		child = l.at(i);
		obj = getPointerObject(M, child);
		if (not mrb_nil_p(obj)) {
			if(do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QGraphicsItem", child, mrb_string_value_ptr(M, obj));
			mrb_gc_mark_value(M, obj);
		}

		mark_qgraphicsitem_children(M, child);
	}
}

void
mark_qtreewidgetitem_children(mrb_state* M, QTreeWidgetItem * item)
{
	mrb_value obj;
	QTreeWidgetItem *child;

	for (int i = 0; i < item->childCount(); i++) {
		child = item->child(i);
		obj = getPointerObject(M, child);
		if (not mrb_nil_p(obj)) {
			if(do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QTreeWidgetItem", child, mrb_string_value_ptr(M, obj));
			mrb_gc_mark_value(M, obj);
		}

		mark_qtreewidgetitem_children(M, child);
	}
}

void
mark_qstandarditem_children(mrb_state* M, QStandardItem * item)
{
	mrb_value obj;

	for (int row = 0; row < item->rowCount(); row++) {
		for (int column = 0; column < item->columnCount(); column++) {
			QStandardItem * child = item->child(row, column);
			if (child != 0) {
				if (child->hasChildren()) {
					mark_qstandarditem_children(M, child);
				}
				obj = getPointerObject(M, child);
				if (not mrb_nil_p(obj)) {
					if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QStandardItem", item, mrb_string_value_ptr(M, obj));
					mrb_gc_mark_value(M, obj);
				}
			}
		}
	}
}

void
smokeruby_mark(mrb_state* M, mrb_value obj)
{
  if(not DATA_TYPE(obj)) { return; }
  assert(DATA_TYPE(obj) == &smokeruby_type);
  smokeruby_object * o = (smokeruby_object *) DATA_PTR(obj);
  if(not o) { return; }
    const char *className = o->smoke->classes[o->classId].className;

	if (do_debug & qtdb_gc) qWarning("Checking for mark (%s*)%p", className, o->ptr);

    if (o->ptr && o->allocated) {
		if (o->smoke->isDerivedFrom(className, "QObject")) {
			QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject", true).index);
			// Only mark the QObject tree if the current item doesn't have a parent or the parent isn't wrapped by the bindings.
			// This avoids marking parts of a tree more than once.
			if (qobject->parent() == 0 || mrb_nil_p(getPointerObject(M, qobject->parent()))) {
				mark_qobject_children(M, qobject);
			}
		}

		if (o->smoke->isDerivedFrom(className, "QWidget")) {
			QWidget * widget = (QWidget *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QWidget", true).index);
			QLayout * layout = widget->layout();
			if (layout != 0) {
				obj = getPointerObject(M, layout);
				if (not mrb_nil_p(obj)) {
					if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QLayout", layout, mrb_string_value_ptr(M, obj));
					mrb_gc_mark_value(M, obj);
				}
			}
		}

		if (o->smoke->isDerivedFrom(className, "QListWidget")) {
			QListWidget * listwidget = (QListWidget *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QListWidget", true).index);

			for (int i = 0; i < listwidget->count(); i++) {
				QListWidgetItem * item = listwidget->item(i);
				obj = getPointerObject(M, item);
				if (not mrb_nil_p(obj)) {
					if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %p", "QListWidgetItem", item, mrb_string_value_ptr(M, obj));
					mrb_gc_mark_value(M, obj);
				}
			}
			return;
		}

		if (o->smoke->isDerivedFrom(className, "QTableWidget")) {
			QTableWidget * table = (QTableWidget *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QTableWidget", true).index);
			QTableWidgetItem *item;

			for ( int row = 0; row < table->rowCount(); row++ ) {
				for ( int col = 0; col < table->columnCount(); col++ ) {
					item = table->item(row, col);
					obj = getPointerObject(M, item);
					if (not mrb_nil_p(obj)) {
						if(do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", className, item, mrb_string_value_ptr(M, obj));
						mrb_gc_mark_value(M, obj);
					}
				}
			}
			return;
		}

		if (o->smoke->isDerivedFrom(className, "QTreeWidget")) {
			QTreeWidget * qtreewidget = (QTreeWidget *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QTreeWidget", true).index);

			for (int i = 0; i < qtreewidget->topLevelItemCount(); i++) {
				QTreeWidgetItem * item = qtreewidget->topLevelItem(i);
				obj = getPointerObject(M, item);
				if (not mrb_nil_p(obj)) {
					if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QTreeWidgetItem", item, mrb_string_value_ptr(M, obj));
					mrb_gc_mark_value(M, obj);
				}
				mark_qtreewidgetitem_children(M, item);
			}
			return;
		}

		if (o->smoke->isDerivedFrom(className, "QLayout")) {
			QLayout * qlayout = (QLayout *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QLayout", true).index);
      obj = getPointerObject(M, qlayout);
			for (int i = 0; i < qlayout->count(); ++i) {
				QLayoutItem * item = qlayout->itemAt(i);
        if (do_debug & qtdb_gc) qWarning("Checking QLayoutItem %p", item);
				if (item != 0) {
					obj = getPointerObject(M, item);
					if (not mrb_nil_p(obj)) {
						if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QLayoutItem", item, mrb_string_value_ptr(M, obj));
						mrb_gc_mark_value(M, obj);
					}
          QWidget * widget = item->widget();
          if (widget != 0) {
            obj = getPointerObject(M, widget);
            if (not mrb_nil_p(obj)) {
              if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QLayoutItem->widget", widget, mrb_string_value_ptr(M, obj));
              mrb_gc_mark_value(M, obj);
            }
          }
				}
			}
			return;
		}

		if (o->smoke->isDerivedFrom(className, "QStandardItemModel")) {
			QStandardItemModel * model = (QStandardItemModel *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QStandardItemModel", true).index);
			for (int row = 0; row < model->rowCount(); row++) {
				for (int column = 0; column < model->columnCount(); column++) {
					QStandardItem * item = model->item(row, column);
					if (item != 0) {
						if (item->hasChildren()) {
							mark_qstandarditem_children(M, item);
						}
						obj = getPointerObject(M, item);
						if (not mrb_nil_p(obj)) {
							if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QStandardItem", item, mrb_string_value_ptr(M, obj));
							mrb_gc_mark_value(M, obj);
						}
					}
				}
			}
			return;
		}

		if (o->smoke->isDerivedFrom(className, "QGraphicsWidget")) {
			QGraphicsWidget * widget = (QGraphicsWidget *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsWidget", true).index);
			QGraphicsLayout * layout = widget->layout();
			if (layout != 0) {
				obj = getPointerObject(M, layout);
				if (not mrb_nil_p(obj)) {
					if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QGraphicsLayout", layout, mrb_string_value_ptr(M, obj));
					mrb_gc_mark_value(M, obj);
				}
			}
		}

		if (o->smoke->isDerivedFrom(className, "QGraphicsLayout")) {
			QGraphicsLayout * qlayout = (QGraphicsLayout *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsLayout", true).index);
			for (int i = 0; i < qlayout->count(); ++i) {
				QGraphicsLayoutItem * item = qlayout->itemAt(i);
				if (item != 0) {
					obj = getPointerObject(M, item);
					if (not mrb_nil_p(obj)) {
						if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QGraphicsLayoutItem", item, mrb_string_value_ptr(M, obj));
						mrb_gc_mark_value(M, obj);
					}
				}
			}
			return;
		}

		if (o->smoke->isDerivedFrom(className, "QGraphicsItem")) {
			QGraphicsItem * item = (QGraphicsItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsItem", true).index);
			// Only mark the QGraphicsItem tree if the current item doesn't have a parent.
			// This avoids marking parts of a tree more than once.
			if (item->parentItem() == 0) {
				mark_qgraphicsitem_children(M, item);
			}
			if (QGraphicsEffect* effect = item->graphicsEffect()) {
				obj = getPointerObject(M, effect);
				if (not mrb_nil_p(obj)) {
				  if (do_debug & qtdb_gc)
            qWarning("Marking (%s*)%p -> %p", "QGraphicsEffect", effect, mrb_string_value_ptr(M, obj));
					mrb_gc_mark_value(M, obj);
				}
			}
		}

		if (o->smoke->isDerivedFrom(className, "QGraphicsScene")) {
			QGraphicsScene * scene = (QGraphicsScene *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsScene", true).index);
			QList<QGraphicsItem *> list = scene->items();
			for (int i = 0; i < list.size(); i++) {
				QGraphicsItem * item = list.at(i);
				if (item != 0) {
					obj = getPointerObject(M, item);
					if (not mrb_nil_p(obj)) {
						if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QGraphicsItem", item, mrb_string_value_ptr(M, obj));
						mrb_gc_mark_value(M, obj);
					}
				}
			}
			return;
		}

		if (qstrcmp(className, "QModelIndex") == 0) {
			QModelIndex * qmodelindex = (QModelIndex *) o->ptr;
			void * ptr = qmodelindex->internalPointer();
      obj = getPointerObject(M, ptr);
			if (not mrb_nil_p(obj)) {
				if (do_debug & qtdb_gc) qWarning("Marking (%s*)%p -> %s", "QModelIndex", ptr, mrb_string_value_ptr(M, obj));

				mrb_gc_mark_value(M, obj);
			}

			return;
		}
	}
}

void
smokeruby_free(mrb_state* M, void* p)
{
  smokeruby_object *o = static_cast<smokeruby_object*>(p);
  const char *className = o->smoke->classes[o->classId].className;

	if(do_debug & qtdb_gc) qWarning("Checking for delete (%s*)%p allocated: %s", className, o->ptr, o->allocated ? "true" : "false");

	if(application_terminated || !o->allocated || o->ptr == 0) {
		free_smokeruby_object(M, o);
		return;
	}

	unmapPointer(o, o->classId, 0);
	object_count --;

	if (o->smoke->isDerivedFrom(className, "QGraphicsLayoutItem")) {
		QGraphicsLayoutItem * item = (QGraphicsLayoutItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsLayoutItem", true).index);
		if (item->graphicsItem() != 0 || item->parentLayoutItem() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (o->smoke->isDerivedFrom(className, "QGraphicsItem")) {
		QGraphicsItem * item = (QGraphicsItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsItem", true).index);
		if (item->parentItem() != 0 || item->parentObject() != 0 || item->parentWidget() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (o->smoke->isDerivedFrom(className, "QLayoutItem")) {
		QLayoutItem * item = (QLayoutItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QLayoutItem", true).index);
		if (item->layout() != 0 || item->widget() != 0 || item->spacerItem() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (o->smoke->isDerivedFrom(className, "QStandardItem")) {
		QStandardItem * item = (QStandardItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QStandardItem", true).index);
		if (item->model() != 0 || item->parent() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (qstrcmp(className, "QListWidgetItem") == 0) {
		QListWidgetItem * item = (QListWidgetItem *) o->ptr;
		if (item->listWidget() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (o->smoke->isDerivedFrom(className, "QTableWidgetItem")) {
		QTableWidgetItem * item = (QTableWidgetItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QTableWidgetItem", true).index);
		if (item->tableWidget() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (o->smoke->isDerivedFrom(className, "QWidget")) {
		QWidget * qwidget = (QWidget *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QWidget", true).index);
		if (qwidget->parentWidget() != 0 || QCoreApplication::closingDown()) {
			free_smokeruby_object(M, o);
			return;
		}
	} else if (o->smoke->isDerivedFrom(className, "QObject")) {
		QObject * qobject = (QObject *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QObject", true).index);
		if (qobject->parent() != 0) {
			free_smokeruby_object(M, o);
			return;
		}
	}

	if(do_debug & qtdb_gc) qWarning("Deleting (%s*)%p", className, o->ptr);

	//~ char *methodName = new char[strlen(className) + 2];
	//~ methodName[0] = '~';
	//~ strcpy(methodName + 1, className);
	//~ Smoke::ModuleIndex nameId = o->smoke->findMethodName(className, methodName);
	//~ Smoke::ModuleIndex classIdx(o->smoke, o->classId);
	//~ Smoke::ModuleIndex meth = o->smoke->findMethod(classIdx, nameId);
	//~ if(meth.index > 0) {
		//~ Smoke::Method &m = meth.smoke->methods[meth.smoke->methodMaps[meth.index].method];
		//~ Smoke::ClassFn fn = meth.smoke->classes[m.classId].classFn;
		//~ Smoke::StackItem i[1];
		//~ (*fn)(m.method, o->ptr, i);
	//~ }
	//~ delete[] methodName;
	free_smokeruby_object(M, o);

    return;
}

/*
 * Given an approximate classname and a qt instance, try to improve the resolution of the name
 * by using the various Qt rtti mechanisms for QObjects, QEvents and so on
 */
Q_DECL_EXPORT const char *
resolve_classname_qt(smokeruby_object * o)
{
#define SET_SMOKERUBY_OBJECT(className) \
    { \
        Smoke::ModuleIndex mi = Smoke::findClass(className); \
        o->classId = mi.index; \
        o->smoke = mi.smoke; \
    }

	if (o->smoke->isDerivedFrom(o->smoke->classes[o->classId].className, "QEvent")) {
		QEvent * qevent = (QEvent *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QEvent", true).index);
		switch (qevent->type()) {
		case QEvent::Timer:
   			SET_SMOKERUBY_OBJECT("QTimerEvent")
			break;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseMove:
   			SET_SMOKERUBY_OBJECT("QMouseEvent")
			break;
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		case QEvent::ShortcutOverride:
   			SET_SMOKERUBY_OBJECT("QKeyEvent")
			break;
		case QEvent::FocusIn:
		case QEvent::FocusOut:
   			SET_SMOKERUBY_OBJECT("QFocusEvent")
			break;
		case QEvent::Enter:
		case QEvent::Leave:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::Paint:
   			SET_SMOKERUBY_OBJECT("QPaintEvent")
			break;
		case QEvent::Move:
   			SET_SMOKERUBY_OBJECT("QMoveEvent")
			break;
		case QEvent::Resize:
   			SET_SMOKERUBY_OBJECT("QResizeEvent")
			break;
		case QEvent::Create:
		case QEvent::Destroy:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::Show:
   			SET_SMOKERUBY_OBJECT("QShowEvent")
			break;
		case QEvent::Hide:
   			SET_SMOKERUBY_OBJECT("QHideEvent")
		case QEvent::Close:
   			SET_SMOKERUBY_OBJECT("QCloseEvent")
			break;
		case QEvent::Quit:
		case QEvent::ParentChange:
		case QEvent::ParentAboutToChange:
		case QEvent::ThreadChange:
		case QEvent::WindowActivate:
		case QEvent::WindowDeactivate:
		case QEvent::ShowToParent:
		case QEvent::HideToParent:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::Wheel:
   			SET_SMOKERUBY_OBJECT("QWheelEvent")
			break;
		case QEvent::WindowTitleChange:
		case QEvent::WindowIconChange:
		case QEvent::ApplicationWindowIconChange:
		case QEvent::ApplicationFontChange:
		case QEvent::ApplicationLayoutDirectionChange:
		case QEvent::ApplicationPaletteChange:
		case QEvent::PaletteChange:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::Clipboard:
   			SET_SMOKERUBY_OBJECT("QClipboardEvent")
			break;
		case QEvent::Speech:
		case QEvent::MetaCall:
		case QEvent::SockAct:
		case QEvent::WinEventAct:
		case QEvent::DeferredDelete:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::DragEnter:
   			SET_SMOKERUBY_OBJECT("QDragEnterEvent")
			break;
		case QEvent::DragLeave:
   			SET_SMOKERUBY_OBJECT("QDragLeaveEvent")
			break;
		case QEvent::DragMove:
   			SET_SMOKERUBY_OBJECT("QDragMoveEvent")
		case QEvent::Drop:
   			SET_SMOKERUBY_OBJECT("QDropEvent")
			break;
		case QEvent::DragResponse:
   			SET_SMOKERUBY_OBJECT("QDragResponseEvent")
			break;
		case QEvent::ChildAdded:
		case QEvent::ChildRemoved:
		case QEvent::ChildPolished:
   			SET_SMOKERUBY_OBJECT("QChildEvent")
			break;
		case QEvent::ShowWindowRequest:
		case QEvent::PolishRequest:
		case QEvent::Polish:
		case QEvent::LayoutRequest:
		case QEvent::UpdateRequest:
		case QEvent::EmbeddingControl:
		case QEvent::ActivateControl:
		case QEvent::DeactivateControl:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
        case QEvent::ContextMenu:
			SET_SMOKERUBY_OBJECT("QContextMenuEvent")
            break;
        case QEvent::DynamicPropertyChange:
			SET_SMOKERUBY_OBJECT("QDynamicPropertyChangeEvent")
            break;
		case QEvent::InputMethod:
   			SET_SMOKERUBY_OBJECT("QInputMethodEvent")
			break;
		case QEvent::AccessibilityPrepare:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::TabletMove:
		case QEvent::TabletPress:
		case QEvent::TabletRelease:
   			SET_SMOKERUBY_OBJECT("QTabletEvent")
			break;
		case QEvent::LocaleChange:
		case QEvent::LanguageChange:
		case QEvent::LayoutDirectionChange:
		case QEvent::Style:
		case QEvent::OkRequest:
		case QEvent::HelpRequest:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::IconDrag:
   			SET_SMOKERUBY_OBJECT("QIconDragEvent")
			break;
		case QEvent::FontChange:
		case QEvent::EnabledChange:
		case QEvent::ActivationChange:
		case QEvent::StyleChange:
		case QEvent::IconTextChange:
		case QEvent::ModifiedChange:
		case QEvent::MouseTrackingChange:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::WindowBlocked:
		case QEvent::WindowUnblocked:
		case QEvent::WindowStateChange:
   			SET_SMOKERUBY_OBJECT("QWindowStateChangeEvent")
			break;
		case QEvent::ToolTip:
		case QEvent::WhatsThis:
   			SET_SMOKERUBY_OBJECT("QHelpEvent")
			break;
		case QEvent::StatusTip:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::ActionChanged:
		case QEvent::ActionAdded:
		case QEvent::ActionRemoved:
   			SET_SMOKERUBY_OBJECT("QActionEvent")
			break;
		case QEvent::FileOpen:
   			SET_SMOKERUBY_OBJECT("QFileOpenEvent")
			break;
		case QEvent::Shortcut:
   			SET_SMOKERUBY_OBJECT("QShortcutEvent")
			break;
		case QEvent::WhatsThisClicked:
   			SET_SMOKERUBY_OBJECT("QWhatsThisClickedEvent")
			break;
		case QEvent::ToolBarChange:
   			SET_SMOKERUBY_OBJECT("QToolBarChangeEvent")
			break;
		case QEvent::ApplicationActivated:
		case QEvent::ApplicationDeactivated:
		case QEvent::QueryWhatsThis:
		case QEvent::EnterWhatsThisMode:
		case QEvent::LeaveWhatsThisMode:
		case QEvent::ZOrderChange:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
		case QEvent::HoverEnter:
		case QEvent::HoverLeave:
		case QEvent::HoverMove:
   			SET_SMOKERUBY_OBJECT("QHoverEvent")
			break;
		case QEvent::AccessibilityHelp:
		case QEvent::AccessibilityDescription:
   			SET_SMOKERUBY_OBJECT("QEvent")
#if QT_VERSION >= 0x40200
		case QEvent::GraphicsSceneMouseMove:
		case QEvent::GraphicsSceneMousePress:
		case QEvent::GraphicsSceneMouseRelease:
		case QEvent::GraphicsSceneMouseDoubleClick:
   			SET_SMOKERUBY_OBJECT("QGraphicsSceneMouseEvent")
			break;
		case QEvent::GraphicsSceneContextMenu:
   			SET_SMOKERUBY_OBJECT("QGraphicsSceneContextMenuEvent")
			break;
		case QEvent::GraphicsSceneHoverEnter:
		case QEvent::GraphicsSceneHoverMove:
		case QEvent::GraphicsSceneHoverLeave:
   			SET_SMOKERUBY_OBJECT("QGraphicsSceneHoverEvent")
			break;
		case QEvent::GraphicsSceneHelp:
   			SET_SMOKERUBY_OBJECT("QGraphicsSceneHelpEvent")
			break;
		case QEvent::GraphicsSceneDragEnter:
		case QEvent::GraphicsSceneDragMove:
		case QEvent::GraphicsSceneDragLeave:
		case QEvent::GraphicsSceneDrop:
   			SET_SMOKERUBY_OBJECT("QGraphicsSceneDragDropEvent")
			break;
		case QEvent::GraphicsSceneWheel:
   			SET_SMOKERUBY_OBJECT("QGraphicsSceneWheelEvent")
			break;
		case QEvent::KeyboardLayoutChange:
   			SET_SMOKERUBY_OBJECT("QEvent")
			break;
#endif
		default:
			break;
		}
	} else if (o->smoke->isDerivedFrom(o->smoke->classes[o->classId].className, "QGraphicsItem")) {
		QGraphicsItem * item = (QGraphicsItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QGraphicsItem", true).index);
		switch (item->type()) {
		case 1:
   			SET_SMOKERUBY_OBJECT("QGraphicsItem")
			break;
		case 2:
   			SET_SMOKERUBY_OBJECT("QGraphicsPathItem")
			break;
		case 3:
   			SET_SMOKERUBY_OBJECT("QGraphicsRectItem")
		case 4:
   			SET_SMOKERUBY_OBJECT("QGraphicsEllipseItem")
			break;
		case 5:
   			SET_SMOKERUBY_OBJECT("QGraphicsPolygonItem")
			break;
		case 6:
   			SET_SMOKERUBY_OBJECT("QGraphicsLineItem")
			break;
		case 7:
   			SET_SMOKERUBY_OBJECT("QGraphicsItem")
			break;
		case 8:
   			SET_SMOKERUBY_OBJECT("QGraphicsTextItem")
			break;
		case 9:
   			SET_SMOKERUBY_OBJECT("QGraphicsSimpleTextItem")
			break;
		case 10:
   			SET_SMOKERUBY_OBJECT("QGraphicsItemGroup")
			break;
		}
	} else if (o->smoke->isDerivedFrom(o->smoke->classes[o->classId].className, "QLayoutItem")) {
		QLayoutItem * item = (QLayoutItem *) o->smoke->cast(o->ptr, o->classId, o->smoke->idClass("QLayoutItem", true).index);
		if (item->widget() != 0) {
   			SET_SMOKERUBY_OBJECT("QWidgetItem")
		} else if (item->spacerItem() != 0) {
   			SET_SMOKERUBY_OBJECT("QSpacerItem")
		}
	}

	return qtruby_modules[o->smoke].binding->className(o->classId);

#undef SET_SMOKERUBY_OBJECT
}

bool
matches_arg(Smoke *smoke, Smoke::Index meth, Smoke::Index argidx, const char *argtype)
{
	Smoke::Index *arg = smoke->argumentList + smoke->methods[meth].args + argidx;
	SmokeType type = SmokeType(smoke, *arg);
	if (type.name() && qstrcmp(type.name(), argtype) == 0) {
		return true;
	}
	return false;
}

void *
construct_copy(smokeruby_object *o)
{
    const char *className = o->smoke->className(o->classId);
    int classNameLen = strlen(className);

    // copy constructor signature
    QByteArray ccSig(className);
    int pos = ccSig.lastIndexOf("::");
    if (pos != -1) {
        ccSig = ccSig.mid(pos + strlen("::"));
    }
    ccSig.append("#");
    Smoke::ModuleIndex ccId = o->smoke->findMethodName(className, ccSig);

    char *ccArg = new char[classNameLen + 8];
    sprintf(ccArg, "const %s&", className);

    Smoke::ModuleIndex classIdx(o->smoke, o->classId);
    Smoke::ModuleIndex ccMeth = o->smoke->findMethod(classIdx, ccId);

    if (ccMeth.index == 0) {
        qWarning("construct_copy() failed %s %p\n", resolve_classname(o), o->ptr);
        delete[] ccArg;
        return 0;
    }
    Smoke::Index method = ccMeth.smoke->methodMaps[ccMeth.index].method;
    if (method > 0) {
        // Make sure it's a copy constructor
        if (!matches_arg(o->smoke, method, 0, ccArg)) {
            qWarning("construct_copy() failed %s %p\n", resolve_classname(o), o->ptr);
            delete[] ccArg;
            return 0;
        }
        delete[] ccArg;
        ccMeth.index = method;
    } else {
        // ambiguous method, pick the copy constructor
        Smoke::Index i = -method;
        while (ccMeth.smoke->ambiguousMethodList[i]) {
            if (matches_arg(ccMeth.smoke, ccMeth.smoke->ambiguousMethodList[i], 0, ccArg)) {
                break;
            }
            i++;
        }
        delete[] ccArg;
        ccMeth.index = ccMeth.smoke->ambiguousMethodList[i];
        if (ccMeth.index == 0) {
            qWarning("construct_copy() failed %s %p\n", resolve_classname(o), o->ptr);
            return 0;
        }
    }

    // Okay, ccMeth is the copy constructor. Time to call it.
    Smoke::StackItem args[2];
    args[0].s_voidp = 0;
    args[1].s_voidp = o->ptr;
    Smoke::ClassFn fn = o->smoke->classes[o->classId].classFn;
    (*fn)(o->smoke->methods[ccMeth.index].method, 0, args);

    // Initialize the binding for the new instance
    Smoke::StackItem s[2];
    s[1].s_voidp = qtruby_modules[o->smoke].binding;
    (*fn)(0, args[0].s_voidp, s);

    return args[0].s_voidp;
}

template <class T>
static void marshall_it(Marshall *m)
{
	switch(m->action()) {
		case Marshall::FromVALUE:
			marshall_from_ruby<T>(m);
		break;

		case Marshall::ToVALUE:
			marshall_to_ruby<T>( m );
		break;

		default:
			m->unsupported();
		break;
	}
}

void
marshall_basetype(Marshall *m)
{
	switch(m->type().elem()) {

		case Smoke::t_bool:
			marshall_it<bool>(m);
		break;

		case Smoke::t_char:
			marshall_it<signed char>(m);
		break;

		case Smoke::t_uchar:
			marshall_it<unsigned char>(m);
		break;

		case Smoke::t_short:
			marshall_it<short>(m);
		break;

		case Smoke::t_ushort:
			marshall_it<unsigned short>(m);
		break;

		case Smoke::t_int:
			marshall_it<int>(m);
		break;

		case Smoke::t_uint:
			marshall_it<unsigned int>(m);
		break;

		case Smoke::t_long:
			marshall_it<long>(m);
		break;

		case Smoke::t_ulong:
			marshall_it<unsigned long>(m);
		break;

		case Smoke::t_float:
			marshall_it<float>(m);
		break;

		case Smoke::t_double:
			marshall_it<double>(m);
		break;

		case Smoke::t_enum:
			marshall_it<SmokeEnumWrapper>(m);
		break;

		case Smoke::t_class:
			marshall_it<SmokeClassWrapper>(m);
		break;

		default:
			m->unsupported();
		break;
	}

}

static void marshall_void(Marshall * /*m*/) {}
static void marshall_unknown(Marshall *m) {
    m->unsupported();
}

void marshall_ucharP(Marshall *m) {
  marshall_it<unsigned char *>(m);
}

static void marshall_doubleR(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value rv = *(m->var());
		double * d = new double;
		*d = mrb_float(rv);
		m->item().s_voidp = d;
		m->next();
		if (m->cleanup() && m->type().isConst()) {
			delete d;
		} else {
			m->item().s_voidp = new double((double)mrb_float(rv));
		}
	}
	break;
	case Marshall::ToVALUE:
	{
		double *dp = (double*)m->item().s_voidp;
	    mrb_value rv = *(m->var());
		if (dp == 0) {
			rv = mrb_nil_value();
			break;
		}
		*(m->var()) = mrb_float_value(m->M, *dp);
		m->next();
		if (!m->type().isConst()) {
			*dp = mrb_float(*(m->var()));
		}
	}
	break;
	default:
		m->unsupported();
		break;
	}
}

QString*
qstringFromRString(mrb_state* M, mrb_value rstring) {
  // force UTF-8
  rstring = mrb_str_to_str(M, rstring);
  return new QString(QString::fromUtf8(RSTRING_PTR(rstring), RSTRING_LEN(rstring)));
}

mrb_value
rstringFromQString(mrb_state* M, QString * s) {
  if (s->isNull()) { return mrb_nil_value(); }
  QByteArray const str = s->toUtf8();
	return mrb_str_new(M, str.constData(), str.size());
}

QByteArray*
qbytearrayFromRString(mrb_state* M, mrb_value rstring) {
  rstring = mrb_str_to_str(M, rstring);
  return new QByteArray(RSTRING_PTR(rstring), RSTRING_LEN(rstring));
}

mrb_value
rstringFromQByteArray(mrb_state* M, QByteArray * s) {
  return mrb_str_new(M, s->data(), s->size());
}

static void marshall_QString(Marshall *m) {
	switch(m->action()) {
		case Marshall::FromVALUE:
		{
			QString* s = mrb_nil_p( *(m->var()))? new QString() : qstringFromRString(m->M, *(m->var()));

			m->item().s_voidp = s;
			m->next();

			if (!m->type().isConst() && not mrb_nil_p(*(m->var())) && s != 0 && !s->isNull()) {
				mrb_str_resize(m->M, *(m->var()), 0);
				mrb_value temp = rstringFromQString(m->M, s);
				mrb_str_cat_cstr(m->M, *(m->var()), mrb_string_value_ptr(m->M, temp));
			}

			if (s != 0 && m->cleanup()) { delete s; }
		}
		break;

		case Marshall::ToVALUE:
    {
			QString *s = (QString*)m->item().s_voidp;
      *(m->var()) = (s or not s->isNull())? rstringFromQString(m->M, s) : mrb_nil_value();
      if(s and (m->cleanup() or m->type().isStack())) { delete s; }
		}
		break;

		default:
			m->unsupported();
		break;
   }
}

static void marshall_QByteArray(Marshall *m) {
  switch(m->action()) {
    case Marshall::FromVALUE:
    {
      QByteArray* s = 0;
      if(not mrb_nil_p( *(m->var()))) {
        s = qbytearrayFromRString(m->M, *(m->var()));
      } else {
        s = new QByteArray();
      }

      m->item().s_voidp = s;
      m->next();

      if (!m->type().isConst() && not mrb_nil_p(*(m->var())) && s != 0 && !s->isNull()) {
        mrb_str_resize(m->M, *(m->var()), 0);
        mrb_value temp = rstringFromQByteArray(m->M, s);
        mrb_str_cat_cstr(m->M, *(m->var()), mrb_string_value_ptr(m->M, temp));
      }

      if (s != 0 && m->cleanup()) {
        delete s;
      }
    }
    break;

    case Marshall::ToVALUE:
    {
      QByteArray *s = (QByteArray*)m->item().s_voidp;
      if(s) {
        if (s->isNull()) {
          *(m->var()) = mrb_nil_value();
        } else {
          *(m->var()) = rstringFromQByteArray(m->M, s);
        }
        if(m->cleanup() || m->type().isStack() ) {
          delete s;
        }
      } else {
        *(m->var()) = mrb_nil_value();
      }
    }
    break;

    default:
      m->unsupported();
    break;
   }
}

// The only way to convert a QChar to a QString is to
// pass a QChar to a QString constructor. However,
// QStrings aren't in the QtRuby api, so add this
// convenience method 'Qt::Char.to_s' to get a ruby
// string from a Qt::Char.
mrb_value
qchar_to_s(mrb_state* M, mrb_value self)
{
	smokeruby_object *o = value_obj_info(M, self);
	if (o == 0 || o->ptr == 0) {
		return mrb_nil_value();
	}

	QChar * qchar = (QChar*) o->ptr;
	QString s(*qchar);
	return rstringFromQString(M, &s);
}

void marshall_QDBusVariant(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value v = *(m->var());
		if (mrb_nil_p(v)) {
			m->item().s_voidp = 0;
			break;
		}

		smokeruby_object *o = value_obj_info(m->M, v);
		if (!o || !o->ptr) {
			if (m->type().isRef()) {
				m->unsupported();
			}
		    m->item().s_class = 0;
		    break;
		}
		m->item().s_class = o->ptr;
		break;
	}

	case Marshall::ToVALUE:
	{
		if (m->item().s_voidp == 0) {
			*(m->var()) = mrb_nil_value();
		    break;
		}

		void *p = m->item().s_voidp;
		mrb_value obj = getPointerObject(m->M, p);
		if(not mrb_nil_p(obj)) {
			*(m->var()) = obj;
		    break;
		}
		smokeruby_object * o = alloc_smokeruby_object(m->M, false, m->smoke(), m->smoke()->findClass("QVariant").index, p);

		obj = set_obj_info(m->M, "Qt::DBusVariant", o);
		if (do_debug & qtdb_calls) {
			qWarning("allocating %s %p -> %s\n", "Qt::DBusVariant", o->ptr, mrb_string_value_ptr(m->M, obj));
		}

		if (m->type().isStack()) {
		    o->allocated = true;
			// Keep a mapping of the pointer so that it is only wrapped once
		    mapPointer(m->M, obj, o, o->classId, 0);
		}

		*(m->var()) = obj;
		break;
	}

	default:
		m->unsupported();
		break;
    }
}

static void marshall_charP_array(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value arglist = *(m->var());
	    if (mrb_nil_p(arglist)
	    || mrb_type(arglist) != MRB_TT_ARRAY
	    || RARRAY_LEN(arglist) == 0 )
	    {
                m->item().s_voidp = 0;
                break;
	    }

	    char **argv = new char *[RARRAY_LEN(arglist) + 1];
	    long i;
	    for(i = 0; i < RARRAY_LEN(arglist); i++) {
                mrb_value item = mrb_ary_entry(arglist, i);
                char *s = mrb_string_value_ptr(m->M, item);
                argv[i] = new char[strlen(s) + 1];
                strcpy(argv[i], s);
	    }
	    argv[i] = 0;
	    m->item().s_voidp = argv;
	    m->next();

      mrb_ary_clear(m->M, arglist);
		for(i = 0; argv[i]; i++) {
      mrb_ary_push(m->M, arglist, mrb_str_new_cstr(m->M, argv[i]));
	    }
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QStringList(Marshall *m) {
	switch(m->action()) {
		case Marshall::FromVALUE:
		{
			mrb_value list = *(m->var());
			if (mrb_type(list) != MRB_TT_ARRAY) {
				m->item().s_voidp = 0;
				break;
			}

			int count = RARRAY_LEN(list);
			QStringList *stringlist = new QStringList;

			for(long i = 0; i < count; i++) {
				mrb_value item = mrb_ary_entry(list, i);
					if(mrb_type(item) != MRB_TT_STRING) {
						stringlist->append(QString());
						continue;
					}

          stringlist->append(*(qstringFromRString(m->M, item)));
			}

			m->item().s_voidp = stringlist;
			m->next();

			if (stringlist != 0 && !m->type().isConst()) {
				mrb_ary_clear(m->M, list);
				for(QStringList::Iterator it = stringlist->begin(); it != stringlist->end(); ++it)
          mrb_ary_push(m->M, list, rstringFromQString(m->M, &(*it)));
			}

			if (m->cleanup()) {
				delete stringlist;
			}

			break;
		}

      case Marshall::ToVALUE:
	{
		QStringList *stringlist = static_cast<QStringList *>(m->item().s_voidp);
		if (!stringlist) {
			*(m->var()) = mrb_nil_value();
			break;
		}

		mrb_value av = mrb_ary_new(m->M);
		for (QStringList::Iterator it = stringlist->begin(); it != stringlist->end(); ++it) {
			mrb_value rv = rstringFromQString(m->M, &(*it));
			mrb_ary_push(m->M, av, rv);
		}

		*(m->var()) = av;

		if (m->cleanup()) {
			delete stringlist;
		}

	}
	break;
      default:
	m->unsupported();
	break;
    }
}


void marshall_QByteArrayList(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value list = *(m->var());
	    if (mrb_type(list) != MRB_TT_ARRAY) {
		m->item().s_voidp = 0;
		break;
	    }

	    int count = RARRAY_LEN(list);
	    QList<QByteArray> *stringlist = new QList<QByteArray>;

	    for(long i = 0; i < count; i++) {
		mrb_value item = mrb_ary_entry(list, i);
		if(mrb_type(item) != MRB_TT_STRING) {
		    stringlist->append(QByteArray());
		    continue;
		}
		stringlist->append(QByteArray(RSTRING_PTR(item), RSTRING_LEN(item)));
	    }

	    m->item().s_voidp = stringlist;
	    m->next();

		if (!m->type().isConst()) {
			mrb_ary_clear(m->M, list);
			for (int i = 0; i < stringlist->size(); i++) {
				mrb_ary_push(m->M, list, mrb_str_new_cstr(m->M, (const char *) stringlist->at(i)));
			}
		}

		if(m->cleanup()) {
			delete stringlist;
	    }
	    break;
      }
      case Marshall::ToVALUE:
	{
	    QList<QByteArray> *stringlist = static_cast<QList<QByteArray>*>(m->item().s_voidp);
	    if(!stringlist) {
        *(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value av = mrb_ary_new(m->M);
		for (int i = 0; i < stringlist->size(); i++) {
			mrb_value rv = mrb_str_new_cstr(m->M, (const char *) stringlist->at(i));
			mrb_ary_push(m->M, av, rv);
		}


	    *(m->var()) = av;

		if (m->cleanup()) {
			delete stringlist;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QListCharStar(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value av = *(m->var());
		if (mrb_type(av) != MRB_TT_ARRAY) {
			m->item().s_voidp = 0;
			break;
		}
		int count = RARRAY_LEN(av);
		QList<const char*> *list = new QList<const char*>;
		long i;
		for(i = 0; i < count; i++) {
			mrb_value item = mrb_ary_entry(av, i);
			if (mrb_type(item) != MRB_TT_STRING) {
				list->append(0);
		    	continue;
			}
			list->append(mrb_string_value_ptr(m->M, item));
		}

		m->item().s_voidp = list;
	}
	break;
	case Marshall::ToVALUE:
	{
		QList<const char*> *list = (QList<const char*>*)m->item().s_voidp;
		if (list == 0) {
			*(m->var()) = mrb_nil_value();
			break;
		}

		mrb_value av = mrb_ary_new(m->M);

		for (	QList<const char*>::iterator i = list->begin();
				i != list->end();
				++i )
		{
      mrb_ary_push(m->M, av, mrb_str_new_cstr(m->M, (const char *)*i));
		}

		*(m->var()) = av;
		m->next();
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QListInt(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value list = *(m->var());
	    if (mrb_type(list) != MRB_TT_ARRAY) {
		m->item().s_voidp = 0;
		break;
	    }
	    int count = RARRAY_LEN(list);
	    QList<int> *valuelist = new QList<int>;
	    long i;
	    for(i = 0; i < count; i++) {
		valuelist->append(get_mrb_int(m->M, mrb_ary_entry(list, i)));
	    }

	    m->item().s_voidp = valuelist;
	    m->next();

		if (!m->type().isConst()) {
			mrb_ary_clear(m->M, list);

			for (	QList<int>::iterator i = valuelist->begin();
					i != valuelist->end();
					++i )
			{
				mrb_ary_push(m->M, list, mrb_fixnum_value((int)*i));
			}
		}

		if (m->cleanup()) {
			delete valuelist;
	    }
	}
	break;
      case Marshall::ToVALUE:
	{
	    QList<int> *valuelist = (QList<int>*)m->item().s_voidp;
	    if(!valuelist) {
        *(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value av = mrb_ary_new(m->M);

		for (	QList<int>::iterator i = valuelist->begin();
				i != valuelist->end();
				++i )
		{
      mrb_ary_push(m->M, av, mrb_fixnum_value((int)*i));
		}

	    *(m->var()) = av;
		m->next();

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}


void marshall_QListUInt(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value list = *(m->var());
	    if (mrb_type(list) != MRB_TT_ARRAY) {
		m->item().s_voidp = 0;
		break;
	    }
	    int count = RARRAY_LEN(list);
	    QList<uint> *valuelist = new QList<uint>;
	    long i;
	    for(i = 0; i < count; i++) {
        valuelist->append(get_mrb_int(m->M, mrb_ary_entry(list, i)));
	    }

	    m->item().s_voidp = valuelist;
	    m->next();

		if (!m->type().isConst()) {
			mrb_ary_clear(m->M, list);

			for (	QList<uint>::iterator i = valuelist->begin();
					i != valuelist->end();
					++i )
			{
				mrb_ary_push(m->M, list, mrb_fixnum_value((int)*i));
			}
		}

		if (m->cleanup()) {
			delete valuelist;
	    }
	}
	break;
      case Marshall::ToVALUE:
	{
	    QList<uint> *valuelist = (QList<uint>*)m->item().s_voidp;
	    if(!valuelist) {
		*(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value av = mrb_ary_new(m->M);

		for (	QList<uint>::iterator i = valuelist->begin();
				i != valuelist->end();
				++i )
		{
      mrb_ary_push(m->M, av, mrb_fixnum_value((int)*i));
		}

	    *(m->var()) = av;
		m->next();

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QListqreal(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value list = *(m->var());
	    if (mrb_type(list) != MRB_TT_ARRAY) {
		m->item().s_voidp = 0;
		break;
	    }
	    int count = RARRAY_LEN(list);
	    QList<qreal> *valuelist = new QList<qreal>;
	    long i;
	    for(i = 0; i < count; i++) {
		mrb_value item = mrb_ary_entry(list, i);
		if(mrb_type(item) != MRB_TT_FLOAT) {
		    valuelist->append(0.0);
		    continue;
		}
		valuelist->append(mrb_float(item));
	    }

	    m->item().s_voidp = valuelist;
	    m->next();

		if (!m->type().isConst()) {
			mrb_ary_clear(m->M, list);

			for (	QList<qreal>::iterator i = valuelist->begin();
					i != valuelist->end();
					++i )
			{
				mrb_ary_push(m->M, list, mrb_float_value(m->M, (qreal)*i));
			}
		}

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
      case Marshall::ToVALUE:
	{
	    QList<qreal> *valuelist = (QList<qreal>*)m->item().s_voidp;
	    if(!valuelist) {
        *(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value av = mrb_ary_new(m->M);

		for (	QList<qreal>::iterator i = valuelist->begin();
				i != valuelist->end();
				++i )
		{
      mrb_ary_push(m->M, av, mrb_float_value(m->M, (qreal)*i));
		}

	    *(m->var()) = av;
		m->next();

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QVectorqreal(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value list = *(m->var());

		list = mrb_check_array_type(m->M, *(m->var()));
		if (mrb_nil_p(list)) {
			m->item().s_voidp = 0;
			break;
		}

		int count = RARRAY_LEN(list);
		QVector<qreal> *valuelist = new QVector<qreal>;
		long i;
		for (i = 0; i < count; i++) {
			valuelist->append(mrb_float(mrb_ary_entry(list, i)));
		}

		m->item().s_voidp = valuelist;
		m->next();

		if (!m->type().isConst()) {
			mrb_ary_clear(m->M, list);

			for (	QVector<qreal>::iterator i = valuelist->begin();
					i != valuelist->end();
					++i )
			{
				mrb_ary_push(m->M, list, mrb_float_value(m->M, (qreal)*i));
			}
		}

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
	case Marshall::ToVALUE:
	{
	    QVector<qreal> *valuelist = (QVector<qreal>*)m->item().s_voidp;
	    if(!valuelist) {
        *(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value av = mrb_ary_new(m->M);

		for (	QVector<qreal>::iterator i = valuelist->begin();
				i != valuelist->end();
				++i )
		{
      mrb_ary_push(m->M, av, mrb_float_value(m->M, (qreal)*i));
		}

	    *(m->var()) = av;
		m->next();

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QVectorint(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value list = *(m->var());

		list = mrb_check_array_type(m->M, *(m->var()));
		if (mrb_nil_p(list)) {
			m->item().s_voidp = 0;
			break;
		}

		int count = RARRAY_LEN(list);
		QVector<int> *valuelist = new QVector<int>;
		long i;
		for (i = 0; i < count; i++) {
			valuelist->append(get_mrb_int(m->M, mrb_ary_entry(list, i)));
		}

		m->item().s_voidp = valuelist;
		m->next();

		if (!m->type().isConst()) {
			mrb_ary_clear(m->M, list);

			for (	QVector<int>::iterator i = valuelist->begin();
					i != valuelist->end();
					++i )
			{
				mrb_ary_push(m->M, list, mrb_fixnum_value((int)*i));
			}
		}

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
	case Marshall::ToVALUE:
	{
	    QVector<int> *valuelist = (QVector<int>*)m->item().s_voidp;
	    if(!valuelist) {
		*(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value av = mrb_ary_new(m->M);

		for (	QVector<int>::iterator i = valuelist->begin();
				i != valuelist->end();
				++i )
		{
      mrb_ary_push(m->M, av, mrb_fixnum_value((int)*i));
		}

	    *(m->var()) = av;
		m->next();

		if (m->cleanup()) {
			delete valuelist;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_voidP(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value rv = *(m->var());
	    if (not mrb_nil_p(rv))
		m->item().s_voidp = mrb_voidp(*(m->var()));
	    else
		m->item().s_voidp = 0;
	}
	break;
      case Marshall::ToVALUE:
	{
    *(m->var()) = mrb_voidp_value(m->M, m->item().s_voidp);
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QMapQStringQString(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value hash = *(m->var());
	    if (mrb_type(hash) != MRB_TT_HASH) {
		m->item().s_voidp = 0;
		break;
	    }

		QMap<QString,QString> * map = new QMap<QString,QString>;

		// Convert the ruby hash to an array of key/value arrays
		mrb_value temp = mrb_hash_keys(m->M, hash);

		for (long i = 0; i < RARRAY_LEN(temp); i++) {
			mrb_value key = RARRAY_PTR(temp)[i];
			(*map)[QString(mrb_string_value_ptr(m->M, key))] =
          QString(mrb_string_value_ptr(m->M, mrb_hash_get(m->M, hash, key)));
		}

		m->item().s_voidp = map;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      case Marshall::ToVALUE:
	{
	    QMap<QString,QString> *map = (QMap<QString,QString>*)m->item().s_voidp;
	    if(!map) {
		*(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value hv = mrb_hash_new(m->M);

		QMap<QString,QString>::Iterator it;
		for (it = map->begin(); it != map->end(); ++it) {
			mrb_hash_set(m->M, hv, rstringFromQString(m->M, (QString*)&(it.key())), rstringFromQString(m->M, (QString*) &(it.value())));
        }

		*(m->var()) = hv;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QMapQStringQVariant(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value hash = *(m->var());
		if (mrb_type(hash) != MRB_TT_HASH) {
			m->item().s_voidp = 0;
			break;
	    }

		QMap<QString,QVariant> * map = new QMap<QString,QVariant>;

		// Convert the ruby hash to an array of key/value arrays
		mrb_value temp = mrb_hash_keys(m->M, hash);

		for (long i = 0; i < RARRAY_LEN(temp); i++) {
			mrb_value key = RARRAY_PTR(temp)[i];
			mrb_value value = mrb_hash_get(m->M, hash, key);

			smokeruby_object *o = value_obj_info(m->M, value);
			if (!o || !o->ptr || o->classId != o->smoke->findClass("QVariant").index) {
				// If the value isn't a Qt::Variant, then try and construct
				// a Qt::Variant from it
				value = mrb_funcall(m->M, mrb_obj_value(qvariant_class(m->M)), "fromValue", 1, value);
				if (mrb_nil_p(value)) {
					continue;
				}
				o = value_obj_info(m->M, value);
			}

			(*map)[QString(mrb_string_value_ptr(m->M, key))] = (QVariant)*(QVariant*)o->ptr;
		}

		m->item().s_voidp = map;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      case Marshall::ToVALUE:
	{
	    QMap<QString,QVariant> *map = (QMap<QString,QVariant>*)m->item().s_voidp;
	    if(!map) {
		*(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value hv = mrb_hash_new(m->M);

		QMap<QString,QVariant>::Iterator it;
		for (it = map->begin(); it != map->end(); ++it) {
			void *p = new QVariant(it.value());
			mrb_value obj = getPointerObject(m->M, p);

			if (mrb_nil_p(obj)) {
				smokeruby_object  * o = alloc_smokeruby_object(	m->M, true,
																m->smoke(),
																m->smoke()->idClass("QVariant").index,
																p );
				obj = set_obj_info(m->M, "Qt::Variant", o);
			}

			mrb_hash_set(m->M, hv, rstringFromQString(m->M, (QString*)&(it.key())), obj);
        }

		*(m->var()) = hv;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QMapIntQVariant(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value hash = *(m->var());
		if (mrb_type(hash) != MRB_TT_HASH) {
			m->item().s_voidp = 0;
			break;
	    }

		QMap<int,QVariant> * map = new QMap<int,QVariant>;

		// Convert the ruby hash to an array of key/value arrays
		mrb_value temp = mrb_hash_keys(m->M, hash);

		for (long i = 0; i < RARRAY_LEN(temp); i++) {
			mrb_value key = RARRAY_PTR(temp)[i];
			mrb_value value = mrb_hash_get(m->M, hash, key);

			smokeruby_object *o = value_obj_info(m->M, value);
			if (!o || !o->ptr || o->classId != o->smoke->idClass("QVariant").index) {
				// If the value isn't a Qt::Variant, then try and construct
				// a Qt::Variant from it
				value = mrb_funcall(m->M, mrb_obj_value(qvariant_class(m->M)), "fromValue", 1, value);
				if (mrb_nil_p(value)) {
					continue;
				}
				o = value_obj_info(m->M, value);
			}

			(*map)[get_mrb_int(m->M, key)] = (QVariant)*(QVariant*)o->ptr;
		}

		m->item().s_voidp = map;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      case Marshall::ToVALUE:
	{
	    QMap<int,QVariant> *map = (QMap<int,QVariant>*)m->item().s_voidp;
		if (!map) {
			*(m->var()) = mrb_nil_value();
			break;
	    }

	    mrb_value hv = mrb_hash_new(m->M);

		QMap<int,QVariant>::Iterator it;
		for (it = map->begin(); it != map->end(); ++it) {
			void *p = new QVariant(it.value());
			mrb_value obj = getPointerObject(m->M, p);

			if (mrb_nil_p(obj)) {
				smokeruby_object  * o = alloc_smokeruby_object(	m->M, true,
																m->smoke(),
																m->smoke()->idClass("QVariant").index,
																p );
				obj = set_obj_info(m->M, "Qt::Variant", o);
			}

			mrb_hash_set(m->M, hv, mrb_fixnum_value(it.key()), obj);
        }

		*(m->var()) = hv;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_QMapintQVariant(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value hash = *(m->var());
	    if (mrb_type(hash) != MRB_TT_HASH) {
		m->item().s_voidp = 0;
		break;
	    }

		QMap<int,QVariant> * map = new QMap<int,QVariant>;

		// Convert the ruby hash to an array of key/value arrays
		mrb_value temp = mrb_hash_keys(m->M, hash);

		for (long i = 0; i < RARRAY_LEN(temp); i++) {
			mrb_value key = RARRAY_PTR(temp)[i];
			smokeruby_object *o = value_obj_info(m->M, mrb_hash_get(m->M, hash, key));
			if( !o || !o->ptr) { continue; }
			void * ptr = o->ptr;
			ptr = o->smoke->cast(ptr, o->classId, o->smoke->idClass("QVariant").index);

			(*map)[get_mrb_int(m->M, key)] = (QVariant)*(QVariant*)ptr;
		}

		m->item().s_voidp = map;
		m->next();

    if(m->cleanup()) { delete map; }
	}
	break;
      case Marshall::ToVALUE:
	{
	    QMap<int,QVariant> *map = (QMap<int,QVariant>*)m->item().s_voidp;
	    if(!map) {
		*(m->var()) = mrb_nil_value();
		break;
	    }

	    mrb_value hv = mrb_hash_new(m->M);

		QMap<int,QVariant>::Iterator it;
		for (it = map->begin(); it != map->end(); ++it) {
			void *p = new QVariant(it.value());
			mrb_value obj = getPointerObject(m->M, p);

			if (mrb_nil_p(obj)) {
				smokeruby_object  * o = alloc_smokeruby_object(	m->M, true,
																m->smoke(),
																m->smoke()->idClass("QVariant").index,
																p );
				obj = set_obj_info(m->M, "Qt::Variant", o);
			}

			mrb_hash_set(m->M, hv, mrb_fixnum_value((int)(it.key())), obj);
        }

		*(m->var()) = hv;
		m->next();

	    if(m->cleanup())
		delete map;
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

void marshall_voidP_array(Marshall *m) {
    switch(m->action()) {
	case Marshall::FromVALUE:
	{
	    mrb_value rv = *(m->var());
		if (not mrb_nil_p(rv)) {
			m->item().s_voidp = mrb_voidp(rv);
		} else {
			m->item().s_voidp = 0;
		}
	}
	break;
	case Marshall::ToVALUE:
	{
		mrb_value rv = mrb_voidp_value(m->M, m->item().s_voidp);
		*(m->var()) = rv;
	}
	break;
		default:
		m->unsupported();
	break;
    }
}

void marshall_QRgb_array(Marshall *m) {
    switch(m->action()) {
      case Marshall::FromVALUE:
	{
	    mrb_value list = *(m->var());
	    if (mrb_type(list) != MRB_TT_ARRAY) {
		m->item().s_voidp = 0;
		break;
	    }
	    int count = RARRAY_LEN(list);
	    QRgb *rgb = new QRgb[count + 2];
	    long i;
	    for(i = 0; i < count; i++) {
        rgb[i] = get_mrb_int(m->M, mrb_ary_entry(list, i));
	    }
	    m->item().s_voidp = rgb;
	    m->next();
	}
	break;
      case Marshall::ToVALUE:
	// Implement this with a tied array or something
      default:
	m->unsupported();
	break;
    }
}

void marshall_QPairQStringQStringList(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value list = *(m->var());
		if (mrb_type(list) != MRB_TT_ARRAY) {
			m->item().s_voidp = 0;
			break;
	    }

		QList<QPair<QString,QString> > * pairlist = new QList<QPair<QString,QString> >();
		int count = RARRAY_LEN(list);

		for (long i = 0; i < count; i++) {
			mrb_value item = mrb_ary_entry(list, i);
			if (mrb_type(item) != MRB_TT_ARRAY || RARRAY_LEN(item) != 2) {
				continue;
			}
			mrb_value s1 = mrb_ary_entry(item, 0);
			mrb_value s2 = mrb_ary_entry(item, 1);
			QPair<QString,QString> * qpair = new QPair<QString,QString>(*(qstringFromRString(m->M, s1)),*(qstringFromRString(m->M, s2)));
			pairlist->append(*qpair);
		}

		m->item().s_voidp = pairlist;
		m->next();

		if (m->cleanup()) {
			delete pairlist;
		}

		break;
	}

	case Marshall::ToVALUE:
	{
		QList<QPair<QString,QString> > *pairlist = static_cast<QList<QPair<QString,QString> > * >(m->item().s_voidp);
		if (pairlist == 0) {
			*(m->var()) = mrb_nil_value();
			break;
		}

		mrb_value av = mrb_ary_new(m->M);
		for (QList<QPair<QString,QString> >::Iterator it = pairlist->begin(); it != pairlist->end(); ++it) {
			QPair<QString,QString> * pair = &(*it);
			mrb_value rv1 = rstringFromQString(m->M, &(pair->first));
			mrb_value rv2 = rstringFromQString(m->M, &(pair->second));
			mrb_value pv = mrb_ary_new(m->M);
			mrb_ary_push(m->M, pv, rv1);
			mrb_ary_push(m->M, pv, rv2);
			mrb_ary_push(m->M, av, pv);
		}

		*(m->var()) = av;

		if (m->cleanup()) {
			delete pairlist;
		}

	}
	break;
	default:
		m->unsupported();
		break;
  }
}

void marshall_QPairqrealQColor(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE: {
		mrb_value list = *(m->var());
		if (mrb_type(list) != MRB_TT_ARRAY || RARRAY_LEN(list) != 2) {
			m->item().s_voidp = 0;
			break;
    }

		mrb_value item1 = mrb_ary_entry(list, 0);
		qreal real = mrb_float_p(item1)? mrb_float(item1) : 0;

		mrb_value item2 = mrb_ary_entry(list, 1);

		smokeruby_object *o = value_obj_info(m->M, item2);
		if (o == 0 || o->ptr == 0) {
			m->item().s_voidp = 0;
			break;
		}

		QPair<qreal,QColor> * qpair = new QPair<qreal,QColor>(real, *((QColor *) o->ptr));
		m->item().s_voidp = qpair;
		m->next();

		if (m->cleanup()) { delete qpair; }
	} break;
	case Marshall::ToVALUE: {
		QPair<qreal,QColor> * qpair = static_cast<QPair<qreal,QColor> * >(m->item().s_voidp);
		if (qpair == 0) {
			*(m->var()) = mrb_nil_value();
			break;
		}

		mrb_value rv1 = mrb_float_value(m->M, qpair->first);

		void *p = (void *) &(qpair->second);
		mrb_value rv2 = getPointerObject(m->M, p);
		if (mrb_nil_p(rv2)) {
			smokeruby_object  * o = alloc_smokeruby_object(
          m->M, false, m->smoke(), m->smoke()->idClass("QColor").index, p );
			rv2 = set_obj_info(m->M, "Qt::Color", o);
		}

		mrb_value av = mrb_ary_new_capa(m->M, 2);
		mrb_ary_push(m->M, av, rv1);
		mrb_ary_push(m->M, av, rv2);
		*(m->var()) = av;

		if (m->cleanup()) { /* delete qpair; */ }
	} break;
	default:
		m->unsupported();
		break;
    }
}

void marshall_QPairintint(Marshall *m) {
	switch(m->action()) {
    case Marshall::FromVALUE: {
      mrb_value list = *(m->var());
      if (mrb_type(list) != MRB_TT_ARRAY || RARRAY_LEN(list) != 2) {
        m->item().s_voidp = 0;
        break;
      }
      int int0;
      int int1;
      int0 = get_mrb_int(m->M, mrb_ary_entry(list, 0));
      int1 = get_mrb_int(m->M, mrb_ary_entry(list, 1));

      QPair<int,int> * qpair = new QPair<int,int>(int0,int1);
      m->item().s_voidp = qpair;
      m->next();

      if (m->cleanup()) { delete qpair; }
    } break;

    case Marshall::ToVALUE:
    default:
      m->unsupported();
      break;
  }
}

DEF_LIST_MARSHALLER( QAbstractButtonList, QList<QAbstractButton*>, QAbstractButton )
DEF_LIST_MARSHALLER( QActionGroupList, QList<QActionGroup*>, QActionGroup )
DEF_LIST_MARSHALLER( QActionList, QList<QAction*>, QAction )
DEF_LIST_MARSHALLER( QListWidgetItemList, QList<QListWidgetItem*>, QListWidgetItem )
DEF_LIST_MARSHALLER( QObjectList, QList<QObject*>, QObject )
DEF_LIST_MARSHALLER( QTableWidgetList, QList<QTableWidget*>, QTableWidget )
DEF_LIST_MARSHALLER( QTableWidgetItemList, QList<QTableWidgetItem*>, QTableWidgetItem )
DEF_LIST_MARSHALLER( QTextFrameList, QList<QTextFrame*>, QTextFrame )
DEF_LIST_MARSHALLER( QTreeWidgetItemList, QList<QTreeWidgetItem*>, QTreeWidgetItem )
DEF_LIST_MARSHALLER( QTreeWidgetList, QList<QTreeWidget*>, QTreeWidget )
DEF_LIST_MARSHALLER( QWidgetList, QList<QWidget*>, QWidget )
DEF_LIST_MARSHALLER( QWidgetPtrList, QList<QWidget*>, QWidget )

#if QT_VERSION >= 0x40200
DEF_LIST_MARSHALLER( QGraphicsItemList, QList<QGraphicsItem*>, QGraphicsItem )
DEF_LIST_MARSHALLER( QStandardItemList, QList<QStandardItem*>, QStandardItem )
DEF_LIST_MARSHALLER( QUndoStackList, QList<QUndoStack*>, QUndoStack )
#endif

#if QT_VERSION >= 0x40300
DEF_LIST_MARSHALLER( QMdiSubWindowList, QList<QMdiSubWindow*>, QMdiSubWindow )
#endif

DEF_VALUELIST_MARSHALLER( QColorVector, QVector<QColor>, QColor )
DEF_VALUELIST_MARSHALLER( QFileInfoList, QFileInfoList, QFileInfo )
DEF_VALUELIST_MARSHALLER( QHostAddressList, QList<QHostAddress>, QHostAddress )
DEF_VALUELIST_MARSHALLER( QImageTextKeyLangList, QList<QImageTextKeyLang>, QImageTextKeyLang )
DEF_VALUELIST_MARSHALLER( QKeySequenceList, QList<QKeySequence>, QKeySequence )
DEF_VALUELIST_MARSHALLER( QLineFVector, QVector<QLineF>, QLineF )
DEF_VALUELIST_MARSHALLER( QLineVector, QVector<QLine>, QLine )
DEF_VALUELIST_MARSHALLER( QModelIndexList, QList<QModelIndex>, QModelIndex )
DEF_VALUELIST_MARSHALLER( QNetworkAddressEntryList, QList<QNetworkAddressEntry>, QNetworkAddressEntry )
DEF_VALUELIST_MARSHALLER( QNetworkInterfaceList, QList<QNetworkInterface>, QNetworkInterface )
DEF_VALUELIST_MARSHALLER( QPixmapList, QList<QPixmap>, QPixmap )
DEF_VALUELIST_MARSHALLER( QPointFVector, QVector<QPointF>, QPointF )
DEF_VALUELIST_MARSHALLER( QPointVector, QVector<QPoint>, QPoint )
DEF_VALUELIST_MARSHALLER( QPolygonFList, QList<QPolygonF>, QPolygonF )
DEF_VALUELIST_MARSHALLER( QRectFList, QList<QRectF>, QRectF )
DEF_VALUELIST_MARSHALLER( QRectFVector, QVector<QRectF>, QRectF )
DEF_VALUELIST_MARSHALLER( QRectVector, QVector<QRect>, QRect )
DEF_VALUELIST_MARSHALLER( QRgbVector, QVector<QRgb>, QRgb )
DEF_VALUELIST_MARSHALLER( QTableWidgetSelectionRangeList, QList<QTableWidgetSelectionRange>, QTableWidgetSelectionRange )
DEF_VALUELIST_MARSHALLER( QTextBlockList, QList<QTextBlock>, QTextBlock )
DEF_VALUELIST_MARSHALLER( QTextEditExtraSelectionsList, QList<QTextEdit::ExtraSelection>, QTextEdit::ExtraSelection )
DEF_VALUELIST_MARSHALLER( QTextFormatVector, QVector<QTextFormat>, QTextFormat )
DEF_VALUELIST_MARSHALLER( QTextLayoutFormatRangeList, QList<QTextLayout::FormatRange>, QTextLayout::FormatRange)
DEF_VALUELIST_MARSHALLER( QTextLengthVector, QVector<QTextLength>, QTextLength )
DEF_VALUELIST_MARSHALLER( QUrlList, QList<QUrl>, QUrl )
DEF_VALUELIST_MARSHALLER( QVariantList, QList<QVariant>, QVariant )
DEF_VALUELIST_MARSHALLER( QVariantVector, QVector<QVariant>, QVariant )

#if QT_VERSION >= 0x40300
DEF_VALUELIST_MARSHALLER( QSslCertificateList, QList<QSslCertificate>, QSslCertificate )
DEF_VALUELIST_MARSHALLER( QSslCipherList, QList<QSslCipher>, QSslCipher )
DEF_VALUELIST_MARSHALLER( QSslErrorList, QList<QSslError>, QSslError )
DEF_VALUELIST_MARSHALLER( QXmlStreamEntityDeclarations, QVector<QXmlStreamEntityDeclaration>, QXmlStreamEntityDeclaration )
DEF_VALUELIST_MARSHALLER( QXmlStreamNamespaceDeclarations, QVector<QXmlStreamNamespaceDeclaration>, QXmlStreamNamespaceDeclaration )
DEF_VALUELIST_MARSHALLER( QXmlStreamNotationDeclarations, QVector<QXmlStreamNotationDeclaration>, QXmlStreamNotationDeclaration )
#endif

#if QT_VERSION >= 0x40400
DEF_VALUELIST_MARSHALLER( QNetworkCookieList, QList<QNetworkCookie>, QNetworkCookie )
DEF_VALUELIST_MARSHALLER( QPrinterInfoList, QList<QPrinterInfo>, QPrinterInfo )
#endif

Q_DECL_EXPORT TypeHandler Qt_handlers[] = {
    { "bool*", marshall_it<bool *> },
    { "bool&", marshall_it<bool *> },
    { "char**", marshall_charP_array },
    { "char*",marshall_it<char *> },
    { "DOM::DOMTimeStamp", marshall_it<long long> },
    { "double*", marshall_doubleR },
    { "double&", marshall_doubleR },
    { "int*", marshall_it<int *> },
    { "int&", marshall_it<int *> },
    { "KIO::filesize_t", marshall_it<long long> },
    { "long long", marshall_it<long long> },
    { "long long int", marshall_it<long long> },
    { "long long int&", marshall_it<long long> },
    { "QDBusVariant", marshall_QDBusVariant },
    { "QDBusVariant&", marshall_QDBusVariant },
    { "QList<QFileInfo>", marshall_QFileInfoList },
    { "QFileInfoList", marshall_QFileInfoList },
    { "QGradiantStops", marshall_QPairqrealQColor },
    { "QGradiantStops&", marshall_QPairqrealQColor },
    { "unsigned int&", marshall_it<unsigned int *> },
    { "quint32&", marshall_it<unsigned int *> },
    { "uint&", marshall_it<unsigned int *> },
    { "qint32&", marshall_it<int *> },
    { "qint64", marshall_it<long long> },
    { "qint64&", marshall_it<long long> },
    { "QList<const char*>", marshall_QListCharStar },
    { "QList<int>", marshall_QListInt },
    { "QList<int>&", marshall_QListInt },
    { "QList<uint>", marshall_QListUInt },
    { "QList<uint>&", marshall_QListUInt },
    { "QList<QAbstractButton*>", marshall_QAbstractButtonList },
    { "QList<QActionGroup*>", marshall_QActionGroupList },
    { "QList<QAction*>", marshall_QActionList },
    { "QList<QAction*>&", marshall_QActionList },
    { "QList<QByteArray>", marshall_QByteArrayList },
    { "QList<QByteArray>*", marshall_QByteArrayList },
    { "QList<QByteArray>&", marshall_QByteArrayList },
    { "QList<QHostAddress>", marshall_QHostAddressList },
    { "QList<QHostAddress>&", marshall_QHostAddressList },
    { "QList<QImageTextKeyLang>", marshall_QImageTextKeyLangList },
    { "QList<QKeySequence>", marshall_QKeySequenceList },
    { "QList<QKeySequence>&", marshall_QKeySequenceList },
    { "QList<QListWidgetItem*>", marshall_QListWidgetItemList },
    { "QList<QListWidgetItem*>&", marshall_QListWidgetItemList },
    { "QList<QModelIndex>", marshall_QModelIndexList },
    { "QList<QModelIndex>&", marshall_QModelIndexList },
    { "QList<QNetworkAddressEntry>", marshall_QNetworkAddressEntryList },
    { "QList<QNetworkInterface>", marshall_QNetworkInterfaceList },
    { "QList<QPair<QString,QString> >", marshall_QPairQStringQStringList },
    { "QList<QPair<QString,QString> >&", marshall_QPairQStringQStringList },
    { "QList<QPixmap>", marshall_QPixmapList },
    { "QList<QPolygonF>", marshall_QPolygonFList },
    { "QList<QRectF>", marshall_QRectFList },
    { "QList<QRectF>&", marshall_QRectFList },
    { "QList<qreal>", marshall_QListqreal },
    { "QList<double>", marshall_QListqreal },
    { "QwtValueList", marshall_QListqreal },
    { "QwtValueList&", marshall_QListqreal },
    { "QList<double>&", marshall_QListqreal },
    { "QList<QObject*>", marshall_QObjectList },
    { "QList<QObject*>&", marshall_QObjectList },
    { "QList<QTableWidgetItem*>", marshall_QTableWidgetItemList },
    { "QList<QTableWidgetItem*>&", marshall_QTableWidgetItemList },
    { "QList<QTableWidgetSelectionRange>", marshall_QTableWidgetSelectionRangeList },
    { "QList<QTextBlock>", marshall_QTextBlockList },
    { "QList<QTextEdit::ExtraSelection>", marshall_QTextEditExtraSelectionsList },
    { "QList<QTextEdit::ExtraSelection>&", marshall_QTextEditExtraSelectionsList },
    { "QList<QTextFrame*>", marshall_QTextFrameList },
    { "QList<QTextLayout::FormatRange>", marshall_QTextLayoutFormatRangeList },
    { "QList<QTextLayout::FormatRange>&", marshall_QTextLayoutFormatRangeList },
    { "QList<QTreeWidgetItem*>", marshall_QTreeWidgetItemList },
    { "QList<QTreeWidgetItem*>&", marshall_QTreeWidgetItemList },
    { "QList<QUndoStack*>", marshall_QUndoStackList },
    { "QList<QUndoStack*>&", marshall_QUndoStackList },
    { "QList<QUrl>", marshall_QUrlList },
    { "QList<QUrl>&", marshall_QUrlList },
    { "QList<QVariant>", marshall_QVariantList },
    { "QList<QVariant>&", marshall_QVariantList },
    { "QList<QWidget*>", marshall_QWidgetPtrList },
    { "QList<QWidget*>&", marshall_QWidgetPtrList },
    { "qlonglong", marshall_it<long long> },
    { "qlonglong&", marshall_it<long long> },
    { "QMap<int,QVariant>", marshall_QMapintQVariant },
    { "QMap<int,QVariant>", marshall_QMapIntQVariant },
    { "QMap<int,QVariant>&", marshall_QMapIntQVariant },
    { "QMap<QString,QString>", marshall_QMapQStringQString },
    { "QMap<QString,QString>&", marshall_QMapQStringQString },
    { "QMap<QString,QVariant>", marshall_QMapQStringQVariant },
    { "QMap<QString,QVariant>&", marshall_QMapQStringQVariant },
    { "QVariantMap", marshall_QMapQStringQVariant },
    { "QVariantMap&", marshall_QMapQStringQVariant },
    { "QModelIndexList", marshall_QModelIndexList },
    { "QModelIndexList&", marshall_QModelIndexList },
    { "QObjectList", marshall_QObjectList },
    { "QObjectList&", marshall_QObjectList },
    { "QPair<int,int>&", marshall_QPairintint },
    { "Q_PID", marshall_it<Q_PID> },
    { "qreal*", marshall_doubleR },
    { "qreal&", marshall_doubleR },
    { "QRgb*", marshall_QRgb_array },
    { "QStringList", marshall_QStringList },
    { "QStringList*", marshall_QStringList },
    { "QStringList&", marshall_QStringList },
    { "QString", marshall_QString },
    { "QString*", marshall_QString },
    { "QString&", marshall_QString },
    { "QByteArray", marshall_QByteArray },
    { "QByteArray*", marshall_QByteArray },
    { "QByteArray&", marshall_QByteArray },
    { "quint64", marshall_it<unsigned long long> },
    { "quint64&", marshall_it<unsigned long long> },
    { "qulonglong", marshall_it<unsigned long long> },
    { "qulonglong&", marshall_it<unsigned long long> },
    { "QVariantList&", marshall_QVariantList },
    { "QVector<int>", marshall_QVectorint },
    { "QVector<int>&", marshall_QVectorint },
    { "QVector<QColor>", marshall_QColorVector },
    { "QVector<QColor>&", marshall_QColorVector },
    { "QVector<QLineF>", marshall_QLineFVector },
    { "QVector<QLineF>&", marshall_QLineFVector },
    { "QVector<QLine>", marshall_QLineVector },
    { "QVector<QLine>&", marshall_QLineVector },
    { "QVector<QPointF>", marshall_QPointFVector },
    { "QVector<QPointF>&", marshall_QPointFVector },
    { "QVector<QPoint>", marshall_QPointVector },
    { "QVector<QPoint>&", marshall_QPointVector },
    { "QVector<qreal>", marshall_QVectorqreal },
    { "QVector<qreal>&", marshall_QVectorqreal },
    { "QVector<double>", marshall_QVectorqreal },
    { "QVector<double>&", marshall_QVectorqreal },
    { "QVector<QRectF>", marshall_QRectFVector },
    { "QVector<QRectF>&", marshall_QRectFVector },
    { "QVector<QRect>", marshall_QRectVector },
    { "QVector<QRect>&", marshall_QRectVector },
    { "QVector<QRgb>", marshall_QRgbVector },
    { "QVector<QRgb>&", marshall_QRgbVector },
    { "QVector<QTextFormat>", marshall_QTextFormatVector },
    { "QVector<QTextFormat>&", marshall_QTextFormatVector },
    { "QVector<QTextLength>", marshall_QTextLengthVector },
    { "QVector<QTextLength>&", marshall_QTextLengthVector },
    { "QVector<QVariant>", marshall_QVariantVector },
    { "QVector<QVariant>&", marshall_QVariantVector },
    { "QWidgetList", marshall_QWidgetList },
    { "QWidgetList&", marshall_QWidgetList },
    { "QwtArray<double>", marshall_QVectorqreal },
    { "QwtArray<double>&", marshall_QVectorqreal },
    { "QwtArray<int>", marshall_QVectorint },
    { "QwtArray<int>&", marshall_QVectorint },
    { "signed int&", marshall_it<int *> },
    { "uchar*", marshall_ucharP },
    { "unsigned char*", marshall_ucharP },
    { "unsigned long long int", marshall_it<long long> },
    { "unsigned long long int&", marshall_it<long long> },
    { "void", marshall_void },
    { "void**", marshall_voidP_array },
    { "WId", marshall_it<WId> },
    { "HBITMAP__*", marshall_voidP },
    { "HDC__*", marshall_voidP },
    { "HFONT__*", marshall_voidP },
    { "HICON__*", marshall_voidP },
    { "HINSTANCE__*", marshall_voidP },
    { "HPALETTE__*", marshall_voidP },
    { "HRGN__*", marshall_voidP },
    { "HWND__*", marshall_voidP },
    { "QFlags&", marshall_it<int *> },
#if QT_VERSION >= 0x40200
    { "QList<QGraphicsItem*>", marshall_QGraphicsItemList },
    { "QList<QGraphicsItem*>&", marshall_QGraphicsItemList },
    { "QList<QStandardItem*>", marshall_QStandardItemList },
    { "QList<QStandardItem*>&", marshall_QStandardItemList },
    { "QList<QUndoStack*>", marshall_QUndoStackList },
    { "QList<QUndoStack*>&", marshall_QUndoStackList },
#endif
#if QT_VERSION >= 0x40300
    { "QList<QMdiSubWindow*>", marshall_QMdiSubWindowList },
    { "QList<QSslCertificate>", marshall_QSslCertificateList },
    { "QList<QSslCertificate>&", marshall_QSslCertificateList },
    { "QList<QSslCipher>", marshall_QSslCipherList },
    { "QList<QSslCipher>&", marshall_QSslCipherList },
    { "QList<QSslError>", marshall_QSslErrorList },
    { "QList<QSslError>&", marshall_QSslErrorList },
    { "QXmlStreamEntityDeclarations", marshall_QXmlStreamEntityDeclarations },
    { "QXmlStreamNamespaceDeclarations", marshall_QXmlStreamNamespaceDeclarations },
    { "QXmlStreamNotationDeclarations", marshall_QXmlStreamNotationDeclarations },
#endif
#if QT_VERSION >= 0x040400
    { "QList<QNetworkCookie>", marshall_QNetworkCookieList },
    { "QList<QNetworkCookie>&", marshall_QNetworkCookieList },
    { "QList<QPrinterInfo>", marshall_QPrinterInfoList },
#endif
    { 0, 0 }
};

QHash<QByteArray, TypeHandler*> type_handlers;

void install_handlers(TypeHandler *h) {
	while(h->name) {
		type_handlers.insert(h->name, h);
		h++;
	}
}

Marshall::HandlerFn getMarshallFn(const SmokeType &type) {
	if (type.elem()) { return marshall_basetype; }
	if (!type.name()) { return marshall_void; }

	TypeHandler *h = type_handlers[type.name()];

	if (h == 0 && type.isConst() && strlen(type.name()) > strlen("const ")) {
		h = type_handlers[type.name() + strlen("const ")];
	}

  char last_char = type.name()[strlen(type.name()) - 1];
  if (h == 0 && strncmp(type.name(), "QFlags", 6) == 0 && last_char == '&') {
    h = type_handlers["QFlags&"];
  }
	if (h) { return h->fn; }

	return marshall_unknown;
}
