/*
 * (C) 2012-2014 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "internal.h"

#include <time.h>
#include <endian.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>

#include <libnftnl/gen.h>

struct nftnl_gen {
	uint32_t id;

	uint32_t flags;
};

struct nftnl_gen *nftnl_gen_alloc(void)
{
	return calloc(1, sizeof(struct nftnl_gen));
}
EXPORT_SYMBOL(nftnl_gen_alloc);

void nftnl_gen_free(const struct nftnl_gen *gen)
{
	xfree(gen);
}
EXPORT_SYMBOL(nftnl_gen_free);

bool nftnl_gen_is_set(const struct nftnl_gen *gen, uint16_t attr)
{
	return gen->flags & (1 << attr);
}
EXPORT_SYMBOL(nftnl_gen_is_set);

void nftnl_gen_unset(struct nftnl_gen *gen, uint16_t attr)
{
	if (!(gen->flags & (1 << attr)))
		return;

	switch (attr) {
	case NFTNL_GEN_ID:
		break;
	}
	gen->flags &= ~(1 << attr);
}
EXPORT_SYMBOL(nftnl_gen_unset);

static uint32_t nftnl_gen_validate[NFTNL_GEN_MAX + 1] = {
	[NFTNL_GEN_ID]	= sizeof(uint32_t),
};

int nftnl_gen_set_data(struct nftnl_gen *gen, uint16_t attr,
		       const void *data, uint32_t data_len)
{
	nftnl_assert_attr_exists(attr, NFTNL_GEN_MAX);
	nftnl_assert_validate(data, nftnl_gen_validate, attr, data_len);

	switch (attr) {
	case NFTNL_GEN_ID:
		gen->id = *((uint32_t *)data);
		break;
	}
	gen->flags |= (1 << attr);
	return 0;
}
EXPORT_SYMBOL(nftnl_gen_set_data);

int nftnl_gen_set(struct nftnl_gen *gen, uint16_t attr, const void *data)
{
	return nftnl_gen_set_data(gen, attr, data, nftnl_gen_validate[attr]);
}
EXPORT_SYMBOL(nftnl_gen_set);

void nftnl_gen_set_u32(struct nftnl_gen *gen, uint16_t attr, uint32_t val)
{
	nftnl_gen_set_data(gen, attr, &val, sizeof(uint32_t));
}
EXPORT_SYMBOL(nftnl_gen_set_u32);

const void *nftnl_gen_get_data(const struct nftnl_gen *gen, uint16_t attr,
			       uint32_t *data_len)
{
	if (!(gen->flags & (1 << attr)))
		return NULL;

	switch(attr) {
	case NFTNL_GEN_ID:
		*data_len = sizeof(gen->id);
		return &gen->id;
	}
	return NULL;
}
EXPORT_SYMBOL(nftnl_gen_get_data);

const void *nftnl_gen_get(const struct nftnl_gen *gen, uint16_t attr)
{
	uint32_t data_len;
	return nftnl_gen_get_data(gen, attr, &data_len);
}
EXPORT_SYMBOL(nftnl_gen_get);

uint32_t nftnl_gen_get_u32(const struct nftnl_gen *gen, uint16_t attr)
{
	const void *ret = nftnl_gen_get(gen, attr);
	return ret == NULL ? 0 : *((uint32_t *)ret);
}
EXPORT_SYMBOL(nftnl_gen_get_u32);

static int nftnl_gen_parse_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_GEN_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_GEN_ID:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0)
			abi_breakage();
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

int nftnl_gen_nlmsg_parse(const struct nlmsghdr *nlh, struct nftnl_gen *gen)
{
	struct nlattr *tb[NFTA_GEN_MAX + 1] = {};

	if (mnl_attr_parse(nlh, sizeof(struct nfgenmsg),
			   nftnl_gen_parse_attr_cb, tb) < 0)
		return -1;

	if (tb[NFTA_GEN_ID]) {
		gen->id = ntohl(mnl_attr_get_u32(tb[NFTA_GEN_ID]));
		gen->flags |= (1 << NFTNL_GEN_ID);
	}
	return 0;
}
EXPORT_SYMBOL(nftnl_gen_nlmsg_parse);

static int nftnl_gen_snprintf_default(char *buf, size_t size,
				      const struct nftnl_gen *gen)
{
	return snprintf(buf, size, "ruleset generation ID %u", gen->id);
}

static int nftnl_gen_cmd_snprintf(char *buf, size_t size,
				  const struct nftnl_gen *gen, uint32_t cmd,
				  uint32_t type, uint32_t flags)
{
	int ret, remain = size, offset = 0;

	ret = nftnl_cmd_header_snprintf(buf + offset, remain, cmd, type, flags);
	SNPRINTF_BUFFER_SIZE(ret, remain, offset);

	switch(type) {
	case NFTNL_OUTPUT_DEFAULT:
		ret = nftnl_gen_snprintf_default(buf + offset, remain, gen);
		break;
	default:
		return -1;
	}
	SNPRINTF_BUFFER_SIZE(ret, remain, offset);

	ret = nftnl_cmd_footer_snprintf(buf + offset, remain, cmd, type, flags);
	SNPRINTF_BUFFER_SIZE(ret, remain, offset);

	return offset;
}

int nftnl_gen_snprintf(char *buf, size_t size, const struct nftnl_gen *gen,
		       uint32_t type, uint32_t flags)
{
	if (size)
		buf[0] = '\0';

	return nftnl_gen_cmd_snprintf(buf, size, gen, nftnl_flag2cmd(flags), type,
				    flags);
}
EXPORT_SYMBOL(nftnl_gen_snprintf);

static int nftnl_gen_do_snprintf(char *buf, size_t size, const void *gen,
				 uint32_t cmd, uint32_t type, uint32_t flags)
{
	return nftnl_gen_snprintf(buf, size, gen, type, flags);
}

int nftnl_gen_fprintf(FILE *fp, const struct nftnl_gen *gen, uint32_t type,
		      uint32_t flags)
{
	return nftnl_fprintf(fp, gen, NFTNL_CMD_UNSPEC, type, flags,
			   nftnl_gen_do_snprintf);
}
EXPORT_SYMBOL(nftnl_gen_fprintf);
