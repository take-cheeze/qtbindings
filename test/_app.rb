assert('$app') do
  $app = Qt::Application.new(0, [])
end

assert('Test against Qt4 link') do
  assert_raise(NoMethodError) { $app.setMainWidget(nil) }
end

assert("QApplication inheritance") do
  assert_true $app.inherits("Qt::Application")
  assert_true $app.inherits("Qt::CoreApplication")
  assert_true $app.inherits("Qt::Object")
end

assert('QApplication methods') do
  assert_equal $app, Qt::Application::instance
  assert_equal $app, Qt::CoreApplication::instance
  assert_equal $app, Qt::Application.instance
  assert_equal $app, Qt::CoreApplication.instance
  assert_equal $app, $qApp
end
