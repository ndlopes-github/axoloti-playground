# Host-side builds: unit tests, the DFU tool and the cheat sheet.
# Patches are compiled by the Ksoloti patcher (see README).

CXX ?= g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined

all: test tools/axo_dfu

test: tests/run_tests tests/run_grains tests/run_grains_ctl tests/run_groove tests/run_soliton tests/run_aeolian tests/run_switch tests/run_raw tests/run_quartet
	./tests/run_tests
	./tests/run_grains
	./tests/run_grains_ctl
	./tests/run_groove
	./tests/run_soliton
	./tests/run_aeolian
	./tests/run_switch
	./tests/run_raw
	./tests/run_quartet

tests/run_tests: tests/test_freak.cpp lib/freak_dsp.h lib/launchkey_ctl.h
	$(CXX) $(CXXFLAGS) -o $@ $<

tests/run_grains: tests/test_grains.cpp lib/grains.h lib/freak_dsp.h
	$(CXX) $(CXXFLAGS) -o $@ $<

tests/run_grains_ctl: tests/test_grains_ctl.cpp lib/grains_voice.h lib/grains_ctl.h lib/grains_bank.h lib/wav.h lib/grains.h lib/launchkey_ctl.h lib/freak_dsp.h
	$(CXX) $(CXXFLAGS) -o $@ $<

tests/run_groove: tests/test_groove.cpp lib/groove_ctl.h lib/groove_seq.h lib/groove_synth.h lib/groove_drums.h lib/launchkey_ctl.h lib/freak_dsp.h
	$(CXX) $(CXXFLAGS) -o $@ $<

SOLITON_DEPS = lib/soliton_voice.h lib/soliton_ctl.h lib/soliton_field.h lib/pde_fft.h third_party/cmsis_fft/cmsis_cfft.h lib/launchkey_ctl.h lib/freak_dsp.h

tests/run_soliton: tests/test_soliton.cpp $(SOLITON_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

AEOLIAN_DEPS = lib/aeolian_ctl.h lib/aeolian_harp.h lib/launchkey_ctl.h lib/freak_dsp.h

tests/run_aeolian: tests/test_aeolian.cpp $(AEOLIAN_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

tests/run_switch: tests/test_switch.cpp lib/patch_switch.h
	$(CXX) $(CXXFLAGS) -o $@ $<

tools/axo_dfu: tools/axo_dfu.c
	$(CC) -O2 -Wall -o $@ $< -l:libusb-1.0.so.0

cheatsheet: docs/cheatsheet.pdf docs/grains_cheatsheet.pdf docs/groove_cheatsheet.pdf docs/soliton_cheatsheet.pdf docs/aeolian_cheatsheet.pdf docs/raw_cheatsheet.pdf docs/quartet_cheatsheet.pdf

# Panel overlays for the Launchkey (print at actual size, cut out)
overlays: docs/overlays.pdf

docs/overlays.pdf: docs/overlays.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error overlays.tex >/dev/null && latexmk -c overlays.tex >/dev/null

docs/cheatsheet.pdf: docs/cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error cheatsheet.tex >/dev/null && latexmk -c cheatsheet.tex >/dev/null

docs/grains_cheatsheet.pdf: docs/grains_cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error grains_cheatsheet.tex >/dev/null && latexmk -c grains_cheatsheet.tex >/dev/null

.PHONY: all test cheatsheet overlays

# Host render of one voice with the real Braids code (needs the Ksoloti firmware tree):
#   make render && ./tests/render_freak out.wav <shape> <midi note>
FW ?= $(HOME)/.local/opt/ksoloti-1.1.0/ksoloti-1.1.0/firmware
render: tests/render_freak

tests/render_freak: tests/render_freak.cpp lib/freak_voice.h lib/freak_dsp.h lib/launchkey_ctl.h lib/braids_init.h third_party/braids/digital_oscillator.cc
	$(CXX) -std=c++11 -O1 -g -fsanitize=address -w -I$(FW) -I$(FW)/mutable_instruments -o $@ tests/render_freak.cpp third_party/braids/digital_oscillator.cc $(FW)/mutable_instruments/braids/braids_resources.cpp $(FW)/mutable_instruments/stmlib/utils/random.cpp

.PHONY: render

# Starter bank for the grains patch (objects/grains_wav/*.wav), rendered with
# freak's voice and the real Braids code: make bank
bank: tests/render_bank
	./tests/render_bank objects/grains_wav

tests/render_bank: tests/render_bank.cpp lib/freak_voice.h lib/freak_dsp.h lib/launchkey_ctl.h lib/braids_init.h third_party/braids/digital_oscillator.cc
	$(CXX) -std=c++11 -O1 -g -fsanitize=address -w -I$(FW) -I$(FW)/mutable_instruments -o $@ tests/render_bank.cpp third_party/braids/digital_oscillator.cc $(FW)/mutable_instruments/braids/braids_resources.cpp $(FW)/mutable_instruments/stmlib/utils/random.cpp

.PHONY: bank

# Drum kit for the groove patch (objects/kit_bb), from CC0 sources: make kit
kit:
	tools/make_kit.sh objects/kit_bb

.PHONY: kit

# Host render of the grains presets with the starter bank (no FX):
#   make render_grains && ./tests/render_grains <out dir>
render_grains: tests/render_grains

tests/render_grains: tests/render_grains.cpp lib/grains_voice.h lib/grains_ctl.h lib/grains_bank.h lib/wav.h lib/grains.h lib/launchkey_ctl.h lib/freak_dsp.h
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: render_grains

# Host render of the groove patch (kit, looper, synth, mixer with the Elements
# reverb): make render_groove && ./tests/render_groove out.wav [kit dir] [preset]
render_groove: tests/render_groove

tests/render_groove: tests/render_groove.cpp lib/groove_mix.h lib/groove_ctl.h lib/groove_seq.h lib/groove_synth.h lib/groove_drums.h lib/grains_bank.h lib/wav.h
	$(CXX) -std=c++11 -O1 -g -fsanitize=address -w -DTEST -I$(FW) -I$(FW)/mutable_instruments -o $@ tests/render_groove.cpp

.PHONY: render_groove

docs/groove_cheatsheet.pdf: docs/groove_cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error groove_cheatsheet.tex >/dev/null && latexmk -c groove_cheatsheet.tex >/dev/null

# Host render of the soliton presets (3 voices, dry): make render_soliton &&
#   ./tests/render_soliton <out dir>          one WAV per preset
#   ./tests/render_soliton <out dir> probe    the numbers dev/probe logs on the board
render_soliton: tests/render_soliton

tests/render_soliton: tests/render_soliton.cpp $(SOLITON_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: render_soliton

docs/soliton_cheatsheet.pdf: docs/soliton_cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error soliton_cheatsheet.tex >/dev/null && latexmk -c soliton_cheatsheet.tex >/dev/null

# Host render of the aeolian presets (dry): make render_aeolian &&
#   ./tests/render_aeolian <out dir>          one stereo WAV per preset, 20 s
#   ./tests/render_aeolian <out dir> probe    the numbers dev/probe logs on the board
render_aeolian: tests/render_aeolian

tests/render_aeolian: tests/render_aeolian.cpp $(AEOLIAN_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: render_aeolian

docs/aeolian_cheatsheet.pdf: docs/aeolian_cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error aeolian_cheatsheet.tex >/dev/null && latexmk -c aeolian_cheatsheet.tex >/dev/null

RAW_DEPS = lib/raw_ctl.h lib/raw_voice.h lib/launchkey_ctl.h lib/freak_dsp.h

tests/run_raw: tests/test_raw.cpp $(RAW_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Host render of the raw presets (dry): make render_raw &&
#   ./tests/render_raw <out dir>          one WAV per preset
#   ./tests/render_raw <out dir> probe    the numbers dev/probe logs on the board
render_raw: tests/render_raw

tests/render_raw: tests/render_raw.cpp $(RAW_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: render_raw

docs/raw_cheatsheet.pdf: docs/raw_cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error raw_cheatsheet.tex >/dev/null && latexmk -c raw_cheatsheet.tex >/dev/null

QUARTET_DEPS = lib/quartet_ctl.h lib/quartet_voice.h lib/freak_dsp.h

tests/run_quartet: tests/test_quartet.cpp $(QUARTET_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Host render of the quartet voice (dry), for comparing by ear:
#   make render_quartet && ./tests/render_quartet <out dir>
render_quartet: tests/render_quartet

tests/render_quartet: tests/render_quartet.cpp $(QUARTET_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: render_quartet

docs/quartet_cheatsheet.pdf: docs/quartet_cheatsheet.tex
	cd docs && latexmk -pdf -interaction=nonstopmode -halt-on-error quartet_cheatsheet.tex >/dev/null && latexmk -c quartet_cheatsheet.tex >/dev/null
