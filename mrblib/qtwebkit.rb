module QtWebKit
  module Internal
    getClassList.each do |c|
      classname = Qt::Internal::normalize_classname(c)
      id = Qt::Internal::findClass(c);
      Qt::Internal::insert_pclassid(classname, id)
      Qt::Internal::CppNames[classname] = c
      klass = Qt::Internal::isQObject(c) ? Qt::Internal::create_qobject_class(classname, Qt) \
      : Qt::Internal::create_qt_class(classname, Qt)
      Qt::Internal::classes[classname] = klass unless klass.nil?
    end
  end
end
