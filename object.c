// object.c — Content-addressable object store

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(id_out->hash, &ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── object_write ────────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    if (type == OBJ_BLOB)        type_str = "blob";
    else if (type == OBJ_TREE)   type_str = "tree";
    else                          type_str = "commit";

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);

    size_t full_len = (size_t)header_len + 1 + len;
    uint8_t *full = malloc(full_len);
    if (!full) return -1;

    memcpy(full, header, header_len);
    full[header_len] = '\0';
    memcpy(full + header_len + 1, data, len);

    compute_hash(full, full_len, id_out);

    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    // Step 5: Create shard directory
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char shard_dir[256];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755);

    // Step 6: Write to temp file
    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full); return -1; }

    ssize_t written = write(fd, full, full_len);
    free(full);

    if (written < 0 || (size_t)written != full_len) {
        close(fd); unlink(tmp_path); return -1;
    }

    // Step 7: fsync + rename atomically
    if (fsync(fd) < 0) { close(fd); unlink(tmp_path); return -1; }
    close(fd);

    if (rename(tmp_path, final_path) < 0) { unlink(tmp_path); return -1; }

    // Step 8: fsync the shard directory
    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) { fsync(dir_fd); close(dir_fd); }

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out,
                void **data_out, size_t *len_out) {
    (void)id; (void)type_out; (void)data_out; (void)len_out;
    return -1;
}
