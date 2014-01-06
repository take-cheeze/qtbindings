app = Qt::Application.new 0, []

main = Qt::Widget.new
main.setWindowTitle 'Hello World!'
main.setLayout Qt::VBoxLayout.new {
  l = Qt::ListWidget.new
  l.addItem 'Hello World!'
  addWidget l
}

main.show

main.activateWindow
main.raise

app.exec
