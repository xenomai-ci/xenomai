/**
 * @file
 * Analogy for Linux, device, subdevice, etc. related features
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2014 Jorge A. Ramirez-Ortiz <jro@xenomai.org>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <rtdm/analogy.h>
#include <stdio.h>
#include <errno.h>
#include "iniparser/iniparser.h"
#include "boilerplate/ancillaries.h"
#include "boilerplate/list.h"
#include "calibration.h"

#define CHK(func, ...)								\
do {										\
	int rc = func(__VA_ARGS__);						\
	if (rc < 0) 								\
		return -1;							\
} while (0)

static lsampl_t data32_get(void *src)
{
	return (lsampl_t) * ((lsampl_t *) (src));
}

static lsampl_t data16_get(void *src)
{
	return (lsampl_t) * ((sampl_t *) (src));
}

static lsampl_t data8_get(void *src)
{
	return (lsampl_t) * ((unsigned char *)(src));
}

static void data32_set(void *dst, lsampl_t val)
{
	*((lsampl_t *) (dst)) = val;
}

static void data16_set(void *dst, lsampl_t val)
{
	*((sampl_t *) (dst)) = (sampl_t) (0xffff & val);
}

static void data8_set(void *dst, lsampl_t val)
{
	*((unsigned char *)(dst)) = (unsigned char)(0xff & val);
}

static inline int read_dbl(double *d, struct _dictionary_ *f,const char *subd,
			   int subd_idx, char *type, int type_idx)
{
	char *str;
	int ret;

	/* if only contains doubles as coefficients */
	if (strncmp(type, COEFF_STR, strlen(COEFF_STR) != 0))
		return -ENOENT;

	ret = asprintf(&str, COEFF_FMT, subd, subd_idx, type, type_idx);
	if (ret < 0)
		return ret;

	*d = iniparser_getdouble(f, str, -255.0);
	if (*d == -255.0)
		ret = -ENOENT;
	free(str);

	return ret;
}

static inline int read_int(int *val, struct _dictionary_ *f, const char *subd,
			   int subd_idx, char *type)
{
	char *str;
	int ret;

	ret = (subd_idx >= 0) ?
	      asprintf(&str, ELEMENT_FIELD_FMT, subd, subd_idx, type):
	      asprintf(&str, ELEMENT_FMT, subd, type);
	if (ret < 0)
		return ret;

	*val = iniparser_getint(f, str, 0xFFFF);
	if (*val == 0xFFFF)
		ret = -ENOENT;
	free(str);

	return ret;
}

static inline int read_str(char **val, struct _dictionary_ *f, const char *subd,
			   const char *type)
{
	char *str;
	int ret;

	ret = asprintf(&str, ELEMENT_FMT, subd, type);
	if (ret < 0)
		return ret;

	*val = (char *) iniparser_getstring(f, str, NULL);
	if (*val == NULL)
		ret = -ENOENT;
	free(str);

	return ret;
}

static inline void write_calibration(FILE *file, char *fmt, ...)
{
	va_list ap;

	if (!file)
		return;

	va_start(ap, fmt);
	vfprintf(file, fmt, ap);
	fflush(file);
	va_end(ap);
}

void
write_calibration_file(FILE *dst, struct listobj *l,
		       struct a4l_calibration_subdev *subd, a4l_desc_t *desc)
{
	struct subdevice_calibration_node *e, *t;
	int i, j = 0;

	if (list_empty(l))
		return;

	/* TODO: modify the meaning of board/driver in the proc */
	if (desc) {
		write_calibration(dst, "[%s] \n",PLATFORM_STR);
		write_calibration(dst, DRIVER_STR" = %s;\n", desc->board_name);
		write_calibration(dst, BOARD_STR" = %s;\n", desc->driver_name);
	}

	write_calibration(dst, "\n[%s] \n", subd->name);
	write_calibration(dst, INDEX_STR" = %d;\n", subd->idx);
	list_for_each_entry_safe(e, t, l, node) {
		j++;
	}
	write_calibration(dst, ELEMENTS_STR" = %d;\n", j);

	j = 0;
	list_for_each_entry_safe(e, t, l, node) {
		write_calibration(dst, "[%s_%d] \n", subd->name, j);
		write_calibration(dst, CHANNEL_STR" = %d;\n", e->channel);
		write_calibration(dst, RANGE_STR" = %d;\n", e->range);
		write_calibration(dst, EXPANSION_STR" = %g;\n",
				  e->polynomial->expansion_origin);
		write_calibration(dst, NBCOEFF_STR"= %d;\n",
				  e->polynomial->nb_coefficients);

		for (i = 0; i < e->polynomial->nb_coefficients; i++)
			write_calibration(dst, COEFF_STR"_%d = %g;\n",
					  i,
					  e->polynomial->coefficients[i]);
		j++;
	}

	return;
}

/*!
 * @ingroup analogy_lib_level2
 * @defgroup analogy_lib_calibration Software calibration API
 * @{
 */

/**
 * @brief Read the analogy generated calibration file
 *
 * @param[in] name Name of the calibration file
 * @param[out] data Pointer to the calibration file contents
 *
 */

int a4l_read_calibration_file(char *name, struct a4l_calibration_data *data)
{
	const char *subdevice[2] = { AI_SUBD_STR, AO_SUBD_STR };
	int i, j, k, index = -1, nb_elements = -1;
	struct a4l_calibration_subdev_data *p = NULL;
	struct _dictionary_ *d;
	struct stat st;

	if (access(name, R_OK))
		return -1;

	if (stat(name, &st) || !st.st_size)
		return -1;

	d = iniparser_load(name);
	if (d == NULL)
		return -1;

	CHK(read_str, &data->driver_name, d, PLATFORM_STR, DRIVER_STR);
	CHK(read_str, &data->board_name, d, PLATFORM_STR, BOARD_STR);

	for (k = 0; k < ARRAY_LEN(subdevice); k++) {
		read_int(&nb_elements, d, subdevice[k], -1, ELEMENTS_STR);
		if (nb_elements < 0 ) {
			/* AO is optional */
			if (!strncmp(subdevice[k], AO_SUBD_STR, sizeof(AO_SUBD_STR)))
			     break;
			return -1;
		}

		CHK(read_int, &index, d, subdevice[k], -1, INDEX_STR);

		if (strncmp(subdevice[k], AI_SUBD_STR,
			    strlen(AI_SUBD_STR)) == 0) {
			data->ai = malloc(nb_elements *
					  sizeof(struct a4l_calibration_subdev_data));
			data->nb_ai = nb_elements;
			p  = data->ai;
		}

		if (strncmp(subdevice[k], AO_SUBD_STR,
			    strlen(AO_SUBD_STR)) == 0) {
			data->ao = malloc(nb_elements *
					  sizeof(struct a4l_calibration_subdev_data));
			data->nb_ao = nb_elements;
			p = data->ao;
		}

		for (i = 0; i < nb_elements; i++) {
			CHK(read_int, &p->expansion, d, subdevice[k], i,
				 EXPANSION_STR);
			CHK(read_int, &p->nb_coeff, d, subdevice[k], i,
				 NBCOEFF_STR);
			CHK(read_int, &p->channel, d, subdevice[k], i,
				 CHANNEL_STR);
			CHK(read_int, &p->range, d, subdevice[k], i,
				 RANGE_STR);

			p->coeff = malloc(p->nb_coeff * sizeof(double));

			for (j = 0; j < p->nb_coeff; j++) {
				CHK(read_dbl,&p->coeff[j], d, subdevice[k], i,
					 COEFF_STR, j);
			}

			p->index = index;
			p++;
		}
	}


	return 0;
}

/**
 * @brief Get the polynomial that will be use for the software calibration
 *
 * @param[out] converter Polynomial to be used on the software calibration
 * @param[in] subd Subdevice index
 * @param[in] chan Channel
 * @param[in] range Range
 * @param[in] data Calibration data read from the calibration file
 *
 * @return -1 on error
 *
 */

int a4l_get_softcal_converter(struct a4l_polynomial *converter,
			      int subd, int chan, int range,
			      struct a4l_calibration_data *data )
{
	int i;

	for (i = 0; i < data->nb_ai; i++) {
		if (data->ai[i].index != subd)
			break;
		if ((data->ai[i].channel == chan || data->ai[i].channel == -1)
		    &&
		    (data->ai[i].range == range || data->ai[i].range == -1)) {
			converter->expansion = data->ai[i].expansion;
			converter->nb_coeff = data->ai[i].nb_coeff;
			converter->coeff = data->ai[i].coeff;
			converter->order = data->ai[i].nb_coeff - 1;
			return 0;
		}
	}

	for (i = 0; i < data->nb_ao; i++) {
		if (data->ao[i].index != subd)
			break;
		if ((data->ao[i].channel == chan || data->ao[i].channel == -1)
		    &&
		    (data->ao[i].range == range || data->ao[i].range == -1)) {
			converter->expansion = data->ao[i].expansion;
			converter->nb_coeff = data->ao[i].nb_coeff;
			converter->coeff = data->ao[i].coeff;
			converter->order = data->ao[i].nb_coeff - 1;
			return 0;
		}
	}

	return -1;
}

/**
 * @brief Convert raw data (from the driver) to calibrated double units
 * @param[in] chan Channel descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 * @param[in] converter Conversion polynomial
 *
 *
 * @return the count of conversion performed, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, rng and the pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_rawtodcal()
 *
 *
 *
 */

int a4l_rawtodcal(a4l_chinfo_t *chan, double *dst, void *src,
		  int cnt, struct a4l_polynomial *converter)
{
	int i = 0, j = 0, k = 0;
	double term = 1.0;
	lsampl_t tmp;
	int size;

	/* Temporary data accessor */
	lsampl_t(*datax_get) (void *);

	/* Basic checking */
	if (chan == NULL)
		return -EINVAL;

	/* Find out the size in memory */
	size = a4l_sizeof_chan(chan);

	/* Get the suitable accessor */
	switch (a4l_sizeof_chan(chan)) {
	case 4:
		datax_get = data32_get;
		break;
	case 2:
		datax_get = data16_get;
		break;
	case 1:
		datax_get = data8_get;
		break;
	default:
		return -EINVAL;
	};

	while (j < cnt) {
		/* Properly retrieve the data */
		tmp = datax_get(src + i);

		/* Perform the conversion */
		dst[j] = 0.0;
		term = 1.0;
		for (k = 0; k < converter->nb_coeff; k++) {
			dst[j] += converter->coeff[k] * term;
			term *= tmp - converter->expansion;
		}

		/* Update the counters */
		i += size;
		j++;
	}

	return j;
}

/**
 * @brief Convert double values to raw calibrated data using polynomials
 *
 * @param[in] chan Channel descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 * @param[in] converter Conversion polynomial
 *
 * @return the count of conversion performed, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, rng and the pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_dcaltoraw()
 *
 */

int a4l_dcaltoraw( a4l_chinfo_t * chan, void *dst, double *src, int cnt,
		   struct a4l_polynomial *converter)
{
	int size, i = 0, j = 0, k = 0;
	double value, term;

	/* Temporary data accessor */
	void (*datax_set) (void *, lsampl_t);

	/* Basic checking */
	if (chan == NULL)
		return -EINVAL;

	/* Find out the size in memory */
	size = a4l_sizeof_chan(chan);

	/* Select the suitable accessor */
	switch (size) {
	case 4:
		datax_set = data32_set;
		break;
	case 2:
		datax_set = data16_set;
		break;
	case 1:
		datax_set = data8_set;
		break;
	default:
		return -EINVAL;
	};

	while (j < cnt) {

		/* Performs the conversion */
		value = 0.0;
		term = 1.0;
		for (k = 0; k < converter->nb_coeff; k++) {
			value += converter->coeff[k] * term;
			term *= src[j] - converter->expansion;
		}
		value = nearbyint(value);

		datax_set(dst + i, (lsampl_t) value);

		/* Updates the counters */
		i += size;
		j++;
	}

	return j;
}

/** @} Calibration API */

