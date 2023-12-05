/**
 * @file
 * Analogy for Linux, descriptor related features
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

#include <string.h>
#include <rtdm/analogy.h>
#include "internal.h"
#include "root_leaf.h"

#ifndef DOXYGEN_CPP

static void a4l_root_setup(a4l_root_t * rt,
			   unsigned long gsize, unsigned long rsize)
{
	/* Common init */
	rt->offset = ((void *)rt + sizeof(a4l_root_t));
	rt->gsize = gsize;
	rt->id = 0xffffffff;
	rt->nb_leaf = 0;
	rt->lfnxt = NULL;
	rt->lfchd = NULL;

	/* Specific init */
	rt->data = rt->offset;
	rt->offset += rsize;
}

static int a4l_leaf_add(a4l_root_t * rt,
			a4l_leaf_t * lf,
			a4l_leaf_t ** lfchild, unsigned long lfsize)
{
	/* Basic checking */
	if (rt->offset +
	    sizeof(a4l_leaf_t) + lfsize > ((void *)rt) + rt->gsize)
		return -ENOMEM;

	if (lf->nb_leaf != 0) {
		int i;
		a4l_leaf_t *lflst = lf->lfchd;

		for (i = 0; i < (lf->nb_leaf - 1); i++) {
			if (lflst == NULL)
				return -EFAULT;
			else
				lflst = lflst->lfnxt;
		}
		lflst->lfnxt = (a4l_leaf_t *) rt->offset;
	} else
		lf->lfchd = (a4l_leaf_t *) rt->offset;

	/* Inits parent leaf */
	lf->nb_leaf++;
	*lfchild = (a4l_leaf_t *) rt->offset;
	rt->offset += sizeof(a4l_leaf_t);

	/* Performs child leaf init */
	(*lfchild)->id = lf->nb_leaf - 1;
	(*lfchild)->nb_leaf = 0;
	(*lfchild)->lfnxt = NULL;
	(*lfchild)->lfchd = NULL;
	(*lfchild)->data = (void *)rt->offset;

	/* Performs root modifications */
	rt->offset += lfsize;

	return 0;
}

static inline a4l_leaf_t *a4l_leaf_get(a4l_leaf_t * lf,
				       unsigned int id)
{
	int i;
	a4l_leaf_t *lflst = lf->lfchd;

	for (i = 0; i < id && lflst != NULL; i++)
		lflst = lflst->lfnxt;

	return lflst;
}

static int __a4l_get_sbsize(int fd, a4l_desc_t * dsc)
{
	unsigned int i, j, nb_chan, nb_rng;
	int ret, res =
		dsc->nb_subd * (sizeof(a4l_sbinfo_t) + sizeof(a4l_leaf_t));

	for (i = 0; i < dsc->nb_subd; i++) {

		if ((ret = a4l_sys_nbchaninfo(fd, i, &nb_chan)) < 0)
			return ret;

		res += nb_chan * (sizeof(a4l_chinfo_t) + sizeof(a4l_leaf_t));
		for (j = 0; j < nb_chan; j++) {
			if ((ret = a4l_sys_nbrnginfo(fd, i, j, &nb_rng)) < 0)
				return ret;
			res += nb_rng * (sizeof(a4l_rnginfo_t) +
					 sizeof(a4l_leaf_t));
		}
	}

	return res;
}

static int __a4l_fill_desc(int fd, a4l_desc_t * dsc)
{
	int ret;
	unsigned int i, j;
	a4l_sbinfo_t *sbinfo;
	a4l_root_t *rt = (a4l_root_t *) dsc->sbdata;

	a4l_root_setup(rt, dsc->sbsize,
		       dsc->nb_subd * sizeof(a4l_sbinfo_t));
	sbinfo = (a4l_sbinfo_t *) rt->data;

	if ((ret = a4l_sys_subdinfo(fd, sbinfo)) < 0)
		return ret;

	for (i = 0; i < dsc->nb_subd; i++) {
		a4l_leaf_t *lfs;
		a4l_chinfo_t *chinfo;

		/* For each subd, add a leaf for the channels even if
		   the subd does not own any channel */
		ret = a4l_leaf_add(rt, (a4l_leaf_t *) rt, &lfs,
			     sbinfo[i].nb_chan * sizeof(a4l_chinfo_t));
		if (ret < 0)
			return ret;

		/* If there is no channel, no need to go further */
		if(sbinfo[i].nb_chan == 0)
			continue;

		chinfo = (a4l_chinfo_t *) lfs->data;

		if ((ret = a4l_sys_chaninfo(fd, i, chinfo)) < 0)
			return ret;

		for (j = 0; j < sbinfo[i].nb_chan; j++) {
			a4l_leaf_t *lfc;
			a4l_rnginfo_t *rnginfo;

			/* For each channel, add a leaf for the ranges
			   even if no range descriptor is available */
			ret = a4l_leaf_add(rt, lfs, &lfc,
				     chinfo[j].nb_rng *
				     sizeof(a4l_rnginfo_t));
			if (ret < 0)
				return ret;


			/* If there is no range, no need to go further */
			if(chinfo[j].nb_rng ==0)
				continue;

			rnginfo = (a4l_rnginfo_t *) lfc->data;
			if ((ret = a4l_sys_rnginfo(fd, i, j, rnginfo)) < 0)
				return ret;
		}
	}

	return 0;
}

#endif /* !DOXYGEN_CPP */

/*!
 * @ingroup analogy_lib_syscall
 * @defgroup analogy_lib_descriptor Descriptor Syscall API
 * @{
 */

/**
 * @brief Get a descriptor on an attached device
 *
 * Once the device has been attached, the function a4l_get_desc()
 * retrieves various information on the device (subdevices, channels,
 * ranges, etc.).
 * The function a4l_get_desc() can be called twice:
 * - The first time, almost all the fields, except sbdata, are set
 *   (board_name, nb_subd, idx_read_subd, idx_write_subd, magic,
 *   sbsize); the last field , sbdata, is supposed to be a pointer on
 *   a buffer, which size is defined by the field sbsize.
 * - The second time, the buffer pointed by sbdata is filled with data
 *   about the subdevices, the channels and the ranges.
 *
 * Between the two calls, an allocation must be performed in order to
 * recover a buffer large enough to contain all the data. These data
 * are set up according a root-leaf organization (device -> subdevice
 * -> channel -> range). They cannot be accessed directly; specific
 * functions are available so as to retrieve them:
 * - a4l_get_subdinfo() to get some subdevice's characteristics.
 * - a4l_get_chaninfo() to get some channel's characteristics.
 * - a4l_get_rnginfo() to get some range's characteristics.
 *
 * @param[in] fd Driver file descriptor
 * @param[out] dsc Device descriptor
 * @param[in] pass Description level to retrieve:
 * - A4L_BSC_DESC to get the basic descriptor (notably the size of
 *   the data buffer to allocate).
 * - A4L_CPLX_DESC to get the complex descriptor, the data buffer
 *   is filled with characteristics about the subdevices, the channels
 *   and the ranges.
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; the
 *    pass argument should be checked; check also the kernel log
 *    ("dmesg")
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENODEV is returned if the descriptor is incoherent (the device
 *    may be unattached)
 *
 */

int a4l_sys_desc(int fd, a4l_desc_t * dsc, int pass)
{
	int ret = 0;

	if (dsc == NULL ||
	    (pass != A4L_BSC_DESC && dsc->magic != MAGIC_BSC_DESC))
		return -EINVAL;

	if (pass == A4L_BSC_DESC) {

		ret = a4l_sys_devinfo(fd, (a4l_dvinfo_t *) dsc);
		if (ret < 0)
			goto out_a4l_sys_desc;

		dsc->sbsize = __a4l_get_sbsize(fd, dsc);
		dsc->sbdata = NULL;
		dsc->magic = MAGIC_BSC_DESC;
	} else {

		if (!dsc->sbsize) {
			ret = -ENODEV;
			goto out_a4l_sys_desc;
		}

		ret = __a4l_fill_desc(fd, dsc);
		if (ret < 0)
			goto out_a4l_sys_desc;

		dsc->magic = MAGIC_CPLX_DESC;
	}

out_a4l_sys_desc:
	return ret;
}

/*! @} Descriptor Syscall API */

/*!
 * @ingroup analogy_lib_level1
 * @defgroup analogy_lib_descriptor1 Descriptor API
 *
 * This is the API interface used to fill and use Analogy device
 * descriptor structure
 * @{
 */

/**
 * @brief Open an Analogy device and basically fill the descriptor
 *
 * @param[out] dsc Device descriptor
 * @param[in] fname Device name
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; the
 *    fname and the dsc pointer should be checked; check also the
 *    kernel log ("dmesg")
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 *
 */
int a4l_open(a4l_desc_t *dsc, const char *fname)
{
	int ret;

	/* Basic checking */
	if (dsc == NULL)
		return -EINVAL;

	/* Initializes the descriptor */
	memset(dsc, 0, sizeof(a4l_desc_t));

	/* Opens the driver */
	dsc->fd = a4l_sys_open(fname);
	if (dsc->fd < 0)
		return dsc->fd;

	/* Basically fills the descriptor */
	ret = a4l_sys_desc(dsc->fd, dsc, A4L_BSC_DESC);
	if (ret < 0) {
		a4l_sys_close(dsc->fd);
	}

	return ret;
}

/**
 * @brief Close the Analogy device related with the descriptor
 *
 * The file descriptor is associated with a context. The context is
 * one of the enabler of asynchronous transfers. So, by closing the
 * file descriptor, the programer must keep in mind that the currently
 * occuring asynchronous transfer will cancelled.
 *
 * @param[in] dsc Device descriptor
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; the
 *    the dsc pointer should be checked; check also the kernel log
 *    ("dmesg")
 *
 */
int a4l_close(a4l_desc_t * dsc)
{
	/* Basic checking */
	if (dsc == NULL)
		return -EINVAL;

	return a4l_sys_close(dsc->fd);
}

/**
 * @brief Fill the descriptor with subdevices, channels and ranges
 * data
 *
 * @param[in] dsc Device descriptor partly filled by a4l_open().
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; the
 *    the dsc pointer should be checked; check also the kernel log
 *    ("dmesg")
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENODEV is returned if the descriptor is incoherent (the device
 *    may be unattached)
 *
 */
int a4l_fill_desc(a4l_desc_t * dsc)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	/* Checks the descriptor has been basically filled */
	if (dsc->magic != MAGIC_BSC_DESC)
		return -EINVAL;

	return a4l_sys_desc(dsc->fd, dsc, A4L_CPLX_DESC);
}

/**
 * @brief Get an information structure on a specified subdevice
 *
 * @param[in] dsc Device descriptor filled by a4l_open() and
 * a4l_fill_desc()
 * @param[in] subd Subdevice index
 * @param[out] info Subdevice information structure
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; subd
 *    and the dsc pointer should be checked; check also the kernel log
 *    ("dmesg"); WARNING: a4l_fill_desc() should be called before
 *    using a4l_get_subdinfo().
 *
 */
int a4l_get_subdinfo(a4l_desc_t * dsc,
		     unsigned int subd, a4l_sbinfo_t ** info)
{
	a4l_leaf_t *tmp;

	if (dsc == NULL || info == NULL)
		return -EINVAL;

	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	if (subd >= dsc->nb_subd)
		return -EINVAL;

	tmp = (a4l_leaf_t *) dsc->sbdata;
	*info = &(((a4l_sbinfo_t *) tmp->data)[subd]);

	return 0;
}

/**
 * @brief Get an information structure on a specified channel
 *
 * @param[in] dsc Device descriptor filled by a4l_open() and
 * a4l_fill_desc()
 * @param[in] subd Subdevice index
 * @param[in] chan Channel index
 * @param[out] info Channel information structure
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; subd,
 *    chan and the dsc pointer should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_get_chinfo()
 *
 */
int a4l_get_chinfo(a4l_desc_t * dsc,
		   unsigned int subd,
		   unsigned int chan, a4l_chinfo_t ** info)
{
	a4l_leaf_t *tmp;

	if (dsc == NULL || info == NULL)
		return -EINVAL;

	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	if (subd >= dsc->nb_subd)
		return -EINVAL;

	tmp = (a4l_leaf_t *) dsc->sbdata;

	if (chan >= ((a4l_sbinfo_t *) tmp->data)[subd].nb_chan)
		return -EINVAL;

	tmp = a4l_leaf_get(tmp, subd);
	*info = &(((a4l_chinfo_t *) tmp->data)[chan]);

	return 0;
}

/**
 * @brief Get an information structure on a specified range
 *
 * @param[in] dsc Device descriptor filled by a4l_open() and
 * a4l_fill_desc()
 * @param[in] subd Subdevice index
 * @param[in] chan Channel index
 * @param[in] rng Range index
 * @param[out] info Range information structure
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; subd,
 *    chan, rng and the dsc pointer should be checked; check also the
 *    kernel log ("dmesg"); WARNING: a4l_fill_desc() should be called
 *    before using a4l_get_rnginfo()
 *
 */
int a4l_get_rnginfo(a4l_desc_t * dsc,
		    unsigned int subd,
		    unsigned int chan,
		    unsigned int rng, a4l_rnginfo_t ** info)
{
	a4l_leaf_t *tmp;

	if (dsc == NULL || info == NULL)
		return -EINVAL;

	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	if (subd >= dsc->nb_subd)
		return -EINVAL;

	tmp = (a4l_leaf_t *) dsc->sbdata;

	if (chan >= ((a4l_sbinfo_t *) tmp->data)[subd].nb_chan)
		return -EINVAL;

	tmp = a4l_leaf_get(tmp, subd);

	if (rng >= ((a4l_chinfo_t *) tmp->data)[chan].nb_rng)
		return -EINVAL;

	tmp = a4l_leaf_get(tmp, chan);
	*info = &(((a4l_rnginfo_t *) tmp->data)[rng]);

	return 0;
}

/*! @} Descriptor API */
