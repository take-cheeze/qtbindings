app = Qt::Application.new 0, []

main = Qt::MainWindow.new
main.setWindowTitle 'Hello World!'
main.show

app.exec
