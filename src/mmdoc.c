/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "arena.h"
#include "files.h"
#include "inputs.h"
#include "multi.h"
#include "parse.h"
#include "refs.h"
#include "render.h"
#include "single.h"
#include "types.h"
#include <mmdocconfig.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

extern int errno;

void print_usage(void) {
  printf("mmdoc version %s - minimal markdown documentation\n", MMDOC_VERSION);
  printf("\n");
  printf("mmdoc PROJECT-NAME SRC OUT\n");
  printf("\n");
  printf("options:\n");
  printf("-h, --help                show help\n");
  printf("-v, --version             show version\n");
  printf("\n");
  printf("PROJECT-NAME is the name of the project the documentation is "
         "generated for.\n");
  printf("\n");
  printf("SRC a directory containing Markdown files; a file called toc.md at "
         "the top level\n");
  printf("is required.\n");
  printf("\n");
  printf("OUT a directory where the documentation is written to.\n");
}

static size_t detect_worker_count(size_t item_count) {
  if (item_count <= 3)
    return 1;
  long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  size_t workers = cpus > 1 ? (size_t)cpus : 1;
  if (workers > 16)
    workers = 16;
  if (workers > item_count)
    workers = item_count;
  return workers;
}

typedef struct {
  MmdocArena arena;
  cmark_parser *parser;
  cmark_node *doc;
  Array anchors;
  char *title;
  size_t file_size;
  unsigned int syntax_hints;
  int has_code_block;
  int arena_live;
} ParsedFile;

typedef struct {
  size_t index;
  size_t size;
} SizeOrderEntry;

static int compare_size_order_desc(const void *a, const void *b) {
  const SizeOrderEntry *sa = a;
  const SizeOrderEntry *sb = b;
  if (sa->size < sb->size)
    return 1;
  if (sa->size > sb->size)
    return -1;
  return sa->index < sb->index ? -1 : (sa->index > sb->index ? 1 : 0);
}

typedef struct {
  const Array *md_files;
  const size_t *parse_order;
  ParsedFile *parsed;
  size_t next_index;
  int failed;
} ParseStageCtx;

static void *parse_stage_worker(void *arg) {
  ParseStageCtx *ctx = arg;
  cmark_mem *mem = mmdoc_arena_cmark_mem();
  for (;;) {
    size_t slot = __atomic_fetch_add(&ctx->next_index, 1, __ATOMIC_RELAXED);
    if (slot >= ctx->md_files->used)
      break;
    if (__atomic_load_n(&ctx->failed, __ATOMIC_RELAXED))
      break;

    size_t i = ctx->parse_order != NULL ? ctx->parse_order[slot] : slot;
    ParsedFile *pf = &ctx->parsed[i];
    size_t slab_size = pf->file_size > 0 ? pf->file_size * 8 : 32768;
    if (slab_size < 32768)
      slab_size = 32768;
    if (slab_size > 16 * 1024 * 1024)
      slab_size = 16 * 1024 * 1024;
    mmdoc_arena_init(&pf->arena, slab_size);
    pf->arena_live = 1;
    mmdoc_arena_bind(&pf->arena);
    init_array(&pf->anchors, pf->file_size > 65536 ? 256 : 16);

    pf->parser =
        cmark_parser_new_with_mem(CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE, mem);
    if (pf->parser != NULL)
      pf->doc = mmdoc_render_cmark_document_ex(ctx->md_files->array[i],
                                               pf->parser, &pf->syntax_hints);

    if (pf->doc == NULL ||
        mmdoc_prepare_parsed_doc(pf->doc, mem, ctx->md_files->array[i],
                                 &pf->anchors, &pf->title,
                                 &pf->has_code_block) != 0) {
      __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
    }
    mmdoc_arena_bind(NULL);
  }
  return NULL;
}

typedef struct {
  AnchorLocationArray toc_anchor_locations;
  const size_t *page_indices;
  const size_t *render_order;
  size_t page_count;
  const size_t *chapter_file_indices;
  ParsedFile *parsed;
  AnchorLocationArray *anchor_locations;
  AnchorLocationArray *chapters_by_source;
  ChapterOutputs *outputs;
  size_t next_index;
  int failed;
} RenderStageCtx;

static void *render_stage_worker(void *arg) {
  RenderStageCtx *ctx = arg;
  cmark_mem *mem = mmdoc_arena_cmark_mem();
  MmdocBuf scratch_plain;
  mmdoc_buf_init(&scratch_plain, 65536);
  for (;;) {
    size_t slot = __atomic_fetch_add(&ctx->next_index, 1, __ATOMIC_RELAXED);
    if (slot >= ctx->page_count)
      break;
    if (__atomic_load_n(&ctx->failed, __ATOMIC_RELAXED))
      break;

    size_t k = ctx->render_order != NULL ? ctx->render_order[slot] : slot;
    const AnchorLocation *chapter =
        &ctx->toc_anchor_locations.array[ctx->page_indices[k]];
    ParsedFile *pf = &ctx->parsed[ctx->chapter_file_indices[k]];
    mmdoc_arena_bind(&pf->arena);
    scratch_plain.len = 0;

    if (mmdoc_render_chapter_from_doc_ex(
            pf->doc, pf->parser, mem, chapter, ctx->anchor_locations,
            ctx->chapters_by_source, pf->syntax_hints, 1, pf->has_code_block,
            pf->file_size, &scratch_plain, &ctx->outputs[k]) != 0) {
      __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
    }

    mmdoc_arena_bind(NULL);
    mmdoc_arena_destroy(&pf->arena);
    pf->arena_live = 0;
    pf->doc = NULL;
    pf->parser = NULL;
  }
  mmdoc_buf_free(&scratch_plain);
  return NULL;
}

typedef struct {
  Inputs inputs;
  AnchorLocationArray anchor_locations;
  const ChapterOutputs *outputs;
  size_t page_count;
  int result;
} SingleWriteTask;

static void *single_write_thread_fn(void *arg) {
  SingleWriteTask *task = arg;
  task->result = mmdoc_single_write_from_chapters(
      task->inputs, task->anchor_locations, task->outputs, task->page_count);
  return NULL;
}

static void run_parallel(size_t workers, void *(*fn)(void *), void *ctx) {
  if (workers <= 1) {
    fn(ctx);
    return;
  }
  pthread_t *threads = malloc((workers - 1) * sizeof(*threads));
  if (threads == NULL) {
    fn(ctx);
    return;
  }
  size_t spawned = 0;
  for (size_t t = 0; t < workers - 1; t++) {
    if (pthread_create(&threads[t], NULL, fn, ctx) == 0)
      spawned++;
    else
      break;
  }
  fn(ctx);
  for (size_t t = 0; t < spawned; t++)
    pthread_join(threads[t], NULL);
  free(threads);
}

static size_t find_sorted_file_index(const Array *sorted_files,
                                     const char *file_path) {
  size_t lo = 0;
  size_t hi = sorted_files->used;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int cmp = strcmp(sorted_files->array[mid], file_path);
    if (cmp == 0)
      return mid;
    if (cmp < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return sorted_files->used;
}

int main(int argc, char *argv[]) {
  if (argc == 2) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      print_usage();
      return 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
      printf("mmdoc version %s\n", MMDOC_VERSION);
      return 0;
    }
  }
  if (argc != 4) {
    print_usage();
    return 1;
  }

  int result = 1;
  Inputs inputs = {0};
  Array toc_refs = {0};
  Array md_files = {0};
  Array img_files = {0};
  AnchorLocationArray anchor_locations = {0};
  AnchorLocationArray toc_anchor_locations = {0};
  AnchorLocationArray chapters_by_source = {0};
  ParsedFile *parsed = NULL;
  size_t *page_indices = NULL;
  size_t *chapter_file_indices = NULL;
  ChapterOutputs *outputs = NULL;
  size_t page_count = 0;

  mmdoc_cmark_init();
#ifdef __GLIBC__
  /* Avoid per-buffer mmap/munmap page-fault churn across worker threads. */
  mallopt(M_MMAP_THRESHOLD, 32 * 1024 * 1024);
  mallopt(M_TRIM_THRESHOLD, -1);
#endif

  if (0 != mmdoc_inputs_derive(&inputs, argv))
    goto cleanup;

  if (0 != mmdoc_refs(&toc_refs, inputs.toc_path))
    goto cleanup;

  init_array(&md_files, 100);
  init_array(&img_files, 32);
  if (mmdoc_collect_source_files(&md_files, &img_files, inputs) != 0)
    goto cleanup;

  /* Stage A: Parse every Markdown file once (in parallel, largest files first)
   * and collect anchors + titles while retaining parsed ASTs. */
  if (md_files.used > 0) {
    parsed = calloc(md_files.used, sizeof(*parsed));
    if (parsed == NULL)
      goto cleanup;

    size_t *parse_order = NULL;
    if (md_files.used > 1) {
      SizeOrderEntry *entries = malloc(md_files.used * sizeof(*entries));
      parse_order = malloc(md_files.used * sizeof(*parse_order));
      if (entries != NULL && parse_order != NULL) {
        for (size_t i = 0; i < md_files.used; i++) {
          struct stat st;
          size_t sz = (stat(md_files.array[i], &st) == 0 && st.st_size > 0)
                          ? (size_t)st.st_size
                          : 0;
          parsed[i].file_size = sz;
          entries[i].index = i;
          entries[i].size = sz;
        }
        qsort(entries, md_files.used, sizeof(*entries),
              compare_size_order_desc);
        for (size_t i = 0; i < md_files.used; i++)
          parse_order[i] = entries[i].index;
      } else {
        free(parse_order);
        parse_order = NULL;
      }
      free(entries);
    }

    ParseStageCtx parse_ctx = {
        .md_files = &md_files,
        .parse_order = parse_order,
        .parsed = parsed,
        .next_index = 0,
        .failed = 0,
    };
    run_parallel(detect_worker_count(md_files.used), parse_stage_worker,
                 &parse_ctx);
    free(parse_order);
    if (parse_ctx.failed)
      goto cleanup;
  }

  /* Build anchor_locations in deterministic sorted file order. */
  size_t total_anchors = 0;
  for (size_t i = 0; i < md_files.used; i++)
    total_anchors += parsed[i].anchors.used;
  init_anchor_location_array(&anchor_locations,
                             total_anchors > 0 ? total_anchors : 64);
  const char *index_anchor = toc_refs.array[0];
  int found_index_page = 0;
  for (size_t i = 0; i < md_files.used; i++) {
    int is_index_page = 0;
    for (size_t j = 0; !found_index_page && j < parsed[i].anchors.used; j++) {
      if (strcmp(parsed[i].anchors.array[j], index_anchor) == 0) {
        is_index_page = 1;
        found_index_page = 1;
      }
    }
    if (mmdoc_anchors_add_file(&anchor_locations, md_files.array[i],
                               &parsed[i].anchors, parsed[i].title,
                               is_index_page, inputs) != 0)
      goto cleanup;
    free(parsed[i].title);
    parsed[i].title = NULL;
    /* Anchor strings live inside parsed[i].arena; only free the pointer table.
     */
    free(parsed[i].anchors.array);
    parsed[i].anchors.array = NULL;
    parsed[i].anchors.used = parsed[i].anchors.size = 0;
  }
  if (build_anchor_location_index(&anchor_locations) != 0)
    goto cleanup;

  init_anchor_location_array(&toc_anchor_locations, toc_refs.used);
  if (0 != mmdoc_anchors_find_toc_anchors(&toc_anchor_locations, &toc_refs,
                                          &anchor_locations))
    goto cleanup;
  free_array(&toc_refs);

  if (toc_anchor_locations.used > 0) {
    page_indices = malloc(toc_anchor_locations.used * sizeof(*page_indices));
    if (page_indices == NULL)
      goto cleanup;
    page_count =
        mmdoc_collect_unique_page_indices(toc_anchor_locations, page_indices);
  }

  /* Map each unique published chapter to its parsed file index and release
   * arenas of unlisted files immediately. */
  unsigned char *used_file =
      md_files.used > 0 ? calloc(md_files.used, 1) : NULL;
  if (page_count > 0) {
    chapter_file_indices = malloc(page_count * sizeof(*chapter_file_indices));
    outputs = calloc(page_count, sizeof(*outputs));
    if (chapter_file_indices == NULL || outputs == NULL ||
        (md_files.used > 0 && used_file == NULL)) {
      free(used_file);
      goto cleanup;
    }
  }

  init_anchor_location_array(&chapters_by_source, page_count);
  for (size_t k = 0; k < page_count; k++) {
    AnchorLocation chapter = toc_anchor_locations.array[page_indices[k]];
    size_t fidx = find_sorted_file_index(&md_files, chapter.file_path);
    if (fidx >= md_files.used) {
      free(used_file);
      goto cleanup;
    }
    chapter_file_indices[k] = fidx;
    used_file[fidx] = 1;
    chapter.anchor = chapter.file_path;
    insert_anchor_location_array(&chapters_by_source, &chapter);
  }
  if (build_anchor_location_index(&chapters_by_source) != 0) {
    free(used_file);
    goto cleanup;
  }

  for (size_t i = 0; i < md_files.used; i++) {
    if (used_file == NULL || !used_file[i]) {
      if (parsed[i].arena_live) {
        mmdoc_arena_destroy(&parsed[i].arena);
        parsed[i].arena_live = 0;
        parsed[i].doc = NULL;
        parsed[i].parser = NULL;
      }
    }
  }
  free(used_file);

  /* Stage B: Render all outputs (Markdown, search JSON, single HTML, multi
   * HTML) per chapter in parallel (largest files first) from the single AST. */
  if (page_count > 0) {
    size_t *render_order = NULL;
    if (page_count > 1) {
      SizeOrderEntry *entries = malloc(page_count * sizeof(*entries));
      render_order = malloc(page_count * sizeof(*render_order));
      if (entries != NULL && render_order != NULL) {
        for (size_t k = 0; k < page_count; k++) {
          entries[k].index = k;
          entries[k].size = parsed[chapter_file_indices[k]].file_size;
        }
        qsort(entries, page_count, sizeof(*entries), compare_size_order_desc);
        for (size_t k = 0; k < page_count; k++)
          render_order[k] = entries[k].index;
      } else {
        free(render_order);
        render_order = NULL;
      }
      free(entries);
    }

    RenderStageCtx render_ctx = {
        .toc_anchor_locations = toc_anchor_locations,
        .page_indices = page_indices,
        .render_order = render_order,
        .page_count = page_count,
        .chapter_file_indices = chapter_file_indices,
        .parsed = parsed,
        .anchor_locations = &anchor_locations,
        .chapters_by_source = &chapters_by_source,
        .outputs = outputs,
        .next_index = 0,
        .failed = 0,
    };
    run_parallel(detect_worker_count(page_count), render_stage_worker,
                 &render_ctx);
    free(render_order);
    if (render_ctx.failed)
      goto cleanup;
  }

  /* Emit any unresolved-anchor warnings in deterministic chapter order. */
  for (size_t k = 0; k < page_count; k++) {
    if (outputs[k].warnings.len > 0)
      fwrite(outputs[k].warnings.data, 1, outputs[k].warnings.len, stdout);
  }

  /* Stage C: Write single-page HTML concurrently with multi-page HTML +
   * search + Markdown, then copy images collected in Stage A. */
  {
    SingleWriteTask single_task = {
        .inputs = inputs,
        .anchor_locations = anchor_locations,
        .outputs = outputs,
        .page_count = page_count,
        .result = -1,
    };
    pthread_t single_thread;
    int spawned_single =
        pthread_create(&single_thread, NULL, single_write_thread_fn,
                       &single_task) == 0;
    if (!spawned_single)
      single_write_thread_fn(&single_task);

    int multi_rc = mmdoc_multi_write_from_chapters(
        inputs, toc_anchor_locations, page_indices, page_count,
        anchor_locations, outputs, detect_worker_count(page_count));

    if (spawned_single)
      pthread_join(single_thread, NULL);

    if (single_task.result != 0 || multi_rc != 0)
      goto cleanup;
  }

  if (0 != mmdoc_copy_img_files(&img_files, inputs))
    goto cleanup;

  result = 0;

cleanup:
  if (outputs != NULL) {
    for (size_t k = 0; k < page_count; k++)
      chapter_outputs_free(&outputs[k]);
    free(outputs);
  }
  if (parsed != NULL) {
    for (size_t i = 0; i < md_files.used; i++) {
      free(parsed[i].title);
      free(parsed[i].anchors.array);
      if (parsed[i].arena_live)
        mmdoc_arena_destroy(&parsed[i].arena);
    }
    free(parsed);
  }
  free(chapter_file_indices);
  free(page_indices);
  free_anchor_location_array(&chapters_by_source);
  free_array(&toc_refs);
  free_array(&md_files);
  free_array(&img_files);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array_deep(&anchor_locations);
  mmdoc_inputs_free(&inputs);
  return result;
}
