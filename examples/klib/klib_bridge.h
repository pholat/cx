#ifndef KLIB_BRIDGE_H
#define KLIB_BRIDGE_H

/* Integer-handle facade over a couple of klib containers, meant to be called
 * from Cx (which has no pointer types). Each handle indexes an internal table;
 * 0 is never a valid handle. See klib_bridge.c for the klib glue. */

/* ---- list of ints (klib klist) ---- */
int  kl_list_new(void);
void kl_list_free(int handle);
int  kl_list_size(int handle);
int  kl_list_empty(int handle);
void kl_list_push(int handle, int value);
int  kl_list_get(int handle, int index);
int  kl_list_shift(int handle, int fallback);

/* ---- int -> int map (klib khash) ---- */
int  kl_map_new(void);
void kl_map_free(int handle);
int  kl_map_size(int handle);
int  kl_map_empty(int handle);
void kl_map_put(int handle, int key, int value);
int  kl_map_get(int handle, int key, int fallback);
int  kl_map_has(int handle, int key);
void kl_map_del(int handle, int key);

#endif /* KLIB_BRIDGE_H */
