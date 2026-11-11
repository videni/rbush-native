#ifndef RBUSH_C_H
#define RBUSH_C_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rbush_tree rbush_tree;
typedef struct rbush_result rbush_result;
typedef struct rbush_box { double min_x, min_y, max_x, max_y; } rbush_box;
/* identity models an upstream object reference, not a unique tree entry. The
   same identity can occur repeatedly. Different identities may have equal boxes. */
typedef struct rbush_item { double min_x, min_y, max_x, max_y; uint64_t identity; } rbush_item;
typedef enum rbush_status {
    RBUSH_OK = 0, RBUSH_INVALID_ARGUMENT = 1, RBUSH_BUFFER_TOO_SMALL = 2,
    RBUSH_ALLOCATION_FAILED = 3, RBUSH_INTERNAL_ERROR = 4
} rbush_status;
/* Synchronous only; callback/context are never retained. Arguments match
   upstream equalsFn(requestedItem, candidateItem). */
typedef int (*rbush_equals_fn)(uint64_t requested, uint64_t candidate, void *context);
/* Per-tree ordering hooks. NULL selects the original core coordinate comparator.
   The callback/context pointers are stored, but their memory is caller-owned:
   keep them valid until replacement or tree destruction. clear/from_json retain
   the configured hooks. Callbacks must not mutate/reenter the same tree. */
typedef double (*rbush_compare_fn)(uint64_t lhs, uint64_t rhs, void *context);
/* Leaf child_offset/count address items; internal offset/count address the
   child_indices buffer, whose entries are indices into nodes. Root index is 0.
   Bounds, order, height and leaf flags are exported exactly, including infinities. */
typedef struct rbush_node {
    double min_x, min_y, max_x, max_y;
    uint64_t child_offset, child_count;
    int32_t height, leaf;
} rbush_node;

rbush_status rbush_create(int32_t max_entries, rbush_tree **out_tree);
void rbush_destroy(rbush_tree *tree);
rbush_status rbush_set_comparators(rbush_tree *tree, rbush_compare_fn compare_min_x,
    rbush_compare_fn compare_min_y, void *context);
rbush_status rbush_get_comparators(const rbush_tree *tree, rbush_compare_fn *compare_min_x,
    rbush_compare_fn *compare_min_y, void **context);
rbush_status rbush_get_capacity(const rbush_tree *tree, int32_t *max_entries, int32_t *min_entries);
rbush_status rbush_clear(rbush_tree *tree);
rbush_status rbush_insert(rbush_tree *tree, rbush_item item);
rbush_status rbush_load(rbush_tree *tree, const rbush_item *items, size_t count);
rbush_status rbush_remove(rbush_tree *tree, rbush_item item, rbush_equals_fn equals, void *context);
/* Extended remove reports one removed occurrence so language bindings can
   release their payload reference promptly. Both output pointers are optional. */
rbush_status rbush_remove_ex(rbush_tree *tree, rbush_item item, rbush_equals_fn equals, void *context,
    int32_t *out_removed, uint64_t *out_removed_identity);
/* Single-pass queries. Result owns immutable PODs, independently of tree;
   data remains valid until result_destroy. No persistent query cache. */
rbush_status rbush_all_result(const rbush_tree *tree, rbush_result **out_result);
rbush_status rbush_search_result(const rbush_tree *tree, rbush_box box, rbush_result **out_result);
size_t rbush_result_count(const rbush_result *result);
const rbush_item *rbush_result_data(const rbush_result *result);
void rbush_result_destroy(rbush_result *result);
/* out_count always receives required length. NULL/0 is a size query. Buffers
   are caller-owned; on BUFFER_TOO_SMALL no partial results are written. */
rbush_status rbush_all(const rbush_tree *tree, rbush_item *items, size_t capacity, size_t *out_count);
rbush_status rbush_search(const rbush_tree *tree, rbush_box box, rbush_item *items, size_t capacity, size_t *out_count);
rbush_status rbush_collides(const rbush_tree *tree, rbush_box box, int32_t *out_collides);
rbush_status rbush_export_counts(const rbush_tree *tree, size_t *nodes, size_t *items, size_t *child_indices);
rbush_status rbush_to_json(const rbush_tree *tree,
    rbush_node *nodes, size_t node_capacity, rbush_item *items, size_t item_capacity,
    uint64_t *child_indices, size_t child_capacity);
rbush_status rbush_from_json(rbush_tree *tree,
    const rbush_node *nodes, size_t node_count, const rbush_item *items, size_t item_count,
    const uint64_t *child_indices, size_t child_count);
/* Integration helpers: separate from original RBush operations. Root bounds
   are O(1); out_empty distinguishes the empty infinite sentinel. Clone is O(N),
   duplicates keep identity but nodes and mutable item geometry are independent.
   Comparator pointers/context are copied without taking context ownership. */
rbush_status rbush_root_bounds(const rbush_tree *tree, rbush_box *out_bounds, int32_t *out_empty);
rbush_status rbush_clone(const rbush_tree *tree, rbush_tree **out_tree);
/* Predicate is synchronous and unretained. Nonzero stops immediately. Optional
   counters report actual visited nodes and tested item boxes, not result count. */
typedef int (*rbush_predicate_fn)(uint64_t identity, void *context);
rbush_status rbush_any(const rbush_tree *tree, rbush_box bounds, rbush_predicate_fn predicate,
    void *context, int32_t *out_found, size_t *out_node_visits, size_t *out_item_tests);
/* Coarse summary covers all entries when budgets are positive. Node visits and
   leaf-entry geometry reads are each <=max_node_visits (temporary frontier
   <=2*max_node_visits). No full export or unbounded item scan. Refinement precedes
   local coalescing and final closest-pair compression, independently bounding
   output<=max_regions. out_node_visits counts nodes, not leaf-entry reads.
   The extended form additionally reports the separately budgeted entry reads.
   Zero budgets produce no regions. */
rbush_status rbush_regions(const rbush_tree *tree, size_t max_regions, size_t max_node_visits,
    double min_span_ratio, rbush_box *regions, size_t capacity, size_t *out_count, size_t *out_node_visits);
rbush_status rbush_regions_ex(const rbush_tree *tree, size_t max_regions, size_t max_node_visits,
    double min_span_ratio, rbush_box *regions, size_t capacity, size_t *out_count,
    size_t *out_node_visits, size_t *out_entry_reads);

/* Thread-local diagnostic for most recent failed call; copied/owned by library.
   Calls on a handle require external synchronization. No C++ exception crosses ABI. */
const char *rbush_last_error(void);
#ifdef __cplusplus
}
#endif
#endif
