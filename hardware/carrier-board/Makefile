# Rebuild the carrier board from a clean checkout.  Needs kicad-cli + kicad-library.
PCB := carrier-board.kicad_pcb
SCH := carrier-board.kicad_sch
GERBER_LAYERS := F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,B.Paste,Edge.Cuts

.PHONY: check gerbers zip cpl clean

check:
	kicad-cli sch erc --exit-code-violations -o erc.rpt $(SCH)
	kicad-cli pcb drc --schematic-parity --severity-all --exit-code-violations -o drc.rpt $(PCB)

gerbers:
	rm -rf gerbers && mkdir gerbers
	kicad-cli pcb export gerbers --no-protel-ext --subtract-soldermask \
		--layers $(GERBER_LAYERS) -o gerbers/ $(PCB)
	kicad-cli pcb export drill --format excellon --excellon-units mm -o gerbers/ $(PCB)

# Upload carrier-board-jlc.zip straight to jlcpcb.com. Runs check first so a board
# that fails DRC can never reach the fab.
zip: check gerbers
	rm -f carrier-board-jlc.zip
	cd gerbers && zip -q -X ../carrier-board-jlc.zip *.gbr *.drl
	@echo
	@unzip -l carrier-board-jlc.zip | tail -n +2
	@printf '\n2 layers, 140.0 x 100.0 mm, 1.6 mm, 1 oz outer copper (power traces sized for it), HASL.\n'

# PCBA files: regenerates assembly/cpl-jlc.csv from the board.
# assembly/bom-jlc.csv is hand-maintained (LCSC C-numbers live nowhere in the sch).
cpl:
	kicad-cli pcb export pos --format csv --units mm --side both -o assembly/pos.csv $(PCB)
	python3 -c "import csv; rows=list(csv.DictReader(open('assembly/pos.csv'))); \
	w=csv.writer(open('assembly/cpl-jlc.csv','w',newline='')); \
	w.writerow(['Designator','Mid X','Mid Y','Layer','Rotation']); \
	[w.writerow([r['Ref'],'%.4fmm'%float(r['PosX']),'%.4fmm'%float(r['PosY']), \
	'Top' if r['Side']=='top' else 'Bottom','%.1f'%float(r['Rot'])]) for r in rows]"
	rm -f assembly/pos.csv

clean:
	rm -rf gerbers *.rpt *.net carrier-board-jlc.zip
