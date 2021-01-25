CLANG_FORMAT=clang-format

test.out: test/*.c
	gcc -g -O1 -lcmark-gfm -lcmark-gfm-extensions -ljq test/*.c src/render.c -o test.out

mmdoc: src/*.c src/asset/fuse.basic.min.js.h src/asset/highlight.pack.js.h src/asset/minimal.css.h src/asset/mono-blue.css.h  src/asset/search.js.h
	gcc -g -O1 -lcmark-gfm -lcmark-gfm-extensions -ljq src/*.c -o mmdoc

src/asset/%.h: src/asset/%
	xxd -i $< $@

format:
	$(CLANG_FORMAT) -i src/*.c

install: mmdoc
	install -D mmdoc "$(out)/bin/mmdoc"
