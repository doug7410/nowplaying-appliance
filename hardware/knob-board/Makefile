# Rebuild the knob board from a clean checkout.  Needs kicad-cli + kicad-library.
PCB := knob-board.kicad_pcb
SCH := knob-board.kicad_sch
GERBER_LAYERS := F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,B.Paste,Edge.Cuts

.PHONY: check gerbers zip clean

check:
	kicad-cli sch erc --exit-code-violations -o erc.rpt $(SCH)
	kicad-cli pcb drc --schematic-parity --severity-all --exit-code-violations -o drc.rpt $(PCB)

gerbers:
	rm -rf gerbers && mkdir gerbers
	kicad-cli pcb export gerbers --no-protel-ext --subtract-soldermask \
		--layers $(GERBER_LAYERS) -o gerbers/ $(PCB)
	kicad-cli pcb export drill --format excellon --excellon-units mm -o gerbers/ $(PCB)

# Upload knob-board-jlc.zip straight to jlcpcb.com. Runs check first so a board
# that fails DRC can never reach the fab.
zip: check gerbers
	rm -f knob-board-jlc.zip
	cd gerbers && zip -q -X ../knob-board-jlc.zip *.gbr *.drl
	@echo
	@unzip -l knob-board-jlc.zip | tail -n +2
	@printf '\n2 layers, 100.0 x 30.0 mm, 1.6 mm, HASL - defaults are fine for the rest.\n'

clean:
	rm -rf gerbers *.rpt *.net knob-board-jlc.zip
