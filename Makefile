# Host-side builds: unit tests, the DFU tool and the cheat sheet.
# Patches are compiled by the Ksoloti patcher (see README).

CXX ?= g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined

all: test tools/axo_dfu

test: tests/run_tests
	./tests/run_tests

tests/run_tests: tests/test_freak.cpp lib/freak_dsp.h lib/launchkey_ctl.h
	$(CXX) $(CXXFLAGS) -o $@ $<

tools/axo_dfu: tools/axo_dfu.c
	$(CC) -O2 -Wall -o $@ $< -l:libusb-1.0.so.0

cheatsheet: docs/cheatsheet.pdf

docs/cheatsheet.pdf: docs/cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error cheatsheet.tex >/dev/null && latexmk -c cheatsheet.tex >/dev/null

.PHONY: all test cheatsheet
