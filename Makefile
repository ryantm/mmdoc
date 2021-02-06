CLANG_FORMAT=clang-format

mmdoc: src/*.c src/*.h src/asset/fuse.basic.min.js.h src/asset/highlight.pack.js.h src/asset/epub.css.h src/asset/minimal.css.h src/asset/mono-blue.css.h  src/asset/search.js.h
	gcc -g -O1 -lzip -lcmark-gfm -lcmark-gfm-extensions -lfastjson src/*.c -o mmdoc

.PHONY: test
test:
	gcc -g -O1 -lzip -lcmark-gfm -lcmark-gfm-extensions -lfastjson test/*.c src/types.c src/render.c src/parse.c -o test.out
	./test.out

src/asset/%.h: src/asset/%
	xxd -i $< $@

format:
	$(CLANG_FORMAT) -i src/*.c src/*.h test/*.c

install: mmdoc
	install -D mmdoc "$(out)/bin/mmdoc"
