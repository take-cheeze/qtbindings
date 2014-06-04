assert('QWidget inheritance') do
  widget = Qt::Widget.new
  assert_true widget.inherits("Qt::Widget")
  assert_true widget.inherits("Qt::Object")
  assert_true widget.inherits("QObject")
end

assert('QString marshall') do
  widget = Qt::Widget.new
  assert_nil widget.objectName
  widget.objectName = "Barney"
  assert_equal widget.objectName, "Barney"
end

assert('Qt::Widget#children') do
  w1 = Qt::Widget.new
  w2 = Qt::Widget.new w1
  w3 = Qt::Widget.new w1

  assert_equal [ w2, w3 ], w1.children
end

assert('Qt::Widget#findChildren') do
  w = Qt::Widget.new
  assert_raise(TypeError) { w.findChildren(nil) }

  assert_equal [], w.findChildren(Qt::Widget)
  w2 = Qt::Widget.new w

  assert_equal [w2], w.findChildren(Qt::Widget)
  assert_equal [w2], w.findChildren(Qt::Object)
  assert_equal [], w.findChildren(Qt::LineEdit)
  assert_equal [], w.findChildren(Qt::Widget,"Bob")
  assert_equal [], w.findChildren(Qt::Object,"Bob")

  w2.objectName = "Bob"

  assert_equal [w2], w.findChildren(Qt::Widget)
  assert_equal [w2], w.findChildren(Qt::Object)
  assert_equal [w2], w.findChildren(Qt::Widget,"Bob")
  assert_equal [w2], w.findChildren(Qt::Object,"Bob")
  assert_equal [], w.findChildren(Qt::LineEdit, "Bob")

  w3 = Qt::Widget.new w
  w4 = Qt::LineEdit.new w2
  w4.setObjectName("Bob")

  assert_equal [w2, w4, w3], w.findChildren(Qt::Widget)
  assert_equal [w4], w.findChildren(Qt::LineEdit)
  assert_equal [w2, w4], w.findChildren(Qt::Widget,"Bob")
  assert_equal [w4], w.findChildren(Qt::LineEdit,"Bob")
end

assert('Qt::Widget#findChild') do
  w = Qt::Widget.new
  assert_raise(TypeError) { w.findChild(nil) }

  assert_nil w.findChild(Qt::Widget)
  w2 = Qt::Widget.new w

  w3 = Qt::Widget.new w
  w3.objectName = "Bob"
  w4 = Qt::LineEdit.new w2
  w4.objectName = "Bob"

  assert_equal w.findChild(Qt::Widget,"Bob"), w3
  assert_equal w.findChild(Qt::LineEdit,"Bob"), w4
end

assert('boolean marshalling') do
  assert_true Qt::Variant.new(true).toBool
  assert_true !Qt::Variant.new(false).toBool

  assert_false Qt::Boolean.new(true).nil?
  assert_nil Qt::Boolean.new(false)

  # Invalid variant conversion should change b to false
  b = Qt::Boolean.new(true)
  v = Qt::Variant.new("Blah")
  v.toInt(b)

  assert_nil b
end

assert('Qt::Integer#value') do
  assert_equal Qt::Integer.new(100).value, 100
end

assert('Qt::Variant') do
  v = Qt::Variant.new(Qt::Variant::Invalid)

  assert_false v.isValid
  assert_true v.isNull

  v = Qt::Variant.new(55)
  assert_equal v.toInt, 55
  assert_equal v.toUInt, 55
  assert_equal v.toLongLong, 55
  assert_equal v.toULongLong, 55
  assert_equal Qt::Variant.new(-55).toLongLong, -55
  # assert_equal Qt::Variant.new(-55).toULongLong, 18446744073709551561
  assert_equal v.toDouble, 55.0
  assert_equal v.toChar, Qt::Char.new(55)
  assert_equal v.toString, "55"
  assert_equal v.toStringList, [ ]

  assert_equal Qt::Variant.new("Blah").toStringList, [ "Blah" ]

  assert_equal Qt::Variant.new(Qt::Size.new(30,40)).toSize,::Size.new(30,40)
  assert_equal Qt::Variant.new(Qt::SizeF.new(20,30)).toSizeF, Qt::SizeF.new(20,30)

  assert_equal Qt::Variant.new(Qt::Rect.new(30,40,10,10)).toRect, Qt::Rect.new(30,40,10,10)
  assert_equal Qt::Variant.new(Qt::RectF.new(20,30,10,10)).toRectF, Qt::RectF.new(20,30,10,10)

  assert_equal Qt::Variant.new(Qt::Point.new(30,40)).toPoint, Qt::Point.new(30,40)
  assert_equal Qt::Variant.new(Qt::PointF.new(20,30)).toPointF, Qt::PointF.new(20,30)
end
