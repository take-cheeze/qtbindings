/***************************************************************************
  marshall_macros.h  -  Useful template based marshallers for QLists, QVectors
                        and QLinkedLists
                             -------------------
    begin                : Thurs Jun 8 2008
    copyright            : (C) 2008 by Richard Dale
    email                : richard.j.dale@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#ifndef MARSHALL_MACROS_H
#define MARSHALL_MACROS_H

#include <QtCore/qlist.h>
#include <QtCore/qlinkedlist.h>
#include <QtCore/qvector.h>
#include <QtCore/qhash.h>
#include <QtCore/qmap.h>

#include "qtruby.h"
#include "marshall.h"

#include <mruby/array.h>
#include <mruby/string.h>
#include <mruby/hash.h>

#define DEF_HASH_MARSHALLER(HashIdent,Item) namespace { char HashIdent##STR[] = #Item; }  \
        Marshall::HandlerFn marshall_##HashIdent = marshall_Hash<Item,HashIdent##STR>;

#define DEF_MAP_MARSHALLER(MapIdent,Item) namespace { char MapIdent##STR[] = #Item; }  \
        Marshall::HandlerFn marshall_##MapIdent = marshall_Map<Item,MapIdent##STR>;

#define DEF_LIST_MARSHALLER(ListIdent,ItemList,Item) namespace { char ListIdent##STR[] = #Item; }  \
        Marshall::HandlerFn marshall_##ListIdent = marshall_ItemList<Item,ItemList,ListIdent##STR>;

#define DEF_VALUELIST_MARSHALLER(ListIdent,ItemList,Item) namespace { char ListIdent##STR[] = #Item; }  \
        Marshall::HandlerFn marshall_##ListIdent = marshall_ValueListItem<Item,ItemList,ListIdent##STR>;

#define DEF_LINKED_LIST_MARSHALLER(ListIdent,ItemList,Item) namespace { char ListIdent##STR[] = #Item; }  \
        Marshall::HandlerFn marshall_##ListIdent = marshall_LinkedItemList<Item,ItemList,ListIdent##STR>;

#define DEF_LINKED_VALUELIST_MARSHALLER(ListIdent,ItemList,Item) namespace { char ListIdent##STR[] = #Item; }  \
        Marshall::HandlerFn marshall_##ListIdent = marshall_LinkedValueListItem<Item,ItemList,ListIdent##STR>;

template <class Item, class ItemList, const char *ItemSTR >
void marshall_ItemList(Marshall *m) {
	switch(m->action()) {
		case Marshall::FromVALUE:
		{
			mrb_value list = *(m->var());
			if (mrb_type(list) != MRB_TT_ARRAY) {
				m->item().s_voidp = 0;
				break;
			}

			int count = RARRAY_LEN(list);
			ItemList *cpplist = new ItemList;
			long i;
			for(i = 0; i < count; i++) {
				mrb_value item = mrb_ary_entry(list, i);
				// TODO do type checking!
				smokeruby_object *o = value_obj_info(m->M, item);
				if(!o || !o->ptr)
					continue;
				void *ptr = o->ptr;
				ptr = o->smoke->cast(
					ptr,				// pointer
					o->classId,				// from
		    		o->smoke->idClass(ItemSTR, true).index	// to
				);
				cpplist->append((Item*)ptr);
			}

			m->item().s_voidp = cpplist;
			m->next();

			if (!m->type().isConst()) {
				mrb_ary_clear(m->M, list);

				for(int i = 0; i < cpplist->size(); ++i ) {
					mrb_value obj = getPointerObject(m->M, (void *) cpplist->at(i) );
					mrb_ary_push(m->M, list, obj);
				}
			}

			if (m->cleanup()) {
				delete cpplist;
			}
		}
		break;

		case Marshall::ToVALUE:
		{
			ItemList * cpplist = (ItemList *) m->item().s_voidp;
			if (cpplist == 0) {
				*(m->var()) = mrb_nil_value();
				break;
			}

			mrb_value av = mrb_ary_new(m->M);

            Smoke::ModuleIndex mi = Smoke::findClass(ItemSTR);

			for (int i=0; i < cpplist->size(); ++i) {
				void *p = (void *) cpplist->at(i);

				if (m->item().s_voidp == 0) {
					*(m->var()) = mrb_nil_value();
					break;
				}

				mrb_value obj = getPointerObject(m->M, p);
				if (mrb_nil_p(obj)) {
					smokeruby_object  * o = alloc_smokeruby_object(m->M,	false,
																	mi.smoke,
																	mi.index,
																	p );

					obj = set_obj_info(m->M, resolve_classname(o), o);
				}

				mrb_ary_push(m->M, av, obj);
			}

			*(m->var()) = av;
			m->next();

			if (!m->type().isConst()) {
			  int count = RARRAY_LEN(av);
			  long i;
			  cpplist->clear();
			  for (i = 0; i < count; i++) {
				  mrb_value item = mrb_ary_entry(av, i);
				  // TODO do type checking!
				  smokeruby_object *o = value_obj_info(m->M, item);
				  if(!o || !o->ptr)
					  continue;
				  void *ptr = o->ptr;
				  ptr = o->smoke->cast(
					  ptr,				// pointer
					  o->classId,				// from
					  o->smoke->idClass(ItemSTR, true).index	// to
				  );

				  cpplist->append((Item*)ptr);
			  }
			}

			if (m->cleanup()) {
				delete cpplist;
			}
		}
		break;

		default:
			m->unsupported();
		break;
   }
}

template <class Item, class ItemList, const char *ItemSTR >
void marshall_ValueListItem(Marshall *m) {
	switch(m->action()) {
		case Marshall::FromVALUE:
		{
			mrb_value list = *(m->var());
			if (mrb_type(list) != MRB_TT_ARRAY) {
				m->item().s_voidp = 0;
				break;
			}
			int count = RARRAY_LEN(list);
			ItemList *cpplist = new ItemList;
			long i;
			for(i = 0; i < count; i++) {
				mrb_value item = mrb_ary_entry(list, i);
				// TODO do type checking!
				smokeruby_object *o = value_obj_info(m->M, item);

				// Special case for the QList<QVariant> type
				if (	qstrcmp(ItemSTR, "QVariant") == 0
						&& (!o || !o->ptr || o->classId != o->smoke->idClass("QVariant", true).index) )
				{
					// If the value isn't a Qt::Variant, then try and construct
					// a Qt::Variant from it
					item = mrb_funcall(m->M, mrb_obj_value(qvariant_class(m->M)), "fromValue", 1, item);
					if (mrb_nil_p(item)) {
						continue;
					}
					o = value_obj_info(m->M, item);
				}

				if (!o || !o->ptr)
					continue;

				void *ptr = o->ptr;
				ptr = o->smoke->cast(
					ptr,				// pointer
					o->classId,				// from
					o->smoke->idClass(ItemSTR, true).index	        // to
				);
				cpplist->append(*(Item*)ptr);
			}

			m->item().s_voidp = cpplist;
			m->next();

			if (!m->type().isConst()) {
				mrb_ary_clear(m->M, list);
				for(int i=0; i < cpplist->size(); ++i) {
					mrb_value obj = getPointerObject(m->M, (void*)&(cpplist->at(i)));
					mrb_ary_push(m->M, list, obj);
				}
			}

			if (m->cleanup()) {
				delete cpplist;
			}
		}
		break;

		case Marshall::ToVALUE:
		{
			ItemList *valuelist = (ItemList*)m->item().s_voidp;
			if(!valuelist) {
				*(m->var()) = mrb_nil_value();
				break;
			}

			mrb_value av = mrb_ary_new(m->M);

			Smoke::ModuleIndex mi = Smoke::findClass(ItemSTR);
			const char * className = qtruby_modules[mi.smoke].binding->className(mi.index);

			for(int i=0; i < valuelist->size() ; ++i) {
				void *p = (void *) &(valuelist->at(i));

				if(m->item().s_voidp == 0) {
				*(m->var()) = mrb_nil_value();
				break;
				}

				mrb_value obj = getPointerObject(m->M, p);
				if(mrb_nil_p(obj)) {
					smokeruby_object  * o = alloc_smokeruby_object(	m->M, false,
																	mi.smoke,
																	mi.index,
																	p );
					obj = set_obj_info(m->M, className, o);
				}

				mrb_ary_push(m->M, av, obj);
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

/*
	The code for the QLinkedList marshallers is identical to the QList and QVector marshallers apart
	from the use of iterators instead of at(), and so it really should be possible to code one marshaller
	to work with all three types.
*/

template <class Item, class ItemList, const char *ItemSTR >
void marshall_LinkedItemList(Marshall *m) {
	switch(m->action()) {
		case Marshall::FromVALUE:
		{
			mrb_value list = *(m->var());
			if (mrb_type(list) != MRB_TT_ARRAY) {
				m->item().s_voidp = 0;
				break;
			}

			int count = RARRAY_LEN(list);
			ItemList *cpplist = new ItemList;
			long i;
			for (i = 0; i < count; i++) {
				mrb_value item = mrb_ary_entry(list, i);
				// TODO do type checking!
				smokeruby_object *o = value_obj_info(m->M, item);
				if (o == 0 || o->ptr == 0)
					continue;
				void *ptr = o->ptr;
				ptr = o->smoke->cast(
					ptr,				// pointer
					o->classId,				// from
		    		o->smoke->idClass(ItemSTR, true).index	// to
				);
				cpplist->append((Item*)ptr);
			}

			m->item().s_voidp = cpplist;
			m->next();

			if (!m->type().isConst()) {
				mrb_ary_clear(m->M, list);

				QLinkedListIterator<Item*> iter(*cpplist);
				while (iter.hasNext()) {
					mrb_value obj = getPointerObject(m->M, (void *) iter.next());
					mrb_ary_push(m->M, list, obj);
				}
			}

			if (m->cleanup()) {
				delete cpplist;
			}
		}
		break;

		case Marshall::ToVALUE:
		{
			ItemList *valuelist = (ItemList*)m->item().s_voidp;
			if (valuelist == 0) {
				*(m->var()) = mrb_nil_value();
				break;
			}

			mrb_value av = mrb_ary_new(m->M);

            Smoke::ModuleIndex mi = Smoke::findClass(ItemSTR);

			QLinkedListIterator<Item*> iter(*valuelist);
			while (iter.hasNext()) {
				void * p = (void *) iter.next();

				if (m->item().s_voidp == 0) {
					*(m->var()) = mrb_nil_value();
					break;
				}

				mrb_value obj = getPointerObject(m->M, p);
				if (mrb_nil_p(obj)) {
					smokeruby_object  * o = alloc_smokeruby_object(	m->M, false,
																	mi.smoke,
																	mi.index,
																	p );

					obj = set_obj_info(m->M, resolve_classname(o), o);
				}

				mrb_ary_push(m->M, av, obj);
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

template <class Item, class ItemList, const char *ItemSTR >
void marshall_LinkedValueListItem(Marshall *m) {
	switch(m->action()) {
		case Marshall::FromVALUE:
		{
			mrb_value list = *(m->var());
			if (mrb_type(list) != MRB_TT_ARRAY) {
				m->item().s_voidp = 0;
				break;
			}
			int count = RARRAY_LEN(list);
			ItemList *cpplist = new ItemList;
			long i;
			for(i = 0; i < count; i++) {
				mrb_value item = mrb_ary_entry(list, i);
				// TODO do type checking!
				smokeruby_object *o = value_obj_info(m->M, item);

				// Special case for the QList<QVariant> type
				if (	qstrcmp(ItemSTR, "QVariant") == 0
						&& (o == 0 || o->ptr == 0 || o->classId != o->smoke->idClass("QVariant", true).index) )
				{
					// If the value isn't a Qt::Variant, then try and construct
					// a Qt::Variant from it
					item = mrb_funcall(m->M, mrb_obj_value(qvariant_class(m->M)), "fromValue", 1, item);
					if (mrb_nil_p(item)) {
						continue;
					}
					o = value_obj_info(m->M, item);
				}

				if (o == 0 || o->ptr == 0)
					continue;

				void *ptr = o->ptr;
				ptr = o->smoke->cast(
					ptr,				// pointer
					o->classId,				// from
					o->smoke->idClass(ItemSTR, true).index	        // to
				);
				cpplist->append(*(Item*)ptr);
			}

			m->item().s_voidp = cpplist;
			m->next();

			if (!m->type().isConst()) {
				mrb_ary_clear(m->M, list);

				QLinkedListIterator<Item> iter(*cpplist);
				while (iter.hasNext()) {
					mrb_value obj = getPointerObject(m->M, (void*)&(iter.next()));
					mrb_ary_push(m->M, list, obj);
				}
			}

			if (m->cleanup()) {
				delete cpplist;
			}
		}
		break;

		case Marshall::ToVALUE:
		{
			ItemList *valuelist = (ItemList*)m->item().s_voidp;
			if (valuelist == 0) {
				*(m->var()) = mrb_nil_value();
				break;
			}

			mrb_value av = mrb_ary_new(m->M);

			Smoke::ModuleIndex mi = Smoke::findClass(ItemSTR);
			const char * className = qtruby_modules[mi.smoke].binding->className(mi.index);

			QLinkedListIterator<Item> iter(*valuelist);
			while (iter.hasNext()) {
				void * p = (void*) &(iter.next());

				if(m->item().s_voidp == 0) {
					*(m->var()) = mrb_nil_value();
					break;
				}

				mrb_value obj = getPointerObject(m->M, p);
				if(mrb_nil_p(obj)) {
					smokeruby_object  * o = alloc_smokeruby_object(	m->M, false,
																	mi.smoke,
																	mi.index,
																	p );
					obj = set_obj_info(m->M, className, o);
				}

				mrb_ary_push(m->M, av, obj);
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

template <class Value, const char *ValueSTR >
void marshall_Hash(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value hv = *(m->var());
		if (mrb_type(hv) != MRB_TT_HASH) {
			m->item().s_voidp = 0;
			break;
		}

		QHash<QString, Value*> * hash = new QHash<QString, Value*>;

		// Convert the ruby hash to an array of key/value arrays
		mrb_value temp = mrb_hash_keys(m->M, hv);

		for (long i = 0; i < RARRAY_LEN(temp); i++) {
			mrb_value key = RARRAY_PTR(temp)[i];
			smokeruby_object *o = value_obj_info(m->M, mrb_hash_get(m->M, hv, key));
			if( !o || !o->ptr)
				continue;
			void * val_ptr = o->ptr;
			val_ptr = o->smoke->cast(val_ptr, o->classId, o->smoke->idClass(ValueSTR, true).index);

			(*hash)[QString(mrb_string_value_ptr(m->M, key))] = (Value*)val_ptr;
		}

		m->item().s_voidp = hash;
		m->next();

		if (m->cleanup())
			delete hash;
	}
	break;
	case Marshall::ToVALUE:
	{
		QHash<QString, Value*> *hash = (QHash<QString, Value*>*) m->item().s_voidp;
		if (hash == 0) {
			*(m->var()) = mrb_nil_value();
			break;
	    }

		mrb_value hv = mrb_hash_new(m->M);

		Smoke::ModuleIndex val_mi = Smoke::findClass(ValueSTR);
	    const char * val_className = qtruby_modules[val_mi.smoke].binding->className(val_mi.index);

		for (QHashIterator<QString, Value*> it(*hash); it.hasNext(); it.next()) {
			void *val_p = it.value();
			mrb_value value_obj = getPointerObject(m->M, val_p);

			if (mrb_nil_p(value_obj)) {
				smokeruby_object *o = (smokeruby_object*)mrb_malloc(m->M, sizeof(smokeruby_object));
				o->classId = val_mi.index;
				o->smoke = val_mi.smoke;
				o->ptr = val_p;
				o->allocated = false;
				value_obj = set_obj_info(m->M, val_className, o);
			}
			mrb_hash_set(m->M, hv, mrb_str_new_cstr(m->M, ((QString*)&(it.key()))->toLatin1()), value_obj);
        }

		*(m->var()) = hv;
		m->next();

		if (m->cleanup())
			delete hash;
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

template <class Value, const char *ValueSTR >
void marshall_Map(Marshall *m) {
	switch(m->action()) {
	case Marshall::FromVALUE:
	{
		mrb_value hv = *(m->var());
		if (mrb_type(hv) != MRB_TT_HASH) {
			m->item().s_voidp = 0;
			break;
		}

		QMap<QString, Value> * map = new QMap<QString, Value>;

		// Convert the ruby hash to an array of key/value arrays
		mrb_value temp = mrb_hash_keys(m->M, hv);

		for (long i = 0; i < RARRAY_LEN(temp); i++) {
			mrb_value key = RARRAY_PTR(temp)[i];
			smokeruby_object *o = value_obj_info(m->M, mrb_hash_get(m->M, hv, key));
			if (o == 0 || o->ptr == 0) {
				continue;
			}
			void * val_ptr = o->ptr;
			val_ptr = o->smoke->cast(val_ptr, o->classId, o->smoke->idClass(ValueSTR, true).index);

			(*map)[QString(mrb_string_value_ptr(m->M, key))] = *((Value*)val_ptr);
		}

		m->item().s_voidp = map;
		m->next();

		if (m->cleanup()) {
			delete map;
		}
	}
	break;
	case Marshall::ToVALUE:
	{
		QMap<QString, Value> *map = (QMap<QString, Value>*) m->item().s_voidp;
		if (map == 0) {
			*(m->var()) = mrb_nil_value();
			break;
		}

		mrb_value hv = mrb_hash_new(m->M);

        Smoke::ModuleIndex val_mi = Smoke::findClass(ValueSTR);
        const char * val_className = qtruby_modules[val_mi.smoke].binding->className(val_mi.index);
		QMapIterator<QString, Value> it(*map);
		while (it.hasNext()) {
			it.next();
			void *val_p = (void *) &(it.value());
			mrb_value value_obj = getPointerObject(m->M, val_p);

			if (mrb_nil_p(value_obj)) {
				smokeruby_object *o = (smokeruby_object*)mrb_malloc(m->M, sizeof(smokeruby_object));
				o->classId = val_mi.index;
				o->smoke = val_mi.smoke;
				o->ptr = val_p;
				o->allocated = false;
				value_obj = set_obj_info(m->M, val_className, o);
			}
			mrb_hash_set(m->M, hv, mrb_str_new_cstr(m->M, ((QString*)&(it.key()))->toLatin1()), value_obj);
        }

		*(m->var()) = hv;
		m->next();

		if (m->cleanup()) {
			delete map;
		}
	}
	break;
      default:
	m->unsupported();
	break;
    }
}

#endif
