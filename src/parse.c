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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stream.h"
#include "private.h"

static int
tok(struct stream *stream, const char *id)
{
	efp_stream_skip_space(stream);

	if (efp_stream_eol(stream))
		return 0;

	if (strncmp(efp_stream_get_ptr(stream), id, strlen(id)) == 0)
		return efp_stream_advance(stream, strlen(id));

	return 0;
}

static inline int
tok_stop(struct stream *stream)
{
	if (tok(stream, "STOP")) {
		efp_stream_next_line(stream);
		return 1;
	}
	return 0;
}

static inline int
tok_end(struct stream *stream)
{
	if (tok(stream, "$END")) {
		efp_stream_next_line(stream);
		return 1;
	}
	return 0;
}

static int
skip_label(struct stream *stream)
{
	efp_stream_skip_space(stream);
	efp_stream_skip_nonspace(stream);

	return efp_stream_current_char(stream) != '\0';
}

static int
tok_label(struct stream *stream, size_t size, char *val)
{
	efp_stream_skip_space(stream);

	if (efp_stream_eol(stream))
		return 0;

	const char *start = efp_stream_get_ptr(stream);
	efp_stream_skip_nonspace(stream);
	const char *end = efp_stream_get_ptr(stream);
	memset(val, 0, size);

	for (size_t i = 0; start < end && i < size - 1; i++)
		*val++ = *start++;

	return start == end;
}

static int
tok_int(struct stream *stream, int *val)
{
	return efp_stream_parse_int(stream, val);
}

static int
tok_uint(struct stream *stream, size_t *val)
{
	int x;

	if (!tok_int(stream, &x))
		return 0;

	if (x < 0)
		return 0;

	if (val)
		*val = (size_t)x;

	return 1;
}

static int
tok_double(struct stream *stream, double *val)
{
	return efp_stream_parse_double(stream, val);
}

static enum efp_result
parse_coordinates(struct frag *frag, struct stream *stream)
{
	efp_stream_next_line(stream);

	while (!efp_stream_eof(stream)) {
		if (tok_stop(stream)) {
			if (frag->n_atoms < 1)
				return EFP_RESULT_SYNTAX_ERROR;

			return EFP_RESULT_SUCCESS;
		}

		struct efp_atom atom;

		if (!tok_label(stream, sizeof(atom.label), atom.label) ||
		    !tok_double(stream, &atom.x) ||
		    !tok_double(stream, &atom.y) ||
		    !tok_double(stream, &atom.z) ||
		    !tok_double(stream, &atom.mass) ||
		    !tok_double(stream, &atom.znuc))
			return EFP_RESULT_SYNTAX_ERROR;

		if (!eq(atom.mass, 0.0)) {
			frag->n_atoms++;
			frag->atoms = realloc(frag->atoms,
				frag->n_atoms * sizeof(struct efp_atom));
			if (!frag->atoms)
				return EFP_RESULT_NO_MEMORY;
			memcpy(frag->atoms + frag->n_atoms - 1, &atom,
				sizeof(struct efp_atom));
		}

		frag->n_multipole_pts++;
		frag->multipole_pts = realloc(frag->multipole_pts,
			frag->n_multipole_pts * sizeof(struct multipole_pt));
		if (!frag->multipole_pts)
			return EFP_RESULT_NO_MEMORY;

		struct multipole_pt *last_pt =
			frag->multipole_pts + frag->n_multipole_pts - 1;

		memset(last_pt, 0, sizeof(struct multipole_pt));
		last_pt->x = atom.x, last_pt->y = atom.y, last_pt->z = atom.z;

		efp_stream_next_line(stream);
	}
	return EFP_RESULT_SYNTAX_ERROR;
}

static enum efp_result
parse_monopoles(struct frag *frag, struct stream *stream)
{
	if (!frag->multipole_pts)
		return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);

	for (size_t i = 0; i < frag->n_multipole_pts; i++) {
		if (!skip_label(stream) ||
		    !tok_double(stream, &frag->multipole_pts[i].monopole) ||
		    !tok_double(stream, NULL))
			return EFP_RESULT_SYNTAX_ERROR;
		efp_stream_next_line(stream);
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_dipoles(struct frag *frag, struct stream *stream)
{
	if (!frag->multipole_pts)
		return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);

	for (size_t i = 0; i < frag->n_multipole_pts; i++) {
		if (!skip_label(stream) ||
		    !tok_double(stream, &frag->multipole_pts[i].dipole.x) ||
		    !tok_double(stream, &frag->multipole_pts[i].dipole.y) ||
		    !tok_double(stream, &frag->multipole_pts[i].dipole.z))
			return EFP_RESULT_SYNTAX_ERROR;
		efp_stream_next_line(stream);
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_quadrupoles(struct frag *frag, struct stream *stream)
{
	if (!frag->multipole_pts)
		return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);

	for (size_t i = 0; i < frag->n_multipole_pts; i++) {
		if (!skip_label(stream))
			return EFP_RESULT_SYNTAX_ERROR;

		double *q = frag->multipole_pts[i].quadrupole;

		for (size_t j = 0; j < 6; j++)
			if (!tok_double(stream, q + j))
				return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_octupoles(struct frag *frag, struct stream *stream)
{
	if (!frag->multipole_pts)
		return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);

	for (size_t i = 0; i < frag->n_multipole_pts; i++) {
		if (!skip_label(stream))
			return EFP_RESULT_SYNTAX_ERROR;

		double *o = frag->multipole_pts[i].octupole;

		for (size_t j = 0; j < 10; j++)
			if (!tok_double(stream, o + j))
				return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_polarizable_pts(struct frag *frag, struct stream *stream)
{
	efp_stream_next_line(stream);

	while (!efp_stream_eof(stream)) {
		if (tok_stop(stream))
			return EFP_RESULT_SUCCESS;

		frag->n_polarizable_pts++;

		size_t size = sizeof(struct polarizable_pt);
		frag->polarizable_pts = realloc(frag->polarizable_pts,
			frag->n_polarizable_pts * size);
		if (!frag->polarizable_pts)
			return EFP_RESULT_NO_MEMORY;

		struct polarizable_pt *pt =
			frag->polarizable_pts + frag->n_polarizable_pts - 1;

		if (!efp_stream_advance(stream, 4))
			return EFP_RESULT_SYNTAX_ERROR;

		if (!tok_double(stream, &pt->x) ||
		    !tok_double(stream, &pt->y) ||
		    !tok_double(stream, &pt->z))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
		double m[9];

		for (size_t i = 0; i < 9; i++)
			if (!tok_double(stream, m + i))
				return EFP_RESULT_SYNTAX_ERROR;

		pt->tensor.xx = m[0];
		pt->tensor.yy = m[1];
		pt->tensor.zz = m[2];
		pt->tensor.xy = m[3];
		pt->tensor.xz = m[4];
		pt->tensor.yz = m[5];
		pt->tensor.yx = m[6];
		pt->tensor.zx = m[7];
		pt->tensor.zy = m[8];

		efp_stream_next_line(stream);
	}

	return EFP_RESULT_SYNTAX_ERROR;
}

static enum efp_result
parse_dynamic_polarizable_pts(struct frag *frag, struct stream *stream)
{
	efp_stream_next_line(stream);

	while (!efp_stream_eof(stream)) {
		frag->n_dynamic_polarizable_pts++;

		size_t size = sizeof(struct dynamic_polarizable_pt);
		frag->dynamic_polarizable_pts = realloc(
			frag->dynamic_polarizable_pts,
			frag->n_dynamic_polarizable_pts * size);
		if (!frag->dynamic_polarizable_pts)
			return EFP_RESULT_NO_MEMORY;

		struct dynamic_polarizable_pt *pt =
			frag->dynamic_polarizable_pts +
			frag->n_dynamic_polarizable_pts - 1;

		if (!efp_stream_advance(stream, 5))
			return EFP_RESULT_SYNTAX_ERROR;

		if (!tok_double(stream, &pt->x) ||
		    !tok_double(stream, &pt->y) ||
		    !tok_double(stream, &pt->z))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
		double m[9];

		for (size_t j = 0; j < 9; j++)
			if (!tok_double(stream, m + j))
				return EFP_RESULT_SYNTAX_ERROR;

		pt->trace[0] = (m[0] + m[1] + m[2]) / 3.0;

		efp_stream_next_line(stream);

		if (efp_stream_eof(stream))
			return EFP_RESULT_SYNTAX_ERROR;

		if (strstr(efp_stream_get_ptr(stream), "FOR"))
			break;
	}

	if (efp_stream_eof(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	for (size_t w = 1; w < 12; w++) {
		for (size_t i = 0; i < frag->n_dynamic_polarizable_pts; i++) {
			struct dynamic_polarizable_pt *pt =
				frag->dynamic_polarizable_pts + i;

			if (!efp_stream_advance(stream, 5))
				return EFP_RESULT_SYNTAX_ERROR;

			if (!tok_double(stream, &pt->x) ||
			    !tok_double(stream, &pt->y) ||
			    !tok_double(stream, &pt->z))
				return EFP_RESULT_SYNTAX_ERROR;

			efp_stream_next_line(stream);
			double m[9];

			for (size_t j = 0; j < 9; j++)
				if (!tok_double(stream, m + j))
					return EFP_RESULT_SYNTAX_ERROR;

			pt->trace[w] = (m[0] + m[1] + m[2]) / 3.0;
			efp_stream_next_line(stream);
		}
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_projection_basis(struct frag *frag, struct stream *stream)
{
	double x, y, z;

	efp_stream_next_line(stream);

	while (!efp_stream_eof(stream)) {
		if (tok_stop(stream))
			return EFP_RESULT_SUCCESS;

		if (!efp_stream_advance(stream, 8))
			return EFP_RESULT_SYNTAX_ERROR;

		if (!tok_double(stream, &x) ||
		    !tok_double(stream, &y) ||
		    !tok_double(stream, &z))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
shell:
		if (efp_stream_eof(stream))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_skip_space(stream);

		if (efp_stream_eol(stream)) {
			efp_stream_next_line(stream);
			continue;
		}

		frag->n_xr_shells++;
		frag->xr_shells = realloc(frag->xr_shells,
				frag->n_xr_shells * sizeof(struct shell));

		struct shell *shell = frag->xr_shells + frag->n_xr_shells - 1;

		shell->x = x, shell->y = y, shell->z = z;
		shell->type = efp_stream_get_char(stream);

		if (!strchr("SLPDF", shell->type))
			return EFP_RESULT_SYNTAX_ERROR;

		if (!tok_uint(stream, &shell->n_funcs))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);

		shell->coef = malloc((shell->type == 'L' ? 3 : 2) *
					shell->n_funcs * sizeof(double));

		double *ptr = shell->coef;

		for (size_t i = 0; i < shell->n_funcs; i++) {
			if (!tok_int(stream, NULL) ||
			    !tok_double(stream, ptr++) ||
			    !tok_double(stream, ptr++))
				return EFP_RESULT_SYNTAX_ERROR;

			if (shell->type == 'L')
				if (!tok_double(stream, ptr++))
					return EFP_RESULT_SYNTAX_ERROR;

			efp_stream_next_line(stream);
		}
		goto shell;
	}

	return EFP_RESULT_SYNTAX_ERROR;
}

static enum efp_result
parse_multiplicity(struct frag *frag, struct stream *stream)
{
	if (!tok_int(stream, &frag->multiplicity))
		return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_projection_wf(struct frag *frag, struct stream *stream)
{
	if (!tok_uint(stream, &frag->n_lmo) ||
	    !tok_uint(stream, &frag->xr_wf_size))
		return EFP_RESULT_SYNTAX_ERROR;

	frag->xr_wf = malloc(frag->n_lmo * frag->xr_wf_size * sizeof(double));
	if (!frag->xr_wf)
		return EFP_RESULT_NO_MEMORY;

	efp_stream_next_line(stream);
	double *ptr = frag->xr_wf;

	for (size_t j = 0; j < frag->n_lmo; j++) {
		for (size_t i = 0; i < frag->xr_wf_size / 5; i++) {
			if (!efp_stream_advance(stream, 5))
				return EFP_RESULT_SYNTAX_ERROR;

			for (size_t k = 0; k < 5; k++)
				if (!tok_double(stream, ptr++))
					return EFP_RESULT_SYNTAX_ERROR;

			efp_stream_next_line(stream);
		}

		if (frag->xr_wf_size % 5 == 0)
			continue;

		if (!efp_stream_advance(stream, 5))
			return EFP_RESULT_SYNTAX_ERROR;

		for (size_t k = 0; k < frag->xr_wf_size % 5; k++)
			if (!tok_double(stream, ptr++))
				return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
	}

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_fock_mat(struct frag *frag, struct stream *stream)
{
	efp_stream_next_line(stream);

	size_t size = frag->n_lmo * (frag->n_lmo + 1) / 2;
	frag->xr_fock_mat = malloc(size * sizeof(double));
	if (!frag->xr_fock_mat)
		return EFP_RESULT_NO_MEMORY;

	for (size_t i = 0; i < size; i++)
		if (!tok_double(stream, frag->xr_fock_mat + i))
			return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);
	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_lmo_centroids(struct frag *frag, struct stream *stream)
{
	efp_stream_next_line(stream);

	frag->lmo_centroids = malloc(frag->n_lmo * sizeof(vec_t));
	if (!frag->lmo_centroids)
		return EFP_RESULT_NO_MEMORY;

	for (size_t i = 0; i < frag->n_lmo; i++) {
		vec_t *ct = frag->lmo_centroids + i;

		if (!skip_label(stream) ||
		    !tok_double(stream, &ct->x) ||
		    !tok_double(stream, &ct->y) ||
		    !tok_double(stream, &ct->z))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_canonvec(struct frag *frag, struct stream *stream)
{
	(void)frag;

	size_t wf_size;

	if (!tok_uint(stream, NULL) ||
	    !tok_uint(stream, &wf_size))
		return EFP_RESULT_SYNTAX_ERROR;

	efp_stream_next_line(stream);

	for (size_t j = 0; j < wf_size; j++) {
		for (size_t i = 0; i <= (wf_size - 1) / 5; i++) {
			efp_stream_next_line(stream);
		}
	}

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_canonfok(struct frag *frag, struct stream *stream)
{
	(void)frag;

	efp_stream_next_line(stream);

	while (!efp_stream_eof(stream)) {
		if (tok_stop(stream))
			return EFP_RESULT_SUCCESS;
		efp_stream_next_line(stream);
	}

	return EFP_RESULT_SYNTAX_ERROR;
}

static enum efp_result
parse_screen(struct frag *frag, struct stream *stream)
{
	double *scr = malloc(frag->n_multipole_pts * sizeof(double));
	if (!scr)
		return EFP_RESULT_NO_MEMORY;

	char type = efp_stream_get_char(stream);

	if (type == '\0' || isspace(type)) {
		if (frag->ai_screen_params)
			return EFP_RESULT_SYNTAX_ERROR;
		frag->ai_screen_params = scr;
	}
	else if (type == '2') {
		if (frag->screen_params)
			return EFP_RESULT_SYNTAX_ERROR;
		frag->screen_params = scr;
	}
	else {
		efp_log("unsupported screen group in EFP data file");
		return EFP_RESULT_FATAL;
	}

	efp_stream_next_line(stream);

	for (size_t i = 0; i < frag->n_multipole_pts; i++) {
		if (!skip_label(stream) ||
		    !tok_double(stream, NULL) ||
		    !tok_double(stream, scr + i))
			return EFP_RESULT_SYNTAX_ERROR;

		efp_stream_next_line(stream);
	}

	if (!tok_stop(stream))
		return EFP_RESULT_SYNTAX_ERROR;

	return EFP_RESULT_SUCCESS;
}

static enum efp_result
parse_ff_types(struct frag *frag, struct stream *stream)
{
	while (!efp_stream_eof(stream)) {
		efp_stream_next_line(stream);

		if (tok_stop(stream))
			return EFP_RESULT_SUCCESS;

		char label[32];
		struct ff_atom atom;

		if (sscanf(efp_stream_get_ptr(stream), "%32s %32s", label, atom.type) < 2)
			return EFP_RESULT_SYNTAX_ERROR;

		for (size_t i = 0; i < frag->n_atoms; i++) {
			if (strcmp(label, frag->atoms[i].label) == 0) {
				atom.idx = i;
				break;
			}
			if (i == frag->n_atoms - 1)
				return EFP_RESULT_SYNTAX_ERROR;
		}

		size_t size = (frag->n_ff_atoms + 1) * sizeof(struct ff_atom);

		if ((frag->ff_atoms = realloc(frag->ff_atoms, size)) == NULL)
			return EFP_RESULT_NO_MEMORY;

		frag->ff_atoms[frag->n_ff_atoms] = atom;
		frag->n_ff_atoms++;
	}

	return EFP_RESULT_SYNTAX_ERROR;
}

static enum efp_result
parse_ff_bonds(struct frag *frag, struct stream *stream)
{
	while (!efp_stream_eof(stream)) {
		efp_stream_next_line(stream);

		if (tok_stop(stream))
			return EFP_RESULT_SUCCESS;

		struct ff_link link;
		char label1[32], label2[32];

		if (sscanf(efp_stream_get_ptr(stream), "%32s %32s", label1, label2) < 2)
			return EFP_RESULT_SYNTAX_ERROR;

		for (size_t i = 0; i < frag->n_ff_atoms; i++) {
			if (strcmp(label1, frag->atoms[frag->ff_atoms[i].idx].label) == 0) {
				link.idx1 = i;
				break;
			}
			if (i == frag->n_ff_atoms - 1)
				return EFP_RESULT_SYNTAX_ERROR;
		}

		for (size_t i = 0; i < frag->n_ff_atoms; i++) {
			if (strcmp(label2, frag->atoms[frag->ff_atoms[i].idx].label) == 0) {
				link.idx2 = i;
				break;
			}
			if (i == frag->n_ff_atoms - 1)
				return EFP_RESULT_SYNTAX_ERROR;
		}

		size_t size = (frag->n_ff_links + 1) * sizeof(struct ff_link);

		if ((frag->ff_links = realloc(frag->ff_links, size)) == NULL)
			return EFP_RESULT_NO_MEMORY;

		frag->ff_links[frag->n_ff_links] = link;
		frag->n_ff_links++;
	}

	return EFP_RESULT_SYNTAX_ERROR;
}

typedef enum efp_result (*parse_fn)(struct frag *, struct stream *);

static parse_fn
get_parse_fn(struct stream *stream)
{
	static const struct {
		const char *label;
		parse_fn fn;
	} funcs[] = {
		{ "COORDINATES",                parse_coordinates },
		{ "MONOPOLES",                  parse_monopoles },
		{ "DIPOLES",                    parse_dipoles },
		{ "QUADRUPOLES",                parse_quadrupoles },
		{ "OCTUPOLES",                  parse_octupoles },
		{ "POLARIZABLE POINTS",         parse_polarizable_pts },
		{ "DYNAMIC POLARIZABLE POINTS", parse_dynamic_polarizable_pts },
		{ "PROJECTION BASIS SET",       parse_projection_basis },
		{ "MULTIPLICITY",               parse_multiplicity },
		{ "PROJECTION WAVEFUNCTION",    parse_projection_wf },
		{ "FOCK MATRIX ELEMENTS",       parse_fock_mat },
		{ "LMO CENTROIDS",              parse_lmo_centroids },
		{ "CANONVEC",                   parse_canonvec },
		{ "CANONFOK",                   parse_canonfok },
		{ "SCREEN",                     parse_screen },
		{ "FFTYPES",                    parse_ff_types },
		{ "FFBONDS",                    parse_ff_bonds }
	};

	for (size_t i = 0; i < ARRAY_SIZE(funcs); i++)
		if (tok(stream, funcs[i].label))
			return funcs[i].fn;

	return NULL;
}

static enum efp_result
parse_fragment(struct frag *frag, struct stream *stream)
{
	enum efp_result res;

	while (!efp_stream_eof(stream)) {
		parse_fn fn = get_parse_fn(stream);

		if (!fn) {
			if (tok_end(stream))
				return EFP_RESULT_SUCCESS;

			efp_log("unknown keyword in EFP potential data file");
			return EFP_RESULT_SYNTAX_ERROR;
		}

		if ((res = fn(frag, stream)))
			return res;
	}

	efp_log("unexpected end of EFP potential data file");
	return EFP_RESULT_SYNTAX_ERROR;
}

static enum efp_result
parse_file(struct efp *efp, struct stream *stream)
{
	char name[32];
	enum efp_result res;

	while (!efp_stream_eof(stream)) {
		if (efp_stream_get_char(stream) == '\0' ||
		    efp_stream_get_char(stream) != '$') {
			efp_stream_next_line(stream);
			continue;
		}

		if (!tok_label(stream, sizeof(name), name))
			return EFP_RESULT_SYNTAX_ERROR;

		if (efp_find_lib(efp, name)) {
			efp_log("parameters for fragment %s are already loaded", name);
			return EFP_RESULT_FATAL;
		}

		struct frag *frag = calloc(1, sizeof(struct frag));
		if (!frag)
			return EFP_RESULT_NO_MEMORY;

		efp->n_lib++;
		efp->lib = realloc(efp->lib, efp->n_lib * sizeof(struct frag *));
		if (!efp->lib)
			return EFP_RESULT_NO_MEMORY;

		frag->lib = frag;
		strcpy(frag->name, name);
		efp->lib[efp->n_lib - 1] = frag;

		efp_stream_next_line(stream);
		efp_stream_next_line(stream);

		if ((res = parse_fragment(frag, stream)))
			return res;
	}

	return EFP_RESULT_SUCCESS;
}

EFP_EXPORT enum efp_result
efp_add_potential(struct efp *efp, const char *path)
{
	enum efp_result res;
	struct stream *stream;

	assert(efp);
	assert(path);

	if ((stream = efp_stream_open(path)) == NULL) {
		efp_log("unable to open file %s", path);
		return (EFP_RESULT_FILE_NOT_FOUND);
	}

	efp_stream_set_split_char(stream, '>');
	efp_stream_next_line(stream);
	res = parse_file(efp, stream);
	efp_stream_close(stream);

	return (res);
}
