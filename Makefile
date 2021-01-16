CLANG_FORMAT=clang-format

mmdoc: src/*.c src/asset.fuse.basic.min.js.h src/asset.highlight.pack.js.h src/asset.minimal.css.h src/asset.mono-blue.css.h  src/asset.search.js.h
	gcc -g -O1 -lcmark-gfm -lcmark-gfm-extensions src/*.c -o mmdoc

src/asset.%.h: asset/%
	xxd -i $< $@

format:
	$(CLANG_FORMAT) -i src/*.c

install: mmdoc
	install -D mmdoc "$(out)/bin/mmdoc"
