#include "rbush.h"
// Emscripten's JS boundary uses pointers for C structs passed by value.
extern "C" {
rbush_tree* rbw_create(int max_entries) { rbush_tree* t=nullptr; return rbush_create(max_entries,&t)==RBUSH_OK ? t : nullptr; }
rbush_status rbw_insert(rbush_tree* t, const rbush_item* i) { return rbush_insert(t,*i); }
rbush_status rbw_remove(rbush_tree* t, const rbush_item* i, rbush_equals_fn eq, int32_t* removed, uint64_t* identity) { return rbush_remove_ex(t,*i,eq,nullptr,removed,identity); }
rbush_status rbw_search(const rbush_tree* t, const rbush_box* b, rbush_item* i, size_t cap, size_t* count) { return rbush_search(t,*b,i,cap,count); }
rbush_status rbw_search_result(const rbush_tree* t, const rbush_box* b, rbush_result** result) { return rbush_search_result(t,*b,result); }
rbush_status rbw_collides(const rbush_tree* t, const rbush_box* b, int32_t* result) { return rbush_collides(t,*b,result); }
}
