MRuby::Build.new do |conf|
  toolchain :gcc

  enable_debug

  if `uname`.chomp.downcase == 'darwin'
    conf.cc.include_paths << '/opt/local/include'
    conf.cxx.include_paths << '/opt/local/include'
    conf.linker.library_paths << '/opt/local/lib'

    conf.cxx.defines << 'QT_QTDBUS' << 'DEBUG'
  end

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
    c.flags = com[1, com.length - 1].concat c.flags
    c.flags << '-O0'
    # c.defines << 'MRB_GC_FIXED_ARENA'
  end

  conf.linker.command = conf.cxx.command
  conf.linker.flags = conf.cxx.flags + conf.linker.flags

  conf.gembox 'full-core'
  conf.gem :github => 'mattn/mruby-onig-regexp'
  conf.gem :github => 'iij/mruby-io'
  conf.gem :github => 'iij/mruby-dir'
  conf.gem :github => 'iij/mruby-require'
  conf.gem "#{MRUBY_ROOT}/.."
end
