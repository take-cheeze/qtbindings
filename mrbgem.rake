MRuby::Gem::Specification.new('mruby-qt') do |spec|
  spec.license = 'LGPL2'
  spec.authors = 'qtbindings'

  spec.cxx.defines << 'QT_QTDBUS' << 'QT_QSCINTILLA2'
  spec.linker.libraries += [
    'smokebase', 'smokephonon', 'smokeqtgui',
    'smokeqtcore', 'smokeqtopengl', 'smokeqtwebkit',
    'smokeqtdbus', 'smokeqtdeclarative', 'smokeqtnetwork',
    'smokeqtxml', 'smokeqtsvg', 'smokeqtsql',
    'smokeqtuitools', 'smokeqtscript', 'smokeqttest',
    'smokeqsci',

    'QtCore', 'QtDBus', 'QtDeclarative',
    'QtGui', 'QtScript', 'QtOpenGL',
    'QtSql', 'QtXml', 'QtNetwork',
    'QtWebKit',

    'qscintilla2']

  spec.add_dependency 'mruby-onig-regexp'
end
