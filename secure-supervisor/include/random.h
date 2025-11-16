#ifndef SECURE_RANDOM_H
#define SECURE_RANDOM_H

void trng_init(void);
int trng_getrandom(unsigned char *out, unsigned len);

#endif // SECURE_RANDOM_H

