#include <qtruby.h>
#include <smokeruby.h>
#include <marshall_macros.h>

#include <QtWebKit/qwebframe.h>
#include <QtWebKit/qwebhistory.h>

#include <QtDeclarative/QDeclarativeError>

#include <QtTest/qtestaccessible.h>

#include <QtScript/qscriptvalue.h>

DEF_VALUELIST_MARSHALLER( QDeclarativeErrorList, QList<QDeclarativeError>, QDeclarativeError )

TypeHandler QtDeclarative_handlers[] = {
    { "QList<QDeclarativeError>", marshall_QDeclarativeErrorList },
    { 0, 0 }
};

DEF_VALUELIST_MARSHALLER( QScriptValueList, QList<QScriptValue>, QScriptValue )

TypeHandler QtScript_handlers[] = {
    { "QList<QScriptValue>&", marshall_QScriptValueList },
    { 0, 0 }
};

DEF_VALUELIST_MARSHALLER( QTestAccessibilityEventList, QList<QTestAccessibilityEvent>, QTestAccessibilityEvent )

TypeHandler QtTest_handlers[] = {
    { "QList<QTestAccessibilityEvent>", marshall_QTestAccessibilityEventList },
    { 0, 0 }
};

TypeHandler QtUiTools_handlers[] = {
    { 0, 0 }
};

DEF_LIST_MARSHALLER( QWebFrameList, QList<QWebFrame*>, QWebFrame )

DEF_VALUELIST_MARSHALLER( QWebHistoryItemList, QList<QWebHistoryItem>, QWebHistoryItem )

TypeHandler QtWebKit_handlers[] = {
    { "QList<QWebFrame*>", marshall_QWebFrameList },
    { "QList<QWebHistoryItem>", marshall_QWebHistoryItemList },
    { 0, 0 }
};
