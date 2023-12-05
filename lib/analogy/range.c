/**
 * @file
 * Analogy for Linux, range related features
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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

#include <errno.h>
#include "internal.h"
#include <rtdm/analogy.h>

#ifndef DOXYGEN_CPP

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

#endif /* !DOXYGEN_CPP */

/*!
 * @ingroup analogy_lib_level2
 * @defgroup analogy_lib_rng2 Range / conversion API
 * @{
 */

/**
 * @brief Get the size in memory of an acquired element
 *
 * According to the board, the channels have various acquisition
 * widths. With values like 8, 16 or 32, there is no problem finding
 * out the size in memory (1, 2, 4); however with widths like 12 or
 * 24, this function might be helpful to guess the size needed in RAM
 * for a single acquired element.
 *
 * @param[in] chan Channel descriptor
 *
 * @return the size in memory of an acquired element, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if the argument chan is NULL
 *
 */
int a4l_sizeof_chan(a4l_chinfo_t * chan)
{
	/* So far, it seems there is no 64 bit acquistion stuff */
	int i = 0, sizes[3] = {8, 16, 32};

	if (chan == NULL)
		return -EINVAL;

	while (i < 3 && sizes[i] < chan->nb_bits)
		i++;

	return (i == 3) ? -EINVAL : sizes[i] / 8;
}

/**
 * @brief Get the size in memory of a digital acquired element
 *
 * This function is only useful for DIO subdevices. Digital subdevices
 * are a specific kind of subdevice on which channels are regarded as
 * bits composing the subdevice's bitfield. During a DIO acquisition,
 * all bits are sampled. Therefore, a4l_sizeof_chan() is useless in
 * this case and we have to use a4l_sizeof_subd().
 * With bitfields which sizes are 8, 16 or 32, there is no problem
 * finding out the size in memory (1, 2, 4); however with widths like
 * 12 or 24, this function might be helpful to guess the size needed
 * in RAM for a single acquired element.
 *
 * @param[in] subd Subdevice descriptor
 *
 * @return the size in memory of an acquired element, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if the argument chan is NULL or if the
 *    subdevice is not a digital subdevice
 *
 */
int a4l_sizeof_subd(a4l_sbinfo_t *subd)
{
	/* So far, it seems there is no 64 bit acquistion stuff */
	int i = 0, sizes[3] = {8, 16, 32};

	if (subd == NULL)
		return -EINVAL;

	/* This function is only useful for DIO subdevice (all
	   channels are acquired in one shot); for other kind of
	   subdevice, the user must use a4l_sizeof_chan() so as to
	   find out the size of the channel he wants to use */
	if ((subd->flags & A4L_SUBD_TYPES) != A4L_SUBD_DIO &&
	    (subd->flags & A4L_SUBD_TYPES) != A4L_SUBD_DI &&
	    (subd->flags & A4L_SUBD_TYPES) != A4L_SUBD_DO)
		return -EINVAL;

	while (i < 3 && sizes[i] < subd->nb_chan)
		i++;

	return (i == 3) ? -EINVAL : sizes[i] / 8;
}

/**
 * @brief Find the must suitable range
 *
 * @param[in] dsc Device descriptor filled by a4l_open() and
 * a4l_fill_desc()
 *
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] idx_chan Index of the concerned channel
 * @param[in] unit Unit type used in the range
 * @param[in] min Minimal limit value
 * @param[in] max Maximal limit value
 * @param[out] rng Found range
 *
 * @return The index of the most suitable range on success. Otherwise:
 *
 * - -ENOENT is returned if a suitable range is not found.
 * - -EINVAL is returned if some argument is missing or wrong;
 *    idx_subd, idx_chan and the dsc pointer should be checked; check
 *    also the kernel log ("dmesg"); WARNING: a4l_fill_desc() should
 *    be called before using a4l_find_range()
 *
 */
int a4l_find_range(a4l_desc_t * dsc,
		   unsigned int idx_subd,
		   unsigned int idx_chan,
		   unsigned long unit,
		   double min, double max, a4l_rnginfo_t ** rng)
{
	int i, ret;
	long lmin, lmax;
	a4l_chinfo_t *chinfo;
	a4l_rnginfo_t *rnginfo, *tmp = NULL;
	unsigned int idx_rng = -ENOENT;

	if (rng != NULL)
		*rng = NULL;

	/* Basic checkings */
	if (dsc == NULL)
		return -EINVAL;

	/* a4l_fill_desc() must have been called on this descriptor */
	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	/* Retrieves the ranges count */
	ret = a4l_get_chinfo(dsc, idx_subd, idx_chan, &chinfo);
	if (ret < 0)
		return ret;

	/* Initializes variables */
	lmin = (long)(min * A4L_RNG_FACTOR);
	lmax = (long)(max * A4L_RNG_FACTOR);

	/* Performs the research */
	for (i = 0; i < chinfo->nb_rng; i++) {

		ret = a4l_get_rnginfo(dsc, idx_subd, idx_chan, i, &rnginfo);
		if (ret < 0)
			return ret;

		if (A4L_RNG_UNIT(rnginfo->flags) == unit &&
		    rnginfo->min <= lmin && rnginfo->max >= lmax) {

			if (tmp != NULL) {
				if (rnginfo->min >= tmp->min &&
				    rnginfo->max <= tmp->max) {
					idx_rng = i;
					tmp = rnginfo;
				}
			} else {
				idx_rng = i;
				tmp = rnginfo;
			}
		}
	}

	if (rng != NULL)
		*rng = tmp;

	return idx_rng;
}

/**
 * @brief Unpack raw data (from the driver) into unsigned long values
 *
 * This function takes as input driver-specific data and scatters each
 * element into an entry of an unsigned long table. It is a
 * convenience routine which performs no conversion, just copy.
 *
 * @param[in] chan Channel descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of transfer to copy
 *
 * @return the count of copy performed, otherwise a negative error
 * code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, dst and src pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_ultoraw()
 *
 */
int a4l_rawtoul(a4l_chinfo_t * chan, unsigned long *dst, void *src, int cnt)
{
	int size, i = 0, j = 0;

	/* Temporary data accessor */
	lsampl_t(*datax_get) (void *);

	/* Basic checking */
	if (chan == NULL)
		return -EINVAL;

	/* Find out the size in memory */
	size = a4l_sizeof_chan(chan);

	/* Get the suitable accessor */
	switch (size) {
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

		/* Properly copy the data */
		dst[j] = (unsigned long)datax_get(src + i);

		/* Update the counters */
		i += size;
		j++;
	}

	return j;
}

/**
 * @brief Convert raw data (from the driver) to float-typed samples
 *
 * @param[in] chan Channel descriptor
 * @param[in] rng Range descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 *
 * @return the count of conversion performed, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, rng and the pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_rawtod()
 *
 */
int a4l_rawtof(a4l_chinfo_t * chan,
	       a4l_rnginfo_t * rng, float *dst, void *src, int cnt)
{
	int size, i = 0, j = 0;
	lsampl_t tmp;

	/* Temporary values used for conversion
	   (phys = a * src + b) */
	float a, b;
	/* Temporary data accessor */
	lsampl_t(*datax_get) (void *);

	/* Basic checking */
	if (rng == NULL || chan == NULL)
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

	/* Compute the translation factor and the constant only once */
	a = ((float)(rng->max - rng->min)) /
		(((1ULL << chan->nb_bits) - 1) * A4L_RNG_FACTOR);
	b = ((float)rng->min) / A4L_RNG_FACTOR;

	while (j < cnt) {

		/* Properly retrieve the data */
		tmp = datax_get(src + i);

		/* Perform the conversion */
		dst[j] = a * tmp + b;

		/* Update the counters */
		i += size;
		j++;
	}

	return j;
}

/**
 * @brief Convert raw data (from the driver) to double-typed samples
 *
 * @param[in] chan Channel descriptor
 * @param[in] rng Range descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 *
 * @return the count of conversion performed, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, rng and the pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_rawtod()
 *
 */
int a4l_rawtod(a4l_chinfo_t * chan,
	       a4l_rnginfo_t * rng, double *dst, void *src, int cnt)
{
	int size, i = 0, j = 0;
	lsampl_t tmp;

	/* Temporary values used for conversion
	   (phys = a * src + b) */
	double a, b;
	/* Temporary data accessor */
	lsampl_t(*datax_get) (void *);

	/* Basic checking */
	if (rng == NULL || chan == NULL)
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

	/* Computes the translation factor and the constant only once */
	a = ((double)(rng->max - rng->min)) /
		(((1ULL << chan->nb_bits) - 1) * A4L_RNG_FACTOR);
	b = ((double)rng->min) / A4L_RNG_FACTOR;

	while (j < cnt) {

		/* Properly retrieve the data */
		tmp = datax_get(src + i);

		/* Perform the conversion */
		dst[j] = a * tmp + b;

		/* Update the counters */
		i += size;
		j++;
	}

	return j;
}

/**
 * @brief Pack unsigned long values into raw data (for the driver)
 *
 * This function takes as input a table of unsigned long values and
 * gather them according to the channel width. It is a convenience
 * routine which performs no conversion, just formatting.
 *
 * @param[in] chan Channel descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of transfer to copy
 *
 * @return the count of copy performed, otherwise a negative error
 * code:
 *
 * - -EINVAL is returned if some argument is missing or wrong; chan,
 *    dst and src pointers should be checked; check also the kernel
 *    log ("dmesg"); WARNING: a4l_fill_desc() should be called before
 *    using a4l_ultoraw()
 *
 */
int a4l_ultoraw(a4l_chinfo_t * chan, void *dst, unsigned long *src, int cnt)
{
	int size, i = 0, j = 0;

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

		/* Perform the copy */
		datax_set(dst + i, (lsampl_t)src[j]);

		/* Update the counters */
		i += size;
		j++;
	}

	return j;
}

/**
 * @brief Convert float-typed samples to raw data (for the driver)
 *
 * @param[in] chan Channel descriptor
 * @param[in] rng Range descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 *
 * @return the count of conversion performed, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, rng and the pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_ftoraw()
 *
 */
int a4l_ftoraw(a4l_chinfo_t * chan,
	       a4l_rnginfo_t * rng, void *dst, float *src, int cnt)
{
	int size, i = 0, j = 0;

	/* Temporary values used for conversion
	   (dst = a * phys - b) */
	float a, b;
	/* Temporary data accessor */
	void (*datax_set) (void *, lsampl_t);

	/* Basic checking */
	if (rng == NULL || chan == NULL)
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

	/* Computes the translation factor and the constant only once */
	a = (((float)A4L_RNG_FACTOR) / (rng->max - rng->min)) *
		((1ULL << chan->nb_bits) - 1);
	b = ((float)(rng->min) / (rng->max - rng->min)) *
		((1ULL << chan->nb_bits) - 1);

	while (j < cnt) {

		/* Performs the conversion */
		datax_set(dst + i, (lsampl_t) (a * src[j] - b));

		/* Updates the counters */
		i += size;
		j++;
	}

	return j;
}

/**
 * @brief Convert double-typed samples to raw data (for the driver)
 *
 * @param[in] chan Channel descriptor
 * @param[in] rng Range descriptor
 * @param[out] dst Ouput buffer
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 *
 * @return the count of conversion performed, otherwise a negative
 * error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong;
 *    chan, rng and the pointers should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_dtoraw()
 *
 */
int a4l_dtoraw(a4l_chinfo_t * chan,
	       a4l_rnginfo_t * rng, void *dst, double *src, int cnt)
{
	int size, i = 0, j = 0;

	/* Temporary values used for conversion
	   (dst = a * phys - b) */
	double a, b;
	/* Temporary data accessor */
	void (*datax_set) (void *, lsampl_t);

	/* Basic checking */
	if (rng == NULL || chan == NULL)
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

	/* Computes the translation factor and the constant only once */
	a = (((double)A4L_RNG_FACTOR) / (rng->max - rng->min)) *
		((1ULL << chan->nb_bits) - 1);
	b = ((double)(rng->min) / (rng->max - rng->min)) *
		((1ULL << chan->nb_bits) - 1);

	while (j < cnt) {

		/* Performs the conversion */
		datax_set(dst + i, (lsampl_t) (a * src[j] - b));

		/* Updates the counters */
		i += size;
		j++;
	}

	return j;
}
/** @} Range / conversion  API */
