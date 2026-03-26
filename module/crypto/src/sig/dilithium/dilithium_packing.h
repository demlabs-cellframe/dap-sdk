#ifndef PACKING_H
#define PACKING_H

#include "dilithium_polyvec.h"

void dilithium_pack_pk(unsigned char [], const unsigned char [], const polyveck *, dilithium_param_t *);
void dilithium_pack_sk(unsigned char [], const unsigned char [], const unsigned char [], const unsigned char [],
             const polyvecl *, const polyveck *, const polyveck *, dilithium_param_t *);

void dilithium_pack_sig(unsigned char [], const polyvecl *, const polyveck *, const poly *, dilithium_param_t *);

void dilithium_unpack_pk(unsigned char [], polyveck *, const unsigned char [], dilithium_param_t *);

void dilithium_unpack_sk(unsigned char [], unsigned char [], unsigned char [],
               polyvecl *, polyveck *, polyveck *, const unsigned char [], dilithium_param_t *);

int dilithium_unpack_sig(polyvecl *, polyveck *, poly *, const unsigned char [], dilithium_param_t *);

/* FIPS 204 variants: c_tilde is raw bytes, packing uses per-level gamma1/gamma2 */
void mldsa_pack_sk(unsigned char sk[], const unsigned char rho[],
                   const unsigned char key[], const unsigned char tr[],
                   const polyvecl *s1, const polyveck *s2,
                   const polyveck *t0, const dilithium_param_t *p);
void mldsa_unpack_sk(unsigned char rho[], unsigned char key[],
                     unsigned char tr[], polyvecl *s1,
                     polyveck *s2, polyveck *t0,
                     const unsigned char sk[], const dilithium_param_t *p);
void mldsa_pack_sig(unsigned char sig[], const unsigned char *c_tilde,
                    const polyvecl *z, const polyveck *h, const dilithium_param_t *p);
int mldsa_unpack_sig(unsigned char *c_tilde, polyvecl *z, polyveck *h,
                     const unsigned char sig[], const dilithium_param_t *p);

#endif
