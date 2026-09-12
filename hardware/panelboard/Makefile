# Rebuild the panelboard from a clean checkout.  Needs kicad-cli + kicad-library.
PCB := panelboard.kicad_pcb
SCH := panelboard.kicad_sch
GERBER_LAYERS := F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,B.Paste,Edge.Cuts

.PHONY: all check gerbers zip render clean

# The .kicad_sch / .kicad_pcb in git are generated artifacts; these rebuild them.
all:
	python3 generator/gen_sch.py $(SCH)
	python3 generator/gen_pcb.py $(PCB)

# Every severity gates the build -- the board is clean at --severity-all.
check:
	kicad-cli sch erc --exit-code-violations -o erc.rpt $(SCH)
	kicad-cli pcb drc --schematic-parity --severity-all --exit-code-violations \
		-o drc.rpt $(PCB)

gerbers:
	rm -rf gerbers && mkdir gerbers
	kicad-cli pcb export gerbers --no-protel-ext --subtract-soldermask \
		--layers $(GERBER_LAYERS) -o gerbers/ $(PCB)
	kicad-cli pcb export drill --format excellon --excellon-units mm -o gerbers/ $(PCB)

# Upload panelboard-jlc.zip straight to jlcpcb.com.  Runs check first so a board
# that fails DRC can never reach the fab.
zip: check gerbers
	rm -f panelboard-jlc.zip
	cd gerbers && zip -q -X ../panelboard-jlc.zip *.gbr *.drl
	@echo
	@unzip -l panelboard-jlc.zip | tail -n +2
	@printf '\n2 layers, 70.0 x 72.0 mm, 1.6 mm, HASL - defaults are fine for the rest.\n'

render:
	kicad-cli pcb render --side top  -o top.png $(PCB)
	kicad-cli pcb render --side bottom -o bottom.png $(PCB)

clean:
	rm -rf gerbers *.rpt *.net panelboard-jlc.zip top.png bottom.png
