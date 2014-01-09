MRuby::Build.new do |conf|
  toolchain :gcc

  enable_debug

  is_darwin = `uname`.chomp.downcase == 'darwin'

  conf.linker.library_paths << '/opt/local/lib' if is_darwin

  conf.linker.libraries += [
    'smokebase', 'smokephonon', 'smokeqtgui',
    'smokeqtcore', 'smokeqtopengl', 'smokeqtwebkit',
    'smokeqtdbus', 'smokeqtdeclarative', 'smokeqtnetwork',
    'smokeqtxml', 'smokeqtsvg', 'smokeqtsql',
    'smokeqtuitools', 'smokeqtscript', 'smokeqttest',

    'QtCore', 'QtDBus', 'QtDeclarative',
    'QtGui', 'QtScript', 'QtOpenGL',
    'QtSql', 'QtXml', 'QtNetwork',
    'QtWebKit',

    'qscintilla2']

  [conf.cc, conf.cxx].each do |c|
    com = c.command.split ' '
    c.command = com[0]
    c.flags = com[1, com.length - 1].concat c.flags << '-O0'
    c.include_paths << '/opt/local/include' if is_darwin
    c.defines << 'MRB_GC_FIXED_ARENA' << 'QT_QTDBUS' << 'DEBUG=1'
  end

  conf.linker.command = conf.cxx.command
  conf.linker.flags = conf.cxx.flags + conf.linker.flags

  conf.gembox 'full-core'
  conf.gem :github => 'iij/mruby-io'
  conf.gem :github => 'iij/mruby-dir'
  conf.gem :github => 'iij/mruby-tempfile'
  conf.gem :github => 'iij/mruby-env'
  conf.gem :github => 'iij/mruby-require'
  conf.gem :github => 'mattn/mruby-onig-regexp'
  conf.gem "#{MRUBY_ROOT}/.."
end
