/* Extended Module Player
 * Copyright (C) 1996-2013 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU Lesser General Public License. See COPYING.LIB
 * for more information.
 */

/*
 * Public domain DMF sample decompressor by Olivier Lapicque
 */

#include <assert.h>

#include "loader.h"
#include "iff.h"
#include "period.h"

#define MAGIC_DDMF	MAGIC4('D','D','M','F')


static int dmf_test(xmp_file, char *, const int);
static int dmf_load (struct module_data *, xmp_file, const int);

const struct format_loader dmf_loader = {
	"X-Tracker (DMF)",
	dmf_test,
	dmf_load
};

static int dmf_test(xmp_file f, char *t, const int start)
{
	if (read32b(f) != MAGIC_DDMF)
		return -1;

	xmp_fseek(f, 9, SEEK_CUR);
	read_title(f, t, 30);

	return 0;
}


struct local_data {
    int ver;
    uint8 packtype[256];
};


struct hnode {
	short int left, right;
	uint8 value;
};

struct htree {
	uint8 *ibuf, *ibufmax;
	uint32 bitbuf;
	int bitnum;
	int lastnode, nodecount;
	struct hnode nodes[256];
};


static uint8 read_bits(struct htree *tree, int nbits)
{
	uint8 x = 0, bitv = 1;
	while (nbits--) {
		if (tree->bitnum) {
			tree->bitnum--;
		} else {
			tree->bitbuf = (tree->ibuf < tree->ibufmax) ?
							*(tree->ibuf++) : 0;
			tree->bitnum = 7;
		}
		if (tree->bitbuf & 1) x |= bitv;
		bitv <<= 1;
		tree->bitbuf >>= 1;
	}
	return x;
}

/* tree: [8-bit value][12-bit index][12-bit index] = 32-bit */
static void new_node(struct htree *tree)
{
	uint8 isleft, isright;
	int actnode;

	actnode = tree->nodecount;

	if (actnode > 255)
		return;

	tree->nodes[actnode].value = read_bits(tree, 7);
	isleft = read_bits(tree, 1);
	isright = read_bits(tree, 1);
	actnode = tree->lastnode;

	if (actnode > 255)
		return;

	tree->nodecount++;
	tree->lastnode = tree->nodecount;

	if (isleft) {
		tree->nodes[actnode].left = tree->lastnode;
		new_node(tree);
	} else {
		tree->nodes[actnode].left = -1;
	}

	tree->lastnode = tree->nodecount;

	if (isright) {
		tree->nodes[actnode].right = tree->lastnode;
		new_node(tree);
	} else {
		tree->nodes[actnode].right = -1;
	}
}

static int unpack(uint8 *psample, uint8 *ibuf, uint8 *ibufmax, uint32 maxlen)
{
	struct htree tree;
	int i, actnode;
	uint8 value, sign, delta = 0;
	
	memset(&tree, 0, sizeof(tree));
	tree.ibuf = ibuf;
	tree.ibufmax = ibufmax;
	new_node(&tree);
	value = 0;

	for (i = 0; i < maxlen; i++) {
		actnode = 0;
		sign = read_bits(&tree, 1);

		do {
			if (read_bits(&tree, 1))
				actnode = tree.nodes[actnode].right;
			else
				actnode = tree.nodes[actnode].left;
			if (actnode > 255) break;
			delta = tree.nodes[actnode].value;
			if ((tree.ibuf >= tree.ibufmax) && (!tree.bitnum)) break;
		} while ((tree.nodes[actnode].left >= 0) &&
					(tree.nodes[actnode].right >= 0));

		if (sign)
			delta ^= 0xff;
		value += delta;
		psample[i] = i ? value : 0;
	}

	return tree.ibuf - ibuf;
}


/*
 * IFF chunk handlers
 */

static void get_sequ(struct module_data *m, int size, xmp_file f, void *parm)
{
	struct xmp_module *mod = &m->mod;
	int i;

	read16l(f);	/* sequencer loop start */
	read16l(f);	/* sequencer loop end */

	mod->len = (size - 4) / 2;
	if (mod->len > 255)
		mod->len = 255;

	for (i = 0; i < mod->len; i++)
		mod->xxo[i] = read16l(f);
}

static void get_patt(struct module_data *m, int size, xmp_file f, void *parm)
{
	struct xmp_module *mod = &m->mod;
	int i, j, r, chn;
	int patsize;
	int info, counter, data;
	int track_counter[32];
	struct xmp_event *event;

	mod->pat = read16l(f);
	mod->chn = read8(f);
	mod->trk = mod->chn * mod->pat;

	PATTERN_INIT();

	D_(D_INFO "Stored patterns: %d", mod->pat);

	for (i = 0; i < mod->pat; i++) {
		PATTERN_ALLOC(i);
		chn = read8(f);
		read8(f);		/* beat */
		mod->xxp[i]->rows = read16l(f);
		TRACK_ALLOC(i);

		patsize = read32l(f);

		for (j = 0; j < chn; j++)
			track_counter[j] = 0;

		for (counter = r = 0; r < mod->xxp[i]->rows; r++) {
			if (counter == 0) {
				/* global track */
				info = read8(f);
				counter = info & 0x80 ? read8(f) : 0;
				data = info & 0x3f ? read8(f) : 0;
			} else {
				counter--;
			}

			for (j = 0; j < chn; j++) {
				int b, fxt, fxp;

				event = &EVENT(i, j, r);

				if (track_counter[j] == 0) {
					b = read8(f);
		
					if (b & 0x80)
						track_counter[j] = read8(f);
					if (b & 0x40)
						event->ins = read8(f);
					if (b & 0x20)
						event->note = 24 + read8(f);
					if (b & 0x10)
						event->vol = read8(f);
					if (b & 0x08) {	/* instrument effect */
						fxt = read8(f);
						fxp = read8(f);
					}
					if (b & 0x04) {	/* note effect */
						fxt = read8(f);
						fxp = read8(f);
					}
					if (b & 0x02) {	/* volume effect */
						fxt = read8(f);
						fxp = read8(f);
						switch (fxt) {
						case 0x02:
							event->fxt = FX_VOLSLIDE_DN;
							event->fxp = fxp;
							break;
						}
					}
				} else {
					track_counter[j]--;
				}
			}
		}
	}
}

static void get_smpi(struct module_data *m, int size, xmp_file f, void *parm)
{
	struct xmp_module *mod = &m->mod;
	struct local_data *data = (struct local_data *)parm;
	int i, namelen, c3spd, flag;
	uint8 name[30];

	mod->ins = mod->smp = read8(f);

	INSTRUMENT_INIT();

	D_(D_INFO "Instruments: %d", mod->ins);

	for (i = 0; i < mod->ins; i++) {
		int x;

		mod->xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);
		
		namelen = read8(f);
		x = namelen - xmp_fread(name, 1, namelen > 30 ? 30 : namelen, f);
		copy_adjust(mod->xxi[i].name, name, namelen);
		name[namelen] = 0;
		while (x--)
			read8(f);

		mod->xxs[i].len = read32l(f);
		mod->xxs[i].lps = read32l(f);
		mod->xxs[i].lpe = read32l(f);
		mod->xxi[i].nsm = !!mod->xxs[i].len;
		c3spd = read16l(f);
		c2spd_to_note(c3spd, &mod->xxi[i].sub[0].xpo, &mod->xxi[i].sub[0].fin);
		mod->xxi[i].sub[0].vol = read8(f);
		mod->xxi[i].sub[0].pan = 0x80;
		mod->xxi[i].sub[0].sid = i;
		flag = read8(f);
		mod->xxs[i].flg = flag & 0x01 ? XMP_SAMPLE_LOOP : 0;
		if (data->ver >= 8)
			xmp_fseek(f, 8, SEEK_CUR);	/* library name */
		read16l(f);	/* reserved -- specs say 1 byte only*/
		read32l(f);	/* sampledata crc32 */

		data->packtype[i] = (flag & 0x0c) >> 2;
		D_(D_INFO "[%2X] %-30.30s %05x %05x %05x %c P%c %5d V%02x",
				i, name, mod->xxs[i].len, mod->xxs[i].lps & 0xfffff,
				mod->xxs[i].lpe & 0xfffff,
				mod->xxs[i].flg & XMP_SAMPLE_LOOP ? 'L' : ' ',
				'0' + data->packtype[i],
				c3spd, mod->xxi[i].sub[0].vol);
	}
}

static void get_smpd(struct module_data *m, int size, xmp_file f, void *parm)
{
	struct xmp_module *mod = &m->mod;
	struct local_data *data = (struct local_data *)parm;
	int i;
	int smpsize;
	uint8 *sbuf, *ibuf;

	D_(D_INFO "Stored samples: %d", mod->ins);

	for (smpsize = i = 0; i < mod->smp; i++) {
		if (mod->xxs[i].len > smpsize)
			smpsize = mod->xxs[i].len;
	}

	/* why didn't we mmap this? */
	sbuf = malloc(smpsize);
	assert(sbuf != NULL);
	ibuf = malloc(smpsize);
	assert(ibuf != NULL);

	for (i = 0; i < mod->smp; i++) {
		smpsize = read32l(f);
		if (smpsize == 0)
			continue;

		switch (data->packtype[i]) {
		case 0:
			load_sample(m, f, 0, &mod->xxs[mod->xxi[i].sub[0].sid], NULL);
			break;
		case 1:
			xmp_fread(ibuf, smpsize, 1, f);
			unpack(sbuf, ibuf, ibuf + smpsize, mod->xxs[i].len);
			load_sample(m, NULL, SAMPLE_FLAG_NOLOAD, &mod->xxs[i], (char *)sbuf);
			break;
		default:
			xmp_fseek(f, smpsize, SEEK_CUR);
		}
	}

	free(ibuf);
	free(sbuf);
}

static int dmf_load(struct module_data *m, xmp_file f, const int start)
{
	struct xmp_module *mod = &m->mod;
	iff_handle handle;
	uint8 date[3];
	char tracker_name[10];
	struct local_data data;

	LOAD_INIT();

	read32b(f);		/* DDMF */

	data.ver = read8(f);
	xmp_fread(tracker_name, 8, 1, f);
	tracker_name[8] = 0;
	snprintf(mod->type, XMP_NAME_SIZE, "%s DMF v%d",
				tracker_name, data.ver);
	tracker_name[8] = 0;
	xmp_fread(mod->name, 30, 1, f);
	xmp_fseek(f, 20, SEEK_CUR);
	xmp_fread(date, 3, 1, f);
	
	MODULE_INFO();
	D_(D_INFO "Creation date: %02d/%02d/%04d", date[0],
						date[1], 1900 + date[2]);
	
	handle = iff_new();
	if (handle == NULL)
		return -1;

	/* IFF chunk IDs */
	iff_register(handle, "SEQU", get_sequ);
	iff_register(handle, "PATT", get_patt);
	iff_register(handle, "SMPI", get_smpi);
	iff_register(handle, "SMPD", get_smpd);
	iff_set_quirk(handle, IFF_LITTLE_ENDIAN);

	/* Load IFF chunks */
	while (!xmp_feof(f)) {
		iff_chunk(handle, m, f, &data);
	}

	m->volbase = 0xff;

	iff_release(handle);

	return 0;
}
