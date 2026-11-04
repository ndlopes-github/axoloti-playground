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

# Host render of one voice with the real Braids code (needs the Ksoloti firmware tree):
#   make render && ./tests/render_freak out.wav <shape> <midi note>
FW ?= $(HOME)/.local/opt/ksoloti-1.1.0/ksoloti-1.1.0/firmware
render: tests/render_freak

tests/render_freak: tests/render_freak.cpp lib/freak_voice.h lib/freak_dsp.h lib/launchkey_ctl.h lib/braids_init.h third_party/braids/digital_oscillator.cc
	$(CXX) -std=c++11 -O1 -g -fsanitize=address -w -I$(FW) -I$(FW)/mutable_instruments -o $@ tests/render_freak.cpp third_party/braids/digital_oscillator.cc $(FW)/mutable_instruments/braids/braids_resources.cpp $(FW)/mutable_instruments/stmlib/utils/random.cpp

.PHONY: render
