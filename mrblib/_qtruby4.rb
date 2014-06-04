=begin
/***************************************************************************
                          qtruby.rb  -  description
                             -------------------
    begin                : Fri Jul 4 2003
    copyright            : (C) 2003-2008 by Richard Dale
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
=end

module Qt

  module DebugLevel
    Off, Minimal, High, Extensive = 0, 1, 2, 3
  end

  module DebugChannel
    NONE = 0x00
    AMBIGUOUS = 0x01
    METHOD_MISSING = 0x02
    CALLS = 0x04
    GC = 0x08
    VIRTUAL = 0x10
    VERBOSE = 0x20
    ALL = VERBOSE | VIRTUAL | GC | CALLS | METHOD_MISSING | AMBIGUOUS
  end

  @@debug_level = DebugLevel::Off
  def self.debug_level=(level)
    @@debug_level = level
    Internal::setDebug Qt::DebugChannel::ALL if level >= DebugLevel::Extensive
  end

  def Qt.debug_level; @@debug_level end

  class Base
    def -@; Qt::- self end

#    Module has '<', '<=', '>' and '>=' operator instance methods, so pretend they
#    don't exist by calling method_missing() explicitly
    def <(a) begin; Qt::method_missing :<, self, a; rescue; super(a) end end
    def <=(a) begin; Qt::method_missing :<=, self, a; rescue; super a end end
    def >(a) begin; Qt::method_missing :>, self, a; rescue; super a end end
    def >=(a) begin; Qt::method_missing :>=, self, a; rescue; super a end end

    #    Object has a '==' operator instance method, so pretend it
    #    don't exist by calling method_missing() explicitly
    def ==(a)
      return false if a.nil?
      begin; Qt::method_missing(:==, self, a)
      rescue; super(a)
      end
    end

    # Change the behaviors of is_a? and kind_of? (alias of is_a?) to use above self.ancestors method
    # Note: this definition also affects Object#===
    def is_a?(mod) super || self.class.ancestors.include?(mod) end
    alias :kind_of? :is_a?

    def methods(regular=true)
      return singleton_methods if !regular
      qt_methods super, 0x0
    end

    # From smoke.h, Smoke::mf_protected 0x80
    def protected_methods(all=true) qt_methods super, 0x80 end

    def public_methods(all=true) methods end

    # From smoke.h, Smoke::mf_static 0x01
    def singleton_methods(all=true) qt_methods super, 0x01 end

    def self.qt_overwrite_method(*names)
      names.each do |n|
        n = n.to_sym
        define_method(n) { |*args, &block| method_missing(n, *args, &block) }
      end
    end
  end # Qt::Base

  class Point
    def *(other) Qt::* self, other end
  end

  # Provides a mutable numeric class for passing to methods with
  # C++ 'int*' or 'int&' arg types
  class Integer
    attr_accessor :value
    def initialize(n=0) @value = n end

    def +(n) Integer.new(@value + n.to_i) end
    def -(n) Integer.new(@value - n.to_i) end
    def *(n) Integer.new(@value * n.to_i) end
    def /(n) Integer.new(@value / n.to_i) end
    def %(n) Integer.new(@value % n.to_i) end
    def **(n) Integer.new(@value ** n.to_i) end

    def |(n) Integer.new(@value | n.to_i) end
    def &(n) Integer.new(@value & n.to_i) end
    def ^(n) Integer.new(@value ^ n.to_i) end
    def <<(n) Integer.new(@value << n.to_i) end
    def >>(n) Integer.new(@value >> n.to_i) end
    def >(n)  @value > n.to_i end
    def >=(n) @value >= n.to_i end
    def <(n)  @value < n.to_i end
    def <=(n)  @value <= n.to_i end

    def <=>(n)
      n = n.to_i
      if @value < n; return -1
      elsif @value > n; return 1
      else return 0
      end
    end

    def to_f; @value.to_f end
    def to_i; @value.to_i end
    def to_s; @value.to_s end

    def coerce(n) [n, @value] end
  end

  # If a C++ enum was converted to an ordinary ruby Integer, the
  # name of the type is lost. The enum type name is needed for overloaded
  # method resolution when two methods differ only by an enum type.
  class Enum
    attr_accessor :type, :value
    def initialize(n, e) @value, @type = n, e.to_sym end

    def +(n) @value + n.to_i end
    def -(n) @value - n.to_i end
    def *(n) @value * n.to_i end
    def /(n) @value / n.to_i end
    def %(n) @value % n.to_i end
    def **(n) @value ** n.to_i end

    def |(n) Enum.new(@value | n.to_i, @type) end
    def &(n) Enum.new(@value & n.to_i, @type) end
    def ^(n) Enum.new(@value ^ n.to_i, @type) end
    def ~() ~ @value end
    def <(n) @value < n.to_i end
    def <=(n) @value <= n.to_i end
    def >(n) @value > n.to_i end
    def >=(n) @value >= n.to_i end
    def <<(n) Enum.new(@value << n.to_i, @type) end
    def >>(n) Enum.new(@value >> n.to_i, @type) end

    def ==(n) @value == n.to_i end
    def to_i() @value end

    def to_f() @value.to_f end
    def to_s() @value.to_s end

    def coerce(n) [n, @value] end

    def inspect; to_s end

    def pretty_print(pp)
      pp.text "#<%s:0x%8.8x @type=%s, @value=%d>" % [self.class.name, object_id, type, value]
    end
  end

  # Provides a mutable boolean class for passing to methods with
  # C++ 'bool*' or 'bool&' arg types
  class Boolean
    attr_accessor :value
    def initialize(b=false) @value = b end
    def nil?; !@value end
  end

  module Internal
    Classes['Qt::Integer'.to_sym] = Qt::Integer
    Classes['Qt::Boolean'.to_sym] = Qt::Boolean
    Classes['Qt::Enum'.to_sym] = Qt::Enum
  end

  [
    [AbstractSocket, :abort],
    [AbstractTextDocumentLayout, :format],
    # [AccessibleEvent, :type],
    [ActionEvent, :type],
    [Application, :type],
    [Buffer, :open],
    [ButtonGroup, :id],
    [ByteArray, [:chop, :split]],
    [ChildEvent, :type],
    [CloseEvent, :type],
    [Color, :name],
    [ContextMenuEvent, :type],
    [CoreApplication, [:type, :exit]],
    # [CustomEvent, :type],
    [DBusConnection, :send],
    [DBusError, :type],
    [DBusMessage, :type],
    [Dialog, :exec],
    [DomAttr, :name],
    [DomDocumentType, [:name, :type]],
    [DragEnterEvent, :type],
    [DragLeaveEvent, :type],
    [DropEvent, [:format, :type]],
    [Event, :type],
    [EventLoop, [:exec, :exit]],
    [File, :open],
    [FileOpenEvent, :type],
    [FileIconProvider, :type],
    [FocusEvent, :type],
    [Ftp, :abort],
    [GLContext, :format],
    [GLPixelBuffer, :format],
    [GLWidget, :format],
    [GenericArgument, :name],
    [Gradient, :type],
    [GraphicsEllipseItem, :type],
    [GraphicsItem, :type],
    [GraphicsItemGroup, :type],
    [GraphicsLineItem, :type],
    [GraphicsPathItem, :type],
    [GraphicsPixmapItem, :type],
    [GraphicsPolygonItem, :type],
    [GraphicsProxyWidget, :type],
    [GraphicsRectItem, :type],
    [GraphicsSceneDragDropEvent, :type],
    [GraphicsSceneHelpEvent, :type],
    [GraphicsSceneMouseEvent, :type],
    [GraphicsSceneContextMenuEvent, :type],
    [GraphicsSceneHoverEvent, :type],
    [GraphicsSceneHelpEvent, :type],
    [GraphicsSceneWheelEvent, :type],
    [GraphicsSimpleTextItem, :type],
    [GraphicsSvgItem, :type],
    [GraphicsTextItem, :type],
    [GraphicsWidget, :type],
    [HelpEvent, :type],
    [HideEvent, :type],
    [HoverEvent, :type],
    [Http, :abort],
    [IconDragEvent, :type],
    [InputEvent, :type],
    [InputMethodEvent, :type],
    [IODevice, :open],
    [Image, [:format, :load]],
    [ImageIOHandler, [:format, :name]],
    [ImageReader, :format],
    [ImageWriter, :format],
    [ItemSelection, [:select, :split]],
    [ItemSelectionModel, :select],
    [KeyEvent, :type],
    [LCDNumber, :display],
    [Library, :load],
    [ListWidgetItem, :type],
    [Locale, [:name, :system]],
    [MainWindow, :raise],
    [Menu, :exec],
    [MetaClassInfo, :name],
    [MetaEnum, :name],
    [MetaProperty, [:name, :type]],
    [MetaType, [:load, :type]],
    [MouseEvent, :type],
    [MoveEvent, :type],
    [Movie, :format],
    [NetworkProxy, :type],
    [PageSetupDialog, :exec],
    [PaintEvent, :type],
    [Picture, :load],
    [PictureIO, :format],
    [Pixmap, :load],
    [PluginLoader, :load],
    [PrintDialog, :exec],
    [Printer, :abort],
    [ResizeEvent, :type],
    [Shortcut, :id],
    [ShortcutEvent, :type],
    [ShowEvent, :type],
    [SocketNotifier, :type],
    [SqlDatabase, [:exec, :open]],
    [SqlError, :type],
    [SqlField, [:name, :type]],
    [SqlIndex, :name],
    [SqlQuery, :exec],
    [SqlResult, :exec],
    [SqlTableModel, :select],
    [StandardItem, :type],
    [StandardItemModel, :type],
    [StatusTipEvent, :type],
    [StyleHintReturn, :type],
    [StyleOption, :type],
    [SyntaxHighlighter, :format],
    [TableWidgetItem, :type],
    [TemporaryFile, :open],
    [TextCursor, :select],
    [TextDocument, [:clone, :print]],
    [TextFormat, :type],
    [TextImageFormat, :name],
    [TextInlineObject, :format],
    [TextLength, :type],
    [TextList, :format],
    [TextObject, :format],
    [TextTable, :format],
    [TextTableCell, :format],
    [Timer, :start],
    [TimerEvent, :type],
    [Translator, :load],
    [TreeWidgetItem, :type],
    [UrlInfo, :name],
    [Variant, [:load, :type]],
    [WhatsThisClickedEvent, :type],
    [Widget, :raise],
    [WindowStateChangeEvent, :type],
    [XmlAttributes, :type],
  ].each do |k,v|
    if v.kind_of? Array
      k.qt_overwrite_method(*v)
    else
      k.qt_overwrite_method v
    end
  end

  class AbstractSlider < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class Action < Qt::Base
    def setShortcut(arg)
      if arg.kind_of?(String)
        return super(Qt::KeySequence.new(arg))
      else
        return super(arg)
      end
    end

    def shortcut=(arg) setShortcut arg end
  end

  class Application < Qt::Base
    def initialize(*args)
      if args.length == 1 && args[0].kind_of?(Array)
        super(args.length + 1, [$0] + args[0])
      else
        super(*args)
      end
      $qApp = self
    end

    # Delete the underlying C++ instance after exec returns
    # Otherwise, rb_gc_call_finalizer_at_exit() can delete
    # stuff that Qt::Application still needs for its cleanup.
    def exec
      method_missing(:exec)
      self.dispose
      Qt::Internal.application_terminated = true
    end
  end

  class ByteArray < Qt::Base
    def initialize(*args)
      if args.size == 1 && args[0].kind_of?(String)
        super(args[0], args[0].size)
      else
        super
      end
    end

    def to_s; constData end
    def to_i; toInt end
    def to_f; toDouble end
  end

  class CheckBox < Qt::Base
    def setShortcut(arg)
      if arg.kind_of?(String)
        return super(Qt::KeySequence.new(arg))
      else
        return super(arg)
      end
    end

    def shortcut=(arg) setShortcut arg end
  end

  class Color < Qt::Base
    def inspect; super().sub(/>$/, " %s>" % name) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " %s>" % name) end
  end

  class Connection < Qt::Base
    def inspect
      super().sub(/>$/, " memberName=%s, memberType=%s, object=%s>" %
        [memberName.inspect, memberType == 1 ? "SLOT" : "SIGNAL", object.inspect] )
    end

    def pretty_print(pp)
      pp.text to_s.sub(/>$/, "\n memberName=%s,\n memberType=%s,\n object=%s>" %
        [memberName.inspect, memberType == 1 ? "SLOT" : "SIGNAL", object.inspect] )
    end
  end

  class CoreApplication < Qt::Base
    attr_reader :thread_fix

    def initialize(*args)
      if args.length == 1 && args[0].kind_of?(Array)
        super(args.length + 1, [$0] + args[0])
      else
        super(*args)
      end
      $qApp = self
    end

    # Delete the underlying C++ instance after exec returns
    # Otherwise, rb_gc_call_finalizer_at_exit() can delete
    # stuff that Qt::Application still needs for its cleanup.
    def exec
      method_missing(:exec)
      self.dispose
      Qt::Internal.application_terminated = true
    end
  end

  class Cursor < Qt::Base
    def inspect; super().sub(/>$/, " shape=%d>" % shape) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " shape=%d>" % shape) end
  end

  class Date < Qt::Base
    def initialize(*args)
      if args.size == 1 && args[0].class.name == "Date"
        return super(args[0].year, args[0].month, args[0].day)
      else
        return super(*args)
      end
    end

    def inspect; super().sub(/>$/, " %s>" % toString) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " %s>" % toString) end
    def to_date; ::Date.new! to_julian_day end
  end

  class DateTime < Qt::Base
    def initialize(*args)
      if args.size == 1 && args[0].class.name == "DateTime"
        return super(  Qt::Date.new(args[0].year, args[0].month, args[0].day),
                Qt::Time.new(args[0].hour, args[0].min, args[0].sec) )
      elsif args.size == 1 && args[0].class.name == "Time"
        result = super(  Qt::Date.new(args[0].year, args[0].month, args[0].day),
                Qt::Time.new(args[0].hour, args[0].min, args[0].sec, args[0].usec / 1000) )
        result.timeSpec = (args[0].utc? ? Qt::UTC : Qt::LocalTime)
        return result
      else
        return super(*args)
      end
    end

    def to_time
      if timeSpec == Qt::UTC
        return ::Time.utc(  date.year, date.month, date.day,
                  time.hour, time.minute, time.second, time.msec * 1000 )
      else
        return ::Time.local(  date.year, date.month, date.day,
                    time.hour, time.minute, time.second, time.msec * 1000 )
      end
    end

    def inspect; super().sub(/>$/, " %s>" % toString) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " %s>" % toString) end
  end

  class DBusArgument < Qt::Base
    def inspect; super().sub(/>$/, " currentSignature='%s', atEnd=%s>" % [currentSignature, atEnd]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " currentSignature='%s, atEnd=%s'>" % [currentSignature, atEnd]) end
  end

  class DBusConnectionInterface < Qt::Base
    def serviceOwner(name)
        return Qt::DBusReply.new(internalConstCall(Qt::DBus::AutoDetect, "GetNameOwner", [Qt::Variant.new(name)]))
    end

    def service_owner(name) serviceOwner(name) end

    def registeredServiceNames; Qt::DBusReply.new(internalConstCall(Qt::DBus::AutoDetect, "ListNames")) end

    def registered_service_names; registeredServiceNames end

    def isServiceRegistered(serviceName)
        return Qt::DBusReply.new(internalConstCall(Qt::DBus::AutoDetect, "NameHasOwner", [Qt::Variant.new(serviceName)]))
    end

    def is_service_registered(serviceName) isServiceRegistered serviceName end
    def serviceRegistered?(serviceName) isServiceRegistered serviceName end

    def service_registered?(serviceName) isServiceRegistered serviceName end

    def servicePid(serviceName)
        Qt::DBusReply.new(internalConstCall(Qt::DBus::AutoDetect, "GetConnectionUnixProcessID", [Qt::Variant.new(serviceName)]))
    end

    def service_pid(serviceName) servicePid serviceName end

    def serviceUid(serviceName)
        Qt::DBusReply.new(internalConstCall(Qt::DBus::AutoDetect, "GetConnectionUnixUser", [Qt::Variant.new(serviceName)]))
    end

    def service_uid(serviceName) serviceUid serviceName end

    def startService(name)
         call("StartServiceByName", Qt::Variant.new(name), Qt::Variant.new(0)).value
    end

    def start_service(name) startService name end
  end

  class DBusInterface < Qt::Base
    def call(method_name, *args)
      if args.length == 0
        return super(method_name)
      elsif method_name.is_a? Qt::Enum
        opt = args.shift
        qdbusArgs = args.collect {|arg| qVariantFromValue(arg)}
        return super(method_name, opt, *qdbusArgs)
      else
        # If the method is Qt::DBusInterface.call(), create an Array
        # 'dbusArgs' of Qt::Variants from '*args'
        qdbusArgs = args.collect {|arg| qVariantFromValue(arg)}
        return super(method_name, *qdbusArgs)
      end
    end

    def method_missing(id, *args)
      begin
        # First look for a method in the Smoke runtime
        # If not found, then throw an exception and try dbus.
        super(id, *args)
      rescue
        if args.length == 0
          return call(id.to_s).value
        else
          return call(id.to_s, *args).value
        end
      end
    end
  end

  class DBusMessage < Qt::Base
    def value
      if type() == Qt::DBusMessage::ReplyMessage
        reply = arguments()
        if reply.length == 0
          return nil
        elsif reply.length == 1
          return reply[0].value
        else
          return reply.collect {|v| v.value}
        end
      else
        return nil
      end
    end

    def <<(a) a.kind_of?(Qt::Variant) ? super(a) : super(qVariantFromValue a) end
  end

  class DBusReply
    def initialize(reply)
      @error = Qt::DBusError.new(reply)

      if @error.valid?
        @data = Qt::Variant.new
        return
      end

      if reply.arguments.length >= 1
        @data = reply.arguments[0]
        return
      end

      # error
      @error = Qt::DBusError.new(  Qt::DBusError::InvalidSignature,
                    "Unexpected reply signature" )
      @data = Qt::Variant.new      # clear it
    end

    def isValid; !@error.isValid end
    def valid?; !@error.isValid end
    def value; @data.value end
    def error; @error end
  end

  class Dial < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  Dir::Time = Qt::Enum.new 1, "QDir::SortFlag"

  class DoubleSpinBox < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class DoubleValidator < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  FileIconProvider::File = Qt::Enum.new 6, "QFileIconProvider::IconType"

  class Font < Qt::Base
    def inspect
      super.sub(/>$/, " family=%s, pointSize=%d, weight=%d, italic=%s, bold=%s, underline=%s, strikeOut=%s>" %
      [family.inspect, pointSize, weight, italic, bold, underline, strikeOut])
    end

    def pretty_print(pp)
      pp.text to_s.sub(/>$/, "\n family=%s,\n pointSize=%d,\n weight=%d,\n italic=%s,\n bold=%s,\n underline=%s,\n strikeOut=%s>" %
      [family.inspect, pointSize, weight, italic, bold, underline, strikeOut])
    end
  end

  FontDatabase::Symbol = Qt::Enum.new 30, "QFontDatabase::WritingSystem"
  GraphicsItemGroup::Type = 10
  GraphicsLineItem::Type = 6
  GraphicsPathItem::Type = 2
  GraphicsPolygonItem::Type = 5
  GraphicsProxyWidget::Type = 12
  GraphicsRectItem::Type = 3
  GraphicsSimpleTextItem::Type = 9
  GraphicsSvgItem::Type = 13
  GraphicsTextItem::Type = 8
  GraphicsWidget::Type = 11

  class HttpRequestHeader < Qt::Base
    def method(*args)
      if args.length == 1
        super(*args)
      else
        method_missing(:method, *args)
      end
    end
  end

  class Image < Qt::Base
    def fromImage(image) send("operator=".to_sym, image) end
  end

  class IntValidator < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class ItemSelection < Qt::Base
    include Enumerable

    def each
      for i in 0...count
        yield at(i)
      end
      return self
    end
  end

  class KeySequence < Qt::Base
    def initialize(*args)
      if args.length == 1 && args[0].kind_of?(Qt::Enum) && args[0].type == "Qt::Key"
        return super(args[0].to_i)
      end
      return super(*args)
    end

    def inspect; super().sub(/>$/, " %s>" % toString) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " %s>" % toString) end
  end

  class ListWidgetItem < Qt::Base
    def clone(*args) Qt::ListWidgetItem.new(self) end
    def inspect; super().sub(/>$/, " text='%s'>" % text) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " text='%s'>" % text) end
  end

  class MetaEnum < Qt::Base
    def keyValues()
      res = []
      for i in 0...keyCount()
        if flag?
          res.push "%s=0x%x" % [key(i), value(i)]
        else
          res.push "%s=%d" % [key(i), value(i)]
        end
      end
      return res
    end

    def inspect; super().sub(/>$/, " scope=%s, name=%s, keyValues=Array (%d element(s))>" % [scope, name, keyValues.length]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " scope=%s, name=%s, keyValues=Array (%d element(s))>" % [scope, name, keyValues.length]) end
  end

  class MetaMethod < Qt::Base
    # Oops, name clash with the Signal module so hard code
    # this value rather than get it from the Smoke runtime
    Method = Qt::Enum.new(0, "QMetaMethod::MethodType")
    Signal = Qt::Enum.new(1, "QMetaMethod::MethodType")
  end

  class MetaObject < Qt::Base
    def method(*args)
      if args.length == 1 && args[0].kind_of?(Symbol)
        super(*args)
      else
        method_missing(:method, *args)
      end
    end

    # Add three methods, 'propertyNames()', 'slotNames()' and 'signalNames()'
    # from Qt3, as they are very useful when debugging

    def propertyNames(inherits = false)
      res = []
      for p in (inherits ? 0 : propertyOffset)...propertyCount; res.push property(p).name end
      return res
    end

    def slotNames(inherits = false)
      res = []
      for m in (inherits ? 0 : methodOffset)...methodCount
        res.push "%s %s" % [method(m).typeName == "" ? "void" : method(m).typeName,
          method(m).signature] if method(m).methodType == Qt::MetaMethod::Slot
      end
      res
    end

    def signalNames(inherits = false)
      res = []
      for m in (inherits ? 0 : methodOffset)...methodCount
        res.push "%s %s" % [method(m).typeName == "" ? "void" : method(m).typeName,
          method(m).signature] if method(m).methodType == Qt::MetaMethod::Signal
      end
      res
    end

    def enumerators(inherits = false)
      res = []
      for e in (inherits ? 0 : enumeratorOffset)...enumeratorCount; res.push enumerator(e) end
      res
    end

    def inspect
      str = super
      str.sub!(/>$/, "")
      str << " className=%s," % className
      str << " propertyNames=Array (%d element(s))," % propertyNames.length unless propertyNames.length == 0
      str << " signalNames=Array (%d element(s))," % signalNames.length unless signalNames.length == 0
      str << " slotNames=Array (%d element(s))," % slotNames.length unless slotNames.length == 0
      str << " enumerators=Array (%d element(s))," % enumerators.length unless enumerators.length == 0
      str << " superClass=%s," % superClass.inspect unless superClass == nil
      str.chop!
      str << ">"
    end

    def pretty_print(pp)
      str = to_s
      str.sub!(/>$/, "")
      str << "\n className=%s," % className
      str << "\n propertyNames=Array (%d element(s))," % propertyNames.length unless propertyNames.length == 0
      str << "\n signalNames=Array (%d element(s))," % signalNames.length unless signalNames.length == 0
      str << "\n slotNames=Array (%d element(s))," % slotNames.length unless slotNames.length == 0
      str << "\n enumerators=Array (%d element(s))," % enumerators.length unless enumerators.length == 0
      str << "\n superClass=%s," % superClass.inspect unless superClass == nil
      str << "\n methodCount=%d," % methodCount
      str << "\n methodOffset=%d," % methodOffset
      str << "\n propertyCount=%d," % propertyCount
      str << "\n propertyOffset=%d," % propertyOffset
      str << "\n enumeratorCount=%d," % enumeratorCount
      str << "\n enumeratorOffset=%d," % enumeratorOffset
      str.chop!
      str << ">"
      pp.text str
    end
  end

  MetaType::Float = Qt::Enum.new 135, "QMetaType::Type"

  class Point < Qt::Base
    def inspect; super().sub(/>$/, " x=%d, y=%d>" % [self.x, self.y]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n x=%d,\n y=%d>" % [self.x, self.y]) end
  end

  class PointF < Qt::Base
    def inspect; super().sub(/>$/, " x=%f, y=%f>" % [self.x, self.y]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n x=%f,\n y=%f>" % [self.x, self.y]) end
  end

  class Polygon < Qt::Base
    include Enumerable

    def each
      for i in 0...count; yield point(i) end
      self
    end
  end

  class PolygonF < Qt::Base
    include Enumerable

    def each
      for i in 0...count; yield point(i) end
      self
    end
  end

  Process::StandardError = Qt::Enum.new 1, "QProcess::ProcessChannel"

  class ProgressBar < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class ProgressDialog < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class PushButton < Qt::Base
    def setShortcut(arg)
      if arg.kind_of?(String)
        return super(Qt::KeySequence.new(arg))
      else
        return super(arg)
      end
    end

    def shortcut=(arg) setShortcut arg end
  end

  class Line < Qt::Base
    def inspect; super().sub(/>$/, " x1=%d, y1=%d, x2=%d, y2=%d>" % [x1, y1, x2, y2]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n x1=%d,\n y1=%d,\n x2=%d,\n y2=%d>" % [x1, y1, x2, y2]) end
  end

  class LineF < Qt::Base
    def inspect; super().sub(/>$/, " x1=%f, y1=%f, x2=%f, y2=%f>" % [x1, y1, x2, y2]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n x1=%f,\n y1=%f,\n x2=%f,\n y2=%f>" % [x1, y1, x2, y2]) end
  end

  class MetaType < Qt::Base
    def self.type(*args) method_missing(:type, *args) end
  end

  class ModelIndex < Qt::Base
    def inspect; super().sub(/>$/, " valid?=%s, row=%s, column=%s>" % [valid?, row, column]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n valid?=%s,\n row=%s,\n column=%s>" % [valid?, row, column]) end
  end

  class RadioButton < Qt::Base
    def setShortcut(arg)
      if arg.kind_of?(String)
        return super(Qt::KeySequence.new(arg))
      else
        return super(arg)
      end
    end

    def shortcut=(arg) setShortcut arg end
  end

  class Rect < Qt::Base
    def inspect; super().sub(/>$/, " x=%d, y=%d, width=%d, height=%d>" % [self.x, self.y, width, height]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n x=%d,\n y=%d,\n width=%d,\n height=%d>" % [self.x, self.y, width, height]) end
  end

  class RectF < Qt::Base
    def inspect; super().sub(/>$/, " x=%f, y=%f, width=%f, height=%f>" % [self.x, self.y, width, height]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n x=%f,\n y=%f,\n width=%f,\n height=%f>" % [self.x, self.y, width, height]) end
  end

  class ScrollBar < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class Size < Qt::Base
    def inspect; super().sub(/>$/, " width=%d, height=%d>" % [width, height]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n width=%d,\n height=%d>" % [width, height]) end
  end

  class SizeF < Qt::Base
    def inspect; super().sub(/>$/, " width=%f, height=%f>" % [width, height]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n width=%f,\n height=%f>" % [width, height]) end
  end

  class SizePolicy < Qt::Base
    def inspect; super().sub(/>$/, " horizontalPolicy=%d, verticalPolicy=%d>" % [horizontalPolicy, verticalPolicy]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n horizontalPolicy=%d,\n verticalPolicy=%d>" % [horizontalPolicy, verticalPolicy]) end
  end

  class Slider < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  SocketNotifier::Exception = Qt::Enum.new(2, "QSocketNotifier::Type")

  class SpinBox < Qt::Base
    def range=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class StandardItem < Qt::Base
    def inspect; super().sub(/>$/, " text='%s'>" % [text]) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, "\n text='%s'>" % [text]) end
    def clone; Qt::StandardItem.new(self) end
  end

  class TableWidgetItem < Qt::Base
    def clone(*args) Qt::TableWidgetItem.new(self) end
    def inspect; super().sub(/>$/, " text='%s'>" % text) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " text='%s'>" % text) end
  end

  class Time < Qt::Base
    def initialize(*args)
      if args.size == 1 && args[0].class.name == "Time"
        return super(args[0].hour, args[0].min, args[0].sec)
      else
        return super(*args)
      end
    end

    def inspect; super().sub(/>$/, " %s>" % toString) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " %s>" % toString) end
  end

  class TimeLine < Qt::Base
    def frameRange=(arg)
      if arg.kind_of? Range
        return super(arg.begin, arg.exclude_end?  ? arg.end - 1 : arg.end)
      else
        return super(arg)
      end
    end
  end

  class ToolButton < Qt::Base
    def setShortcut(arg) super(arg.kind_of?(String)? Qt::KeySequence.new(arg) : arg) end
    def shortcut=(arg) setShortcut arg end
  end

  class TreeWidget < Qt::Base
    include Enumerable

    def each
      it = Qt::TreeWidgetItemIterator.new(self)
      while it.current
        yield it.current
        it += 1
      end
    end
  end

  class TreeWidgetItem < Qt::Base
    include Enumerable

    def initialize(*args)
      # There is not way to distinguish between the copy constructor
      # QTreeWidgetItem (const QTreeWidgetItem & other)
      # and
      # QTreeWidgetItem (QTreeWidgetItem * parent, const QStringList & strings, int type = Type)
      # when the latter has a single argument. So force the second variant to be called
      if args.length == 1 && args[0].kind_of?(Qt::TreeWidgetItem)
        super(args[0], Qt::TreeWidgetItem::Type)
      else
        super(*args)
      end
    end

    def inspect
      str = super
      str.sub!(/>$/, "")
      str << " parent=%s," % parent unless parent.nil?
      for i in 0..(columnCount - 1)
        str << " text%d='%s'," % [i, self.text(i)]
      end
      str.sub!(/,?$/, ">")
    end

    def pretty_print(pp)
      str = to_s
      str.sub!(/>$/, "")
      str << " parent=%s," % parent unless parent.nil?
      for i in 0..(columnCount - 1)
        str << " text%d='%s'," % [i, self.text(i)]
      end
      str.sub!(/,?$/, ">")
      pp.text str
    end

    def clone(*args) Qt::TreeWidgetItem.new(self) end

    def each
      it = Qt::TreeWidgetItemIterator.new(self)
      while it.current
        yield it.current
        it += 1
      end
    end
  end

  class TreeWidgetItemIterator < Qt::Base
    def current; send("operator*".to_sym) end
  end

  class Url < Qt::Base
    def inspect; super().sub(/>$/, " url=%s>" % toString) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " url=%s>" % toString) end
  end

  Uuid::Time = Qt::Enum.new 1, "QUuid::Version"

  class Variant < Qt::Base
    String = Qt::Enum.new(10, "QVariant::Type")
    Date = Qt::Enum.new(14, "QVariant::Type")
    Time = Qt::Enum.new(15, "QVariant::Type")
    DateTime = Qt::Enum.new(16, "QVariant::Type")

    def initialize(*args)
      if args.size == 1 && args[0].nil?
        return super()
      elsif args.size == 1 && args[0].class.name == "Date"
        return super(Qt::Date.new(args[0]))
      elsif args.size == 1 && args[0].class.name == "DateTime"
        return super(Qt::DateTime.new(  Qt::Date.new(args[0].year, args[0].month, args[0].day),
                        Qt::Time.new(args[0].hour, args[0].min, args[0].sec) ) )
      elsif args.size == 1 && args[0].class.name == "Time"
        return super(Qt::Time.new(args[0]))
      elsif args.size == 1 && args[0].class.name == "BigDecimal"
        return super(args[0].to_f) # we have to make do with a float
      else
        return super(*args)
      end
    end

    def to_a; toStringList end
    def to_f; toDouble end
    def to_i; toInt end
    def to_int; toInt end

    def value
      case type()
      when Qt::Variant::Invalid; return nil
      when Qt::Variant::Bitmap
      when Qt::Variant::Bool; return toBool
      when Qt::Variant::Brush; return qVariantValue(Qt::Brush, self)
      when Qt::Variant::ByteArray; return toByteArray
      when Qt::Variant::Char; return qVariantValue(Qt::Char, self)
      when Qt::Variant::Color; return qVariantValue(Qt::Color, self)
      when Qt::Variant::Cursor; return qVariantValue(Qt::Cursor, self)
      when Qt::Variant::Date; return toDate
      when Qt::Variant::DateTime; return toDateTime
      when Qt::Variant::Double; return toDouble
      when Qt::Variant::Font; return qVariantValue(Qt::Font, self)
      when Qt::Variant::Icon; return qVariantValue(Qt::Icon, self)
      when Qt::Variant::Image; return qVariantValue(Qt::Image, self)
      when Qt::Variant::Int; return toInt
      when Qt::Variant::KeySequence; return qVariantValue(Qt::KeySequence, self)
      when Qt::Variant::Line; return toLine
      when Qt::Variant::LineF; return toLineF
      when Qt::Variant::List; return toList
      when Qt::Variant::Locale; return qVariantValue(Qt::Locale, self)
      when Qt::Variant::LongLong; return toLongLong
      when Qt::Variant::Map; return toMap
      when Qt::Variant::Palette; return qVariantValue(Qt::Palette, self)
      when Qt::Variant::Pen; return qVariantValue(Qt::Pen, self)
      when Qt::Variant::Pixmap; return qVariantValue(Qt::Pixmap, self)
      when Qt::Variant::Point; return toPoint
      when Qt::Variant::PointF; return toPointF
      when Qt::Variant::Polygon; return qVariantValue(Qt::Polygon, self)
      when Qt::Variant::Rect; return toRect
      when Qt::Variant::RectF; return toRectF
      when Qt::Variant::RegExp; return toRegExp
      when Qt::Variant::Region; return qVariantValue(Qt::Region, self)
      when Qt::Variant::Size; return toSize
      when Qt::Variant::SizeF; return toSizeF
      when Qt::Variant::SizePolicy; return toSizePolicy
      when Qt::Variant::String; return toString
      when Qt::Variant::StringList; return toStringList
      when Qt::Variant::TextFormat; return qVariantValue(Qt::TextFormat, self)
      when Qt::Variant::TextLength; return qVariantValue(Qt::TextLength, self)
      when Qt::Variant::Time; return toTime
      when Qt::Variant::UInt; return toUInt
      when Qt::Variant::ULongLong; return toULongLong
      when Qt::Variant::Url; return toUrl
      end

      return qVariantValue(nil, self)
    end

    def inspect; super.sub(/>$/, " typeName=%s>" % typeName) end
    def pretty_print(pp) pp.text to_s.sub(/>$/, " typeName=%s>" % typeName) end
  end

  class DBusVariant < Variant
    def initialize(value)
      if value.kind_of? Qt::Variant
        super(value)
      else
        super(Qt::Variant.new(value))
      end
    end

    def setVariant(variant) end
    def variant=(variant) setVariant variant end
    def variant; return self end
  end

  class SignalBlockInvocation < Qt::Object
    def initialize(parent, block, signature)
      super parent
      self.class.slots signature if metaObject.indexOfSlot(signature) == -1
      @block = block
    end

    def invoke(*args) @block.call(*args) end
  end

  class BlockInvocation < Qt::Object
    def initialize(target, block, signature)
      super target
      self.class.slots signature if metaObject.indexOfSlot(signature) == -1
      @block = block
    end

    def invoke(*args) @block.call(*args) end
  end

  class MethodInvocation < Qt::Object
    def initialize(target, method, signature)
      super target
      self.class.slots signature if metaObject.indexOfSlot(signature) == -1
      @target, @method = target, method.to_sym
    end

    def invoke(*args) @target.send @method, *args end
  end

  # These values are from the enum WindowType in qnamespace.h.
  # Some of the names such as 'Qt::Dialog', clash with QtRuby
  # class names. So add some constants here to use instead,
  # renamed with an ending of 'Type'.
  WidgetType = 0x00000000
  WindowType = 0x00000001
  DialogType = 0x00000002 | WindowType
  SheetType = 0x00000004 | WindowType
  DrawerType = 0x00000006 | WindowType
  PopupType = 0x00000008 | WindowType
  ToolType = 0x0000000a | WindowType
  ToolTipType = 0x0000000c | WindowType
  SplashScreenType = 0x0000000e | WindowType
  DesktopType = 0x00000010 | WindowType
  SubWindowType =  0x00000012

end # Qt

class Object
  def SIGNAL(signal) (signal.kind_of? Symbol)? "2#{signal}()" : "2#{signal}" end
  def SLOT(slot) (slot.kind_of? Symbol)? "1#{slot}()" : "1#{slot}" end

  def emit(signal) signal end

  def QT_TR_NOOP(x) x end
  def QT_TRANSLATE_NOOP(scope, x) x end
end

class Proc
  # Part of the Rails Object#instance_exec implementation
  def bind(object)
    block, time = self, Time.now
    (class << object; self end).class_eval do
      method_name = "__bind_#{time.to_i}_#{time.usec}"
      define_method(method_name, &block)
      method = instance_method(method_name)
      remove_method(method_name)
      method
    end.bind(object)
  end
end

class Module
  alias_method :_constants, :constants
  alias_method :_instance_methods, :instance_methods
  alias_method :_protected_methods, :protected_methods
  alias_method :_public_methods, :public_methods

  private :_constants, :_instance_methods
  private :_protected_methods, :_public_methods

  def constants(_arg = true)
    qt_methods(_constants, 0x10, true)
  end

  def instance_methods(inc_super=true)
    qt_methods(_instance_methods(inc_super), 0x0, inc_super)
  end

  def protected_methods(inc_super=true)
    qt_methods(_protected_methods(inc_super), 0x80, inc_super)
  end

  def public_methods(inc_super=true)
    qt_methods(_public_methods(inc_super), 0x0, inc_super)
  end
end
