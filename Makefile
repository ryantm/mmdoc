CLANG_FORMAT=clang-format

all: src/asset.fuse.basic.min.js.h src/asset.highlight.pack.js.h src/asset.minimal.css.h src/asset.mono-blue.css.h  src/asset.search.js.h
	gcc -lcmark-gfm -lcmark-gfm-extensions src/*.c

src/asset.%.h: asset/%
	xxd -i $< $@

format:
	$(CLANG_FORMAT) -i src/*.c

# This Makefile is currently really only useful in a nix build env
.PHONY: install
install:
	install -D "$(src)/bin/mmdoc" "$(out)/bin/mmdoc"
	install -D -t "$(out)/lib/mmdoc" "$(src)/lib/mmdoc/"{fuse.basic.min.js,highlight.pack.js,minimal.css,mono-blue.css,search.js}
