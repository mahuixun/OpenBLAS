#include "relapack.h"
#include "stdlib.h"

static void RELAPACK_cgbtrf_rec(const blasint *, const blasint *, const blasint *,
    const blasint *, float *, const blasint *, blasint *, float *, const blasint *, float *,
    const blasint *, blasint *);


/** CGBTRF computes an LU factorization of a complex m-by-n band matrix A using partial pivoting with row interchanges.
 *
 * This routine is functionally equivalent to LAPACK's cgbtrf.
 * For details on its interface, see
 * http://www.netlib.org/lapack/explore-html/d0/d3a/cgbtrf_8f.html
 * */
void RELAPACK_cgbtrf(
    const blasint *m, const blasint *n, const blasint *kl, const blasint *ku,
    float *Ab, const blasint *ldAb, blasint *ipiv,
    blasint *info
) {

    // Check arguments
    *info = 0;
    if (*m < 0)
        *info = -1;
    else if (*n < 0)
        *info = -2;
    else if (*kl < 0)
        *info = -3;
    else if (*ku < 0)
        *info = -4;
    else if (*ldAb < 2 * *kl + *ku + 1)
        *info = -6;
    if (*info) {
        const blasint minfo = -*info;
        LAPACK(xerbla)("CGBTRF", &minfo, strlen("CGBTRF"));
        return;
    }

    if (*m == 0 || *n == 0) return;
    // Constant
    const float ZERO[] = { 0., 0. };

    // Result upper band width
    const blasint kv = *ku + *kl;

    // Unskew A
    const blasint ldA[] = { *ldAb - 1 };
    float *const A = Ab + 2 * kv;

    // Zero upper diagonal fill-in elements
    blasint i, j;
    for (j = 0; j < *n; j++) {
        float *const A_j = A + 2 * *ldA * j;
        for (i = MAX(0, j - kv); i < j - *ku; i++)
            A_j[2 * i] = A_j[2 * i + 1] = 0.;
    }

    // Allocate work space
    const blasint n1 = CREC_SPLIT(*n);
    const blasint mWorkl = abs ( (kv > n1) ? MAX(1, *m - *kl) : kv);
    const blasint nWorkl = abs ( (kv > n1) ? n1 : kv);
    const blasint mWorku = abs ((*kl > n1) ? n1 : *kl);
    const blasint nWorku = abs ((*kl > n1) ? MAX(0, *n - *kl) : *kl);
    float *Workl = malloc(mWorkl * nWorkl * 2 * sizeof(float));
    float *Worku = malloc(mWorku * nWorku * 2 * sizeof(float));
    LAPACK(claset)("L", &mWorkl, &nWorkl, ZERO, ZERO, Workl, &mWorkl);
    LAPACK(claset)("U", &mWorku, &nWorku, ZERO, ZERO, Worku, &mWorku);

    // Recursive kernel
    RELAPACK_cgbtrf_rec(m, n, kl, ku, Ab, ldAb, ipiv, Workl, &mWorkl, Worku, &mWorku, info);

    // Free work space
    free(Workl);
    free(Worku);
}


/** cgbtrf's recursive compute kernel */
static void RELAPACK_cgbtrf_rec(
    const blasint *m, const blasint *n, const blasint *kl, const blasint *ku,
    float *Ab, const blasint *ldAb, blasint *ipiv,
    float *Workl, const blasint *ldWorkl, float *Worku, const blasint *ldWorku,
    blasint *info
) {

    if (*n <= MAX(CROSSOVER_CGBTRF, 1)|| *n > *kl || *ldAb == 1) {
        // Unblocked
        LAPACK(cgbtf2)(m, n, kl, ku, Ab, ldAb, ipiv, info);
        return;
    }

    // Constants
    const float ONE[]  = { 1., 0. };
    const float MONE[] = { -1., 0. };
    const blasint   iONE[] = { 1 };

    // Loop iterators
    blasint i, j;

    // Output upper band width
    const blasint kv = *ku + *kl;

    // Unskew A
    const blasint ldA[] = { *ldAb - 1 };
    float *const A = Ab + 2 * kv;

    // Splitting
    const blasint n1  = MIN(CREC_SPLIT(*n), *kl);
    const blasint n2  = *n - n1;
    const blasint m1  = MIN(n1, *m);
    const blasint m2  = *m - m1;
    const blasint mn1 = MIN(m1, n1);
    const blasint mn2 = MIN(m2, n2);

    // Ab_L *
    //      Ab_BR
    float *const Ab_L  = Ab;
    float *const Ab_BR = Ab + 2 * *ldAb * n1;

    // A_L A_R
    float *const A_L = A;
    float *const A_R = A + 2 * *ldA * n1;

    // A_TL A_TR
    // A_BL A_BR
    float *const A_TL = A;
    float *const A_TR = A + 2 * *ldA * n1;
    float *const A_BL = A                 + 2 * m1;
    float *const A_BR = A + 2 * *ldA * n1 + 2 * m1;

    // ipiv_T
    // ipiv_B
    blasint *const ipiv_T = ipiv;
    blasint *const ipiv_B = ipiv + n1;

    // Banded splitting
    const blasint n21 = MIN(n2, kv - n1);
    const blasint n22 = MIN(n2 - n21, n1);
    const blasint m21 = MIN(m2, *kl - m1);
    const blasint m22 = MIN(m2 - m21, m1);

    //   n1 n21  n22
    // m *  A_Rl ARr
    float *const A_Rl = A_R;
    float *const A_Rr = A_R + 2 * *ldA * n21;

    //     n1    n21    n22
    // m1  *     A_TRl  A_TRr
    // m21 A_BLt A_BRtl A_BRtr
    // m22 A_BLb A_BRbl A_BRbr
    float *const A_TRl  = A_TR;
    float *const A_TRr  = A_TR + 2 * *ldA * n21;
    float *const A_BLt  = A_BL;
    float *const A_BLb  = A_BL                  + 2 * m21;
    float *const A_BRtl = A_BR;
    float *const A_BRtr = A_BR + 2 * *ldA * n21;
    float *const A_BRbl = A_BR                  + 2 * m21;
    float *const A_BRbr = A_BR + 2 * *ldA * n21 + 2 * m21;

    // recursion(Ab_L, ipiv_T)
    RELAPACK_cgbtrf_rec(m, &n1, kl, ku, Ab_L, ldAb, ipiv_T, Workl, ldWorkl, Worku, ldWorku, info);

    // Workl = A_BLb
    LAPACK(clacpy)("U", &m22, &n1, A_BLb, ldA, Workl, ldWorkl);

    // partially redo swaps in A_L
    for (i = 0; i < mn1; i++) {
        const blasint ip = ipiv_T[i] - 1;
        if (ip != i) {
            if (ip < *kl)
                BLAS(cswap)(&i, A_L + 2 * i, ldA, A_L + 2 * ip, ldA);
            else
                BLAS(cswap)(&i, A_L + 2 * i, ldA, Workl + 2 * (ip - *kl), ldWorkl);
        }
    }

    // apply pivots to A_Rl
    LAPACK(claswp)(&n21, A_Rl, ldA, iONE, &mn1, ipiv_T, iONE);

    // apply pivots to A_Rr columnwise
    for (j = 0; j < n22; j++) {
        float *const A_Rrj = A_Rr + 2 * *ldA * j;
        for (i = j; i < mn1; i++) {
            const blasint ip = ipiv_T[i] - 1;
            if (ip != i) {
                const float tmpr = A_Rrj[2 * i];
                const float tmpc = A_Rrj[2 * i + 1];
                A_Rrj[2 * i]     = A_Rrj[2 * ip];
                A_Rrj[2 * i + 1] = A_Rr[2 * ip + 1];
                A_Rrj[2 * ip]     = tmpr;
                A_Rrj[2 * ip + 1] = tmpc;
            }
        }
    }

    // A_TRl = A_TL \ A_TRl
    BLAS(ctrsm)("L", "L", "N", "U", &m1, &n21, ONE, A_TL, ldA, A_TRl, ldA);
    // Worku = A_TRr
    LAPACK(clacpy)("L", &m1, &n22, A_TRr, ldA, Worku, ldWorku);
    // Worku = A_TL \ Worku
    BLAS(ctrsm)("L", "L", "N", "U", &m1, &n22, ONE, A_TL, ldA, Worku, ldWorku);
    // A_TRr = Worku
    LAPACK(clacpy)("L", &m1, &n22, Worku, ldWorku, A_TRr, ldA);
    // A_BRtl = A_BRtl - A_BLt * A_TRl
    BLAS(cgemm)("N", "N", &m21, &n21, &n1, MONE, A_BLt, ldA, A_TRl, ldA, ONE, A_BRtl, ldA);
    // A_BRbl = A_BRbl - Workl * A_TRl
    BLAS(cgemm)("N", "N", &m22, &n21, &n1, MONE, Workl, ldWorkl, A_TRl, ldA, ONE, A_BRbl, ldA);
    // A_BRtr = A_BRtr - A_BLt * Worku
    BLAS(cgemm)("N", "N", &m21, &n22, &n1, MONE, A_BLt, ldA, Worku, ldWorku, ONE, A_BRtr, ldA);
    // A_BRbr = A_BRbr - Workl * Worku
    BLAS(cgemm)("N", "N", &m22, &n22, &n1, MONE, Workl, ldWorkl, Worku, ldWorku, ONE, A_BRbr, ldA);

    // partially undo swaps in A_L
    for (i = mn1 - 1; i >= 0; i--) {
        const blasint ip = ipiv_T[i] - 1;
        if (ip != i) {
            if (ip < *kl)
                BLAS(cswap)(&i, A_L + 2 * i, ldA, A_L + 2 * ip, ldA);
            else
                BLAS(cswap)(&i, A_L + 2 * i, ldA, Workl + 2 * (ip - *kl), ldWorkl);
        }
    }

    // recursion(Ab_BR, ipiv_B)
    //RELAPACK_cgbtrf_rec(&m2, &n2, kl, ku, Ab_BR, ldAb, ipiv_B, Workl, ldWorkl, Worku, ldWorku, info);
       LAPACK(cgbtf2)(&m2, &n2, kl, ku, Ab_BR, ldAb, ipiv_B, info);
       
    if (*info)
        *info += n1;
    // shift pivots
    for (i = 0; i < mn2; i++)
        ipiv_B[i] += n1;
}
