# This Makefile is currently really only useful in a nix build env
.PHONY: install
install:
	install -D "$(src)/bin/mmdoc" "$(out)/bin/mmdoc"
	install -D -t "$(out)/lib/mmdoc" "$(src)/lib/mmdoc/"{fuse.basic.min.js,highlight.pack.js,minimal.css,mono-blue.css,search.js}
