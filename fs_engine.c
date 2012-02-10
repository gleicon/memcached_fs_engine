/*
 * fs_engine for memcached as per trondn tutorial
 * http://trondn.blogspot.com/2010/10/writing-your-own-storage-engine-for.html
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memcached/visibility.h>
#include <memcached/engine.h>
#include <memcached/util.h>


struct fs_engine {
    ENGINE_HANDLE_V1 engine;
};

struct fs_item {
    void *key;
    size_t nkey;
    void *data;
    size_t ndata;
    int flags;
    rel_time_t exptime;
};

static void fs_destroy(ENGINE_HANDLE *h) {
    free(h);
}

#define DATA_DIR "/tmp/memcached_fs_storage"

char *get_file_name(void *key, size_t nkey) {
    char *fname;
    int fn_size=0;
    fn_size = nkey + strlen(DATA_DIR) +1;
    fname = malloc(fn_size);
    memset(fname, 0, fn_size);
    snprintf(fname, fn_size, "%s/%s", DATA_DIR, (unsigned char *)key);
    fprintf(stderr, "saving data to %s\n", fname);
    return fname;
}

static void fs_item_release(ENGINE_HANDLE* handle,
                            const void *cookie,
                            item* item) {
    struct fs_item *it = item;
    free(it->key);
    free(it->data);
    free(it);
}


static ENGINE_ERROR_CODE fs_initialize(ENGINE_HANDLE *h, const char* config_str) {
    mkdir(DATA_DIR, 0755);
    chdir(DATA_DIR);
    return ENGINE_SUCCESS;
}

static ENGINE_ERROR_CODE fs_allocate(ENGINE_HANDLE* handle,
                                     const void* cookie,
                                     item **item,
                                     const void* key,
                                     const size_t nkey,
                                     const size_t nbytes,
                                     const int flags,
                                     const rel_time_t exptime) {

    struct fs_item *it = malloc(sizeof(struct fs_item));
    if (it == NULL) return ENGINE_ENOMEM;
    it->flags = flags;
    it->exptime = exptime;
    it->nkey = nkey;
    it->ndata = nbytes;
    it->key = malloc(nkey);
    it->data = malloc(nbytes);

    if (it->key == NULL || it->data == NULL) {
        free(it->key);
        free(it->data);
        free(it);
        return ENGINE_ENOMEM;
    }

    memcpy(it->key, key, nkey);
    *item = it;
    return ENGINE_SUCCESS;
}

static bool fs_get_item_info(ENGINE_HANDLE *handle, const void *cookie,
                             const item* item, item_info *item_info) {
    struct fs_item* it = (struct fs_item*)item;
    if (item_info->nvalue < 1) return false;

    item_info->cas = 0; /* Not supported */
    item_info->clsid = 0; /* Not supported */
    item_info->exptime = it->exptime;
    item_info->flags = it->flags;
    item_info->key = it->key;
    item_info->nkey = it->nkey;
    item_info->nbytes = it->ndata; /* Total length of the items data */
    item_info->nvalue = 1; /* Number of fragments used */
    item_info->value[0].iov_base = it->data; /* pointer to fragment 1 */
    item_info->value[0].iov_len = it->ndata; /* Length of fragment 1 */

    return true;
}

static ENGINE_ERROR_CODE fs_store(ENGINE_HANDLE* handle,
                                  const void *cookie,
                                  item* item,
                                  uint64_t *cas,
                                  ENGINE_STORE_OPERATION operation,
                                  uint16_t vbucket) {
    struct fs_item* it = item;
    
    FILE *fp = fopen(get_file_name(it->key, it->nkey), "w");
    if (fp == NULL) {
        perror("Error store");
        return ENGINE_NOT_STORED;
    }

    size_t nw = fwrite(it->data, 1, it->ndata, fp);
    fclose(fp);
    if (nw != it->ndata) {
      remove(get_file_name(it->key, it->nkey)); 
      return ENGINE_NOT_STORED;
    }

    *cas = 0;
    return ENGINE_SUCCESS;
}

static ENGINE_ERROR_CODE fs_get(ENGINE_HANDLE* handle,
                                const void* cookie,
                                item** item,
                                const void* key,
                                const int nkey,
                                uint16_t vbucket) {

    struct stat st;
    if (stat(get_file_name(&key, nkey), &st) == -1) return ENGINE_NOT_STORED;

    struct fs_item* it = NULL;
    ENGINE_ERROR_CODE ret = fs_allocate(handle, cookie, (void**)&it, key, nkey,
                                       st.st_size, 0, 0);
    if (ret != ENGINE_SUCCESS) return ENGINE_ENOMEM;

    FILE *fp = fopen(get_file_name(it->key, it->nkey), "r");
    if (fp == NULL) {
      perror("Error get");
      fs_item_release(handle, cookie, it);
      return ENGINE_FAILED;
    }

    size_t nr = fread(it->data, 1, it->ndata, fp);
    fclose(fp);
    if (nr != it->ndata) {
      fs_item_release(handle, cookie, it);
      return ENGINE_FAILED;
    }

    *item = it;
    return ENGINE_SUCCESS;
}

static const engine_info* fs_get_info(ENGINE_HANDLE* handle) {
   static engine_info info = {
      .description = "Filesystem engine v0.1",
      .num_features = 0
   };

   return &info;
}

static ENGINE_ERROR_CODE fs_item_delete(ENGINE_HANDLE* handle,
                                        const void* cookie,
                                        const void* key,
                                        const size_t nkey,
                                        uint64_t cas,
                                        uint16_t vbucket) {
   return ENGINE_KEY_ENOENT;
}

static ENGINE_ERROR_CODE fs_get_stats(ENGINE_HANDLE* handle,
                                      const void* cookie,
                                      const char* stat_key,
                                      int nkey,
                                      ADD_STAT add_stat) {
   return ENGINE_SUCCESS;
}

static ENGINE_ERROR_CODE fs_flush(ENGINE_HANDLE* handle,
                                  const void* cookie, time_t when) {

   return ENGINE_SUCCESS;
}

static void fs_reset_stats(ENGINE_HANDLE* handle, const void *cookie) {

}

static ENGINE_ERROR_CODE fs_unknown_command(ENGINE_HANDLE* handle,
                                            const void* cookie,
                                            protocol_binary_request_header *request,
                                            ADD_RESPONSE response) {
   return ENGINE_ENOTSUP;
}

static void fs_item_set_cas(ENGINE_HANDLE *handle, const void *cookie,
                            item* item, uint64_t val){

}


MEMCACHED_PUBLIC_API ENGINE_ERROR_CODE create_instance(uint64_t interface, GET_SERVER_API get_server_api, 
        ENGINE_HANDLE **handle) {

            if (interface == 0) return ENGINE_ENOTSUP;
            struct fs_engine *h = calloc(1, sizeof(*h));
            if (h == NULL) return ENGINE_ENOMEM;
            h->engine.interface.interface = 1;

            /* command handlers */
            h->engine.initialize = fs_initialize;
            h->engine.destroy = fs_destroy;
            h->engine.get_info = fs_get_info;
            h->engine.allocate = fs_allocate;
            h->engine.remove = fs_item_delete;
            h->engine.release = fs_item_release;
            h->engine.get = fs_get;
            h->engine.get_stats = fs_get_stats;
            h->engine.reset_stats = fs_reset_stats;
            h->engine.store = fs_store;
            h->engine.flush = fs_flush;
            h->engine.unknown_command = fs_unknown_command;
            h->engine.item_set_cas = fs_item_set_cas;
            h->engine.get_item_info = fs_get_item_info;

            *handle = (ENGINE_HANDLE *) h;
            return ENGINE_SUCCESS;
}

