# Rebuild the knob board from a clean checkout.  Needs kicad-cli + kicad-library.
PCB := mic-board.kicad_pcb
SCH := mic-board.kicad_sch
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

# Upload mic-board-jlc.zip straight to jlcpcb.com. Runs check first so a board
# that fails DRC can never reach the fab.
zip: check gerbers
	rm -f mic-board-jlc.zip
	cd gerbers && zip -q -X ../mic-board-jlc.zip *.gbr *.drl
	@echo
	@unzip -l mic-board-jlc.zip | tail -n +2
	@printf '\n2 layers, 20.0 x 38.0 mm, 1.6 mm, HASL - defaults are fine for the rest.\n'

clean:
	rm -rf gerbers *.rpt *.net mic-board-jlc.zip
