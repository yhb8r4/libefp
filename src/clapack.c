/*-
 * Copyright (c) 2012-2013 Ilya Kaliman
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>

#include "clapack.h"

void dsyev_(char *,
	    char *,
	    int *,
	    double *,
	    int *,
	    double *,
	    double *,
	    int *,
	    int *);

void dgesv_(int *,
	    int *,
	    double *,
	    int *,
	    int *,
	    double *,
	    int *,
	    int *);

int efp_dsyev(char jobz, char uplo, int n, double *a, int lda, double *w)
{
	int info, lwork;
	double *work;

	lwork = n * n;
	work = malloc(lwork * sizeof(double));

	dsyev_(&jobz, &uplo, &n, a, &lda, w, work, &lwork, &info);

	free(work);
	return (info);
}

int efp_dgesv(int n, int nrhs, double *a, int lda, int *ipiv, double *b, int ldb)
{
	int info;

	dgesv_(&n, &nrhs, a, &lda, ipiv, b, &ldb, &info);

	return (info);
}