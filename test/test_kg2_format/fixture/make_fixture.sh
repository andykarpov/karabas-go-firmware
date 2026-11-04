#!/bin/sh
# Makes fixture.kg with make_core of karabas-go-tools (a sibling of this repo):
#   cd test/test_kg2_format/fixture && ./make_fixture.sh
set -e
python3 ../../../../karabas-go-tools/make_core/make_core.py core.json ../fixture.kg
