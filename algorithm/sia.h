#ifndef SIAH_H
#define SIAH_H

#include "miner.h"

extern void sia_gen_hash(const unsigned char *data, unsigned int len, unsigned char *hash);
extern void sia_regenhash(struct work *work);

#endif /* FRESHH_H */