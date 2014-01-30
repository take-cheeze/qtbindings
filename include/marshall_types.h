/***************************************************************************
    marshall_types.h - Derived from the PerlQt sources, see AUTHORS 
                       for details
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

#ifndef MARSHALL_TYPES_H
#define MARSHALL_TYPES_H

#include <QtCore/qstring.h>
#include <QtCore/qobject.h>
#include <QtCore/qmetaobject.h>

#include <smoke.h>

#include "marshall.h"
#include "qtruby.h"
#include "smokeruby.h"

Marshall::HandlerFn getMarshallFn(const SmokeType &type);

extern void smokeStackToQtStack(mrb_state* M, Smoke::Stack stack, void ** o, int start, int end, QList<MocArgument> const& args);
extern void smokeStackFromQtStack(mrb_state* M, Smoke::Stack stack, void ** _o, int start, int end, QList<MocArgument> const& args);

namespace QtRuby {

typedef Smoke::ModuleIndex ModuleIndex;

class Q_DECL_EXPORT MethodReturnValueBase : public Marshall 
{
public:
	MethodReturnValueBase(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack);
	const Smoke::Method &method();
	Smoke::StackItem &item();
	Smoke *smoke();
	SmokeType type();
	void next();
	bool cleanup();
	void unsupported();
    mrb_value * var();
protected:
	ModuleIndex _module_index;
	Smoke::Stack _stack;
	SmokeType _st;
	mrb_value *_retval;
	virtual const char *classname();
};


class Q_DECL_EXPORT VirtualMethodReturnValue : public MethodReturnValueBase {
public:
	VirtualMethodReturnValue(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack, mrb_value retval);
	Marshall::Action action();

private:
	mrb_value _retval2;
};


class Q_DECL_EXPORT MethodReturnValue : public MethodReturnValueBase {
public:
	MethodReturnValue(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack, mrb_value * retval);
    Marshall::Action action();

private:
	const char *classname();
};

class Q_DECL_EXPORT MethodCallBase : public Marshall
{
public:
	MethodCallBase(mrb_state* M, ModuleIndex const& idx);
	MethodCallBase(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack);
	Smoke *smoke();
	SmokeType type();
	Smoke::StackItem &item();
	const Smoke::Method &method();
	virtual int items() = 0;
	virtual void callMethod() = 0;	
	void next();
	void unsupported();

protected:
	ModuleIndex _module_index;
	Smoke::Stack _stack;
	int _cur;
	Smoke::Index *_args;
	bool _called;
	mrb_value *_sp;
	virtual const char* classname();
};


class Q_DECL_EXPORT VirtualMethodCall : public MethodCallBase {
public:
	VirtualMethodCall(mrb_state* M, ModuleIndex const& idx, Smoke::Stack stack, mrb_value obj,mrb_value *sp);
	~VirtualMethodCall();
	Marshall::Action action();
	mrb_value * var();
	int items();
	void callMethod();
	bool cleanup();
 
private:
	mrb_value _obj;
};


class Q_DECL_EXPORT MethodCall : public MethodCallBase {
public:
	MethodCall(mrb_state* M, ModuleIndex const& idx, mrb_value target, mrb_value *sp, int items);
	~MethodCall();
	Marshall::Action action();
	mrb_value * var();

	void callMethod();

	int items();
	bool cleanup();
private:
	mrb_value _target;
	smokeruby_object * _o;
	mrb_value *_sp;
	int _items;
	mrb_value _retval;
	const char *classname();
};


class Q_DECL_EXPORT SigSlotBase : public Marshall {
public:
	SigSlotBase(mrb_state* M, QList<MocArgument> const& args);
	~SigSlotBase();
	const MocArgument &arg();
	SmokeType type();
	Smoke::StackItem &item();
	mrb_value * var();
	Smoke *smoke();
	virtual const char *mytype() = 0;
	virtual void mainfunction() = 0;
	void unsupported();
	void next(); 
	void prepareReturnValue(void** o);

protected:
	QList<MocArgument> const& _args;
	int _cur;
	bool _called;
	Smoke::Stack _stack;
	mrb_value *_sp;
};


class Q_DECL_EXPORT EmitSignal : public SigSlotBase {
    QObject *_obj;
    int _id;
	mrb_value * _result;
 public:
    EmitSignal(mrb_state* M, QObject *obj, int id, int items, QList<MocArgument> const& args, mrb_value * sp, mrb_value * result);
    Marshall::Action action();
    Smoke::StackItem &item();
	const char *mytype();
	void emitSignal();
	void mainfunction();
	bool cleanup();

};

class Q_DECL_EXPORT InvokeNativeSlot : public SigSlotBase {
    QObject *_obj;
    int _id;
	mrb_value * _result;
 public:
  InvokeNativeSlot(mrb_state* M, QObject *obj, int id, int items, QList<MocArgument> const& args, mrb_value * sp, mrb_value * result);
    Marshall::Action action();
    Smoke::StackItem &item();
	const char *mytype();
	void invokeSlot();
	void mainfunction();
	bool cleanup();
};

class Q_DECL_EXPORT InvokeSlot : public SigSlotBase {
    mrb_value _obj;
    mrb_sym _slotname;
    void **_o;
public:
    InvokeSlot(mrb_state* M, mrb_value obj, mrb_sym slotname, QList<MocArgument> const& args, void ** o);
	~InvokeSlot();
    Marshall::Action action();
	const char *mytype();
    bool cleanup();
	void copyArguments();
	void invokeSlot(); 
	void mainfunction();
};

}

#endif
