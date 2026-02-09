#ifndef __RS_H_
#define __RS_H_

#include <stdint.h>

#define DATA_SHARDS_MAX 255

typedef struct _reed_solomon {
    int ds;
    int ps;
    int ts;
    uint8_t p[];
} reed_solomon;

#define reed_solomon_bufsize(ds, ps) (sizeof(reed_solomon) + 2 * (ps) * (ds))
#define reed_solomon_reconstruct reed_solomon_decode

void reed_solomon_init(void);
reed_solomon *reed_solomon_new_static(void *buf, size_t len, int ds, int ps);
reed_solomon *reed_solomon_new(int data_shards, int parity_shards);
void reed_solomon_release(reed_solomon *rs);

int reed_solomon_encode(reed_solomon *rs, uint8_t **shards, int nr_shards, int bs);
int reed_solomon_decode(reed_solomon *rs, uint8_t **shards, uint8_t *marks, int nr_shards, int bs);

#endif
