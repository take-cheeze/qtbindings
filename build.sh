#!/usr/bin/env bash

cd $(dirname $0)

set -e

cd mruby
MRUBY_CONFIG='../build_config.rb' rake -j 2 $@
