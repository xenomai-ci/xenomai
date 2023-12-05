#include <math.h>

/*
 * Copyright (C) 2014 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <rtdm/analogy.h>

struct vec {
	unsigned dim;
	unsigned stride;
	double *val;
};

struct mat {
	unsigned rows;
	unsigned cols;
	double *val;
};

static void vec_init(struct vec *v, unsigned dim, double *val)
{
	v->dim = dim;
	v->stride = 1;
	v->val = val;
}

static int vec_alloc(struct vec *v, unsigned dim)
{
	double *val = malloc(sizeof(*val) * dim);

	if (val == NULL)
		return -ENOMEM;

	v->dim = dim;
	v->stride = 1;
	v->val = val;

	return 0;
}

static void vec_free(struct vec *v)
{
	free(v->val);
	v->val = NULL;
	v->dim = 0;
	v->stride = 0;
}

static void row_vec_init(struct vec *v, struct mat *m, unsigned row)
{
	v->dim = m->cols;
	v->stride = 1;
	v->val = m->val + row * m->cols;
}

static void col_vec_init(struct vec *v, struct mat *m, unsigned col)
{
	v->dim = m->rows;
	v->stride = m->cols;
	v->val = m->val + col;
}

static void vec_copy(struct vec *dst, struct vec *src)
{
	const unsigned d_stride = dst->stride, s_stride = src->stride;
	double *d_val = dst->val, *s_val = src->val;
	unsigned dim = dst->dim;
	unsigned i;

	assert(dst->dim == src->dim);

	for (i = 0; i < dim; i++, d_val += d_stride, s_val += s_stride)
		*d_val = *s_val;
}

static inline double *vec_of(struct vec *v, unsigned k)
{
	return v->val + v->stride * k;
}

static void vec_mult_scalar(struct vec *v, double x)
{
	const unsigned v_stride = v->stride;
	const unsigned dim = v->dim;
	double *v_val = v->val;
	unsigned i;

	for (i = 0; i < dim; i++, v_val += v_stride)
		*v_val *= x;
}

static double vec_dot(struct vec *l, struct vec *r)
{
	const unsigned l_stride = l->stride;
	const unsigned r_stride = r->stride;
	const double *l_val = l->val;
	const double *r_val = r->val;
	const unsigned dim = l->dim;
	double res = 0;
	unsigned i;

	assert(dim == r->dim);

	for (i = 0; i < dim; i++, l_val += l_stride, r_val += r_stride)
		res += (*l_val) * (*r_val);

	return res;
}

static inline double vec_norm2(struct vec *v)
{
	return sqrt(vec_dot(v, v));
}

static void vec_vandermonde(struct vec *v, const double x)
{
	const unsigned v_stride = v->stride;
	const unsigned dim = v->dim;
	double *v_val = v->val;
	double tmp = 1;
	unsigned i;

	for (i = 0; i < dim; i++, v_val += v_stride, tmp *= x)
		*v_val = tmp;
}

static void vec_householder(struct vec *res, struct vec *v, unsigned k)
{
	const unsigned res_stride = res->stride;
	const unsigned v_stride = v->stride;
	const unsigned dim = res->dim;
	const double *v_val = v->val;
	double *x_k, *res_val = res->val;
	double alpha;
	unsigned j;

	assert(dim == v->dim);
	assert(k < dim);

	for (j = 0; j < k; j++, res_val += res_stride, v_val += v_stride)
		*res_val = 0;
	x_k = res_val;
	for (j = k; j < dim; j++, res_val += res_stride, v_val += v_stride)
		*res_val = *v_val;

	alpha = (signbit(*x_k) ? 1 : -1) * vec_norm2(res);
	*x_k -= alpha;

	vec_mult_scalar(res, 1 / vec_norm2(res));
}

static int mat_alloc(struct mat *m, unsigned rows, unsigned cols)
{
	double *val = malloc(sizeof(*val) * rows * cols);

	if (val == NULL)
		return -ENOMEM;

	m->rows = rows;
	m->cols = cols;
	m->val = val;

	return 0;
}

static void mat_free(struct mat *m)
{
	free(m->val);
	m->val = NULL;
	m->cols = 0;
	m->rows = 0;
}

static inline double *mat_of(struct mat *m, unsigned row, unsigned col)
{
	return m->val + row * m->cols + col;
}

static void vec_mult_mat(struct vec *res, struct vec *v, struct mat *m)
{
	const unsigned res_stride = res->stride;
	const unsigned cols = m->cols;
	double *res_val = res->val;
	struct vec cur_col;
	unsigned i;

	assert(v->dim == m->rows);

	col_vec_init(&cur_col, m, 0);
	for (i = 0; i < cols; i++, cur_col.val++, res_val += res_stride)
		*res_val = vec_dot(v, &cur_col);
}

static void mat_vandermonde(struct mat *m, struct vec *v, const double origin)
{
	const unsigned v_stride = v->stride;
	const unsigned m_rows = m->rows;
	const unsigned m_cols = m->cols;
	const double *v_val = v->val;
	struct vec m_row;
	unsigned i;

	assert(m->rows == v->dim);

	row_vec_init(&m_row, m, 0);
	for (i = 0; i < m_rows; i++, m_row.val += m_cols, v_val += v_stride)
		vec_vandermonde(&m_row, *v_val - origin);
}

static void
house_mult_mat(struct mat *res, struct vec *tmp, struct vec *vh, struct mat *m)
{
	const double *vh_val = vh->val, *tmp_val = tmp->val, *m_val = m->val;
	const unsigned tmp_stride = tmp->stride;
	const unsigned vh_stride = vh->stride;
	const unsigned res_cols = res->cols;
	const unsigned res_rows = res->rows;
	double *res_val = res->val;
	unsigned i, j;

	assert(res_cols == m->cols &&
		res_rows == m->rows && res->rows == vh->dim);

	vec_mult_mat(tmp, vh, m);

	for (j = 0; j < res_rows; j++, vh_val += vh_stride, tmp_val = tmp->val)
		for (i = 0; i < res_cols; i++, tmp_val += tmp_stride)
			*res_val++ = (*m_val++) - 2 * (*vh_val) * (*tmp_val);
}

static void
house_mult_vec(struct vec *res, struct vec *vh, struct vec *v)
{
	const double *vh_val = vh->val, *v_val = v->val;
	const unsigned res_stride = res->stride;
	const unsigned vh_stride = vh->stride;
	const unsigned v_stride = v->stride;
	const unsigned res_dim = res->dim;
	double *res_val = res->val;
	double dot;
	unsigned i;

	assert(res_dim == v->dim);

	dot = 2 * vec_dot(vh, v);

	for (i = 0; i < res_dim; i++, res_val += res_stride,
		     v_val += v_stride, vh_val += vh_stride)
		*res_val = (*v_val) - dot * (*vh_val);
}

static void
mat_upper_triangular_backsub(struct vec *res, struct mat *m, struct vec *v)
{
	unsigned dim = res->dim;
	unsigned j;
	int i;

	assert(dim == m->cols);

	for (i = dim - 1; i >= 0; i--) {
		double sum = *vec_of(v, i);

		for (j = i + 1; j < dim; j++)
			sum -= (*mat_of(m, i, j)) * (*vec_of(res, j));

		*vec_of(res, i) = sum / (*mat_of(m, i, i));
	}
}

/*
 * A = Q.R decomposition using Householder reflections
 * Input: R <- A
 *	  Y
 * Output: R
 *	   Y <- Q^tY
 */
static int mat_qr(struct mat *r, struct vec *y)
{
	struct vec r_col, vh, tr;
	unsigned i;
	int rc;

	rc = vec_alloc(&vh, r->rows);
	if (rc < 0)
		return rc;

	rc = vec_alloc(&tr, y->dim);
	if (rc < 0)
		goto err_free_vh;

	col_vec_init(&r_col, r, 0);
	for (i = 0; i < r->cols; i++, r_col.val++) {
		vec_householder(&vh, &r_col, i);

		house_mult_vec(y, &vh, y);
		house_mult_mat(r, &tr, &vh, r);
	}

	rc = 0;
	vec_free(&tr);
  err_free_vh:
	vec_free(&vh);
	return rc;
}


/*!
 * @ingroup analogy_lib_level2
 * @defgroup analogy_lib_math Math API
 * @{
 */


/**
 * @brief Calculate the polynomial fit
 *
 * @param[in] r_dim Number of elements of the resulting polynomial
 * @param[out] r Polynomial
 * @param[in] orig
 * @param[in] dim Number of elements in the polynomials
 * @param[in] x Polynomial
 * @param[in] y Polynomial
 *
 *
 * Operation:
 *
 * We are looking for Res such that A.Res = Y, with A the Vandermonde
 * matrix made from the X vector.
 *
 * Using the least square method, this means finding Res such that:
 * A^t.A.Res = A^tY
 *
 * If we write A = Q.R with Q^t.Q = 1, and R non singular, this can be
 * reduced to:
 * R.Res = Q^t.Y
 *
 * mat_qr() gives us R and Q^t.Y from A and Y
 *
 * We can then obtain Res by back substitution using
 * mat_upper_triangular_backsub() with R upper triangular.
 *
 */
int a4l_math_polyfit(unsigned r_dim, double *r, double orig, const unsigned dim,
	        double *x, double *y)
{
	struct vec v_x, v_y, v_r, qty;
	struct mat vdm;
	int rc;

	vec_init(&v_x, dim, x);
	vec_init(&v_y, dim, y);
	vec_init(&v_r, r_dim, r);

	rc = vec_alloc(&qty, dim);
	if (rc < 0)
		return rc;
	vec_copy(&qty, &v_y);

	rc = mat_alloc(&vdm, dim, r_dim);
	if (rc < 0)
		goto err_free_qty;

	mat_vandermonde(&vdm, &v_x, orig);

	rc = mat_qr(&vdm, &qty);
	if (rc == 0)
		mat_upper_triangular_backsub(&v_r, &vdm, &qty);

	mat_free(&vdm);

  err_free_qty:
	vec_free(&qty);

        return rc;
}

/**
 * @brief Calculate the aritmetic mean of an array of values
 *
 * @param[out] pmean Pointer to the resulting value
 * @param[in] val Array of input values
 * @param[in] nr Number of array elements
 *
 */
void a4l_math_mean(double *pmean, double *val, unsigned nr)
{
	double sum;
	int i;

	for (sum = 0, i = 0; i < nr; i++)
		sum += val[i];

	*pmean = sum / nr;
}

/**
 * @brief Calculate the standard deviation of an array of values
 *
 * @param[out] pstddev Pointer to the resulting value
 * @param[in] mean Mean value
 * @param[in] val Array of input values
 * @param[in] nr Number of array elements
 *
 */
void a4l_math_stddev(double *pstddev, double mean, double *val, unsigned nr)
{
	double sum, sum_sq;
	int i;

	for (sum = 0, sum_sq = 0, i = 0; i < nr; i++) {
		double x = val[i] - mean;
		sum_sq += x * x;
		sum += x;
	}

	*pstddev = sqrt((sum_sq - (sum * sum) / nr) / (nr - 1));
}

/**
 * @brief Calculate the standard deviation of the mean
 *
 * @param[out] pstddevm Pointer to the resulting value
 * @param[in] mean Mean value
 * @param[in] val Array of input values
 * @param[in] nr Number of array elements
 *
 */
void a4l_math_stddev_of_mean(double *pstddevm, double mean, double *val, unsigned nr)
{
	a4l_math_stddev(pstddevm, mean, val, nr);
	*pstddevm = *pstddevm / sqrt(nr); 
}


/** @} Math API */
