/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2012 by Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Morten Mikkelsen,
 *                 Sergey Sharybin
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/render/intern/source/multires_bake.c
 *  \ingroup render
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_object_types.h"
#include "DNA_mesh_types.h"

#include "BLI_math.h"
#include "BLI_listbase.h"

#include "BKE_ccg.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_multires.h"
#include "BKE_modifier.h"
#include "BKE_subsurf.h"

#include "RE_multires_bake.h"
#include "RE_pipeline.h"
#include "RE_shader_ext.h"

#include "IMB_imbuf_types.h"
#include "IMB_imbuf.h"

typedef void (*MPassKnownData)(DerivedMesh *lores_dm, DerivedMesh *hires_dm, const void *bake_data,
                               ImBuf *ibuf, const int face_index, const int lvl, const float st[2],
                               float tangmat[3][3], const int x, const int y);

typedef void * (*MInitBakeData)(MultiresBakeRender *bkr, Image *ima);
typedef void   (*MApplyBakeData)(void *bake_data);
typedef void   (*MFreeBakeData)(void *bake_data);

typedef struct {
	MVert *mvert;
	MFace *mface;
	MTFace *mtface;
	float *pvtangent;
	float *precomputed_normals;
	int w, h;
	int face_index;
	int i0, i1, i2;
	DerivedMesh *lores_dm, *hires_dm;
	int lvl;
	void *bake_data;
	ImBuf *ibuf;
	MPassKnownData pass_data;
} MResolvePixelData;

typedef void (*MFlushPixel)(const MResolvePixelData *data, const int x, const int y);

typedef struct {
	int w, h;
	char *texels;
	const MResolvePixelData *data;
	MFlushPixel flush_pixel;
} MBakeRast;

typedef struct {
	float *heights;
	float height_min, height_max;
	Image *ima;
	DerivedMesh *ssdm;
	const int *orig_index_mf_to_mpoly;
	const int *orig_index_mp_to_orig;
} MHeightBakeData;

typedef struct {
	const int *orig_index_mf_to_mpoly;
	const int *orig_index_mp_to_orig;
} MNormalBakeData;

static void multiresbake_get_normal(const MResolvePixelData *data, float norm[], const int face_num, const int vert_index)
{
	unsigned int indices[] = {data->mface[face_num].v1, data->mface[face_num].v2,
	                          data->mface[face_num].v3, data->mface[face_num].v4};
	const int smoothnormal = (data->mface[face_num].flag & ME_SMOOTH);

	if (!smoothnormal) { /* flat */
		if (data->precomputed_normals) {
			copy_v3_v3(norm, &data->precomputed_normals[3 * face_num]);
		}
		else {
			float nor[3];
			float *p0, *p1, *p2;
			const int iGetNrVerts = data->mface[face_num].v4 != 0 ? 4 : 3;

			p0 = data->mvert[indices[0]].co;
			p1 = data->mvert[indices[1]].co;
			p2 = data->mvert[indices[2]].co;

			if (iGetNrVerts == 4) {
				float *p3 = data->mvert[indices[3]].co;
				normal_quad_v3(nor, p0, p1, p2, p3);
			}
			else {
				normal_tri_v3(nor, p0, p1, p2);
			}

			copy_v3_v3(norm, nor);
		}
	}
	else {
		short *no = data->mvert[indices[vert_index]].no;

		normal_short_to_float_v3(norm, no);
		normalize_v3(norm);
	}
}

static void init_bake_rast(MBakeRast *bake_rast, const ImBuf *ibuf, const MResolvePixelData *data, MFlushPixel flush_pixel)
{
	memset(bake_rast, 0, sizeof(MBakeRast));

	bake_rast->texels = ibuf->userdata;
	bake_rast->w = ibuf->x;
	bake_rast->h = ibuf->y;
	bake_rast->data = data;
	bake_rast->flush_pixel = flush_pixel;
}

static void flush_pixel(const MResolvePixelData *data, const int x, const int y)
{
	float st[2] = {(x + 0.5f) / data->w, (y + 0.5f) / data->h};
	float *st0, *st1, *st2;
	float *tang0, *tang1, *tang2;
	float no0[3], no1[3], no2[3];
	float fUV[2], from_tang[3][3], to_tang[3][3];
	float u, v, w, sign;
	int r;

	const int i0 = data->i0;
	const int i1 = data->i1;
	const int i2 = data->i2;

	st0 = data->mtface[data->face_index].uv[i0];
	st1 = data->mtface[data->face_index].uv[i1];
	st2 = data->mtface[data->face_index].uv[i2];

	tang0 = data->pvtangent + data->face_index * 16 + i0 * 4;
	tang1 = data->pvtangent + data->face_index * 16 + i1 * 4;
	tang2 = data->pvtangent + data->face_index * 16 + i2 * 4;

	multiresbake_get_normal(data, no0, data->face_index, i0);   /* can optimize these 3 into one call */
	multiresbake_get_normal(data, no1, data->face_index, i1);
	multiresbake_get_normal(data, no2, data->face_index, i2);

	resolve_tri_uv(fUV, st, st0, st1, st2);

	u = fUV[0];
	v = fUV[1];
	w = 1 - u - v;

	/* the sign is the same at all face vertices for any non degenerate face.
	 * Just in case we clamp the interpolated value though. */
	sign = (tang0[3] * u + tang1[3] * v + tang2[3] * w) < 0 ? (-1.0f) : 1.0f;

	/* this sequence of math is designed specifically as is with great care
	 * to be compatible with our shader. Please don't change without good reason. */
	for (r = 0; r < 3; r++) {
		from_tang[0][r] = tang0[r] * u + tang1[r] * v + tang2[r] * w;
		from_tang[2][r] = no0[r] * u + no1[r] * v + no2[r] * w;
	}

	cross_v3_v3v3(from_tang[1], from_tang[2], from_tang[0]);  /* B = sign * cross(N, T)  */
	mul_v3_fl(from_tang[1], sign);
	invert_m3_m3(to_tang, from_tang);
	/* sequence end */

	data->pass_data(data->lores_dm, data->hires_dm, data->bake_data,
	                data->ibuf, data->face_index, data->lvl, st, to_tang, x, y);
}

static void set_rast_triangle(const MBakeRast *bake_rast, const int x, const int y)
{
	const int w = bake_rast->w;
	const int h = bake_rast->h;

	if (x >= 0 && x < w && y >= 0 && y < h) {
		if ((bake_rast->texels[y * w + x]) == 0) {
			flush_pixel(bake_rast->data, x, y);
			bake_rast->texels[y * w + x] = FILTER_MASK_USED;
		}
	}
}

static void rasterize_half(const MBakeRast *bake_rast,
                           const float s0_s, const float t0_s, const float s1_s, const float t1_s,
                           const float s0_l, const float t0_l, const float s1_l, const float t1_l,
                           const int y0_in, const int y1_in, const int is_mid_right)
{
	const int s_stable = fabsf(t1_s - t0_s) > FLT_EPSILON ? 1 : 0;
	const int l_stable = fabsf(t1_l - t0_l) > FLT_EPSILON ? 1 : 0;
	const int w = bake_rast->w;
	const int h = bake_rast->h;
	int y, y0, y1;

	if (y1_in <= 0 || y0_in >= h)
		return;

	y0 = y0_in < 0 ? 0 : y0_in;
	y1 = y1_in >= h ? h : y1_in;

	for (y = y0; y < y1; y++) {
		/*-b(x-x0) + a(y-y0) = 0 */
		int iXl, iXr, x;
		float x_l = s_stable != 0 ? (s0_s + (((s1_s - s0_s) * (y - t0_s)) / (t1_s - t0_s))) : s0_s;
		float x_r = l_stable != 0 ? (s0_l + (((s1_l - s0_l) * (y - t0_l)) / (t1_l - t0_l))) : s0_l;

		if (is_mid_right != 0)
			SWAP(float, x_l, x_r);

		iXl = (int)ceilf(x_l);
		iXr = (int)ceilf(x_r);

		if (iXr > 0 && iXl < w) {
			iXl = iXl < 0 ? 0 : iXl;
			iXr = iXr >= w ? w : iXr;

			for (x = iXl; x < iXr; x++)
				set_rast_triangle(bake_rast, x, y);
		}
	}
}

static void bake_rasterize(const MBakeRast *bake_rast, const float st0_in[2], const float st1_in[2], const float st2_in[2])
{
	const int w = bake_rast->w;
	const int h = bake_rast->h;
	float slo = st0_in[0] * w - 0.5f;
	float tlo = st0_in[1] * h - 0.5f;
	float smi = st1_in[0] * w - 0.5f;
	float tmi = st1_in[1] * h - 0.5f;
	float shi = st2_in[0] * w - 0.5f;
	float thi = st2_in[1] * h - 0.5f;
	int is_mid_right = 0, ylo, yhi, yhi_beg;

	/* skip degenerates */
	if ((slo == smi && tlo == tmi) || (slo == shi && tlo == thi) || (smi == shi && tmi == thi))
		return;

	/* sort by T */
	if (tlo > tmi && tlo > thi) {
		SWAP(float, shi, slo);
		SWAP(float, thi, tlo);
	}
	else if (tmi > thi) {
		SWAP(float, shi, smi);
		SWAP(float, thi, tmi);
	}

	if (tlo > tmi) {
		SWAP(float, slo, smi);
		SWAP(float, tlo, tmi);
	}

	/* check if mid point is to the left or to the right of the lo-hi edge */
	is_mid_right = (-(shi - slo) * (tmi - thi) + (thi - tlo) * (smi - shi)) > 0 ? 1 : 0;
	ylo = (int) ceilf(tlo);
	yhi_beg = (int) ceilf(tmi);
	yhi = (int) ceilf(thi);

	/*if (fTmi>ceilf(fTlo))*/
	rasterize_half(bake_rast, slo, tlo, smi, tmi, slo, tlo, shi, thi, ylo, yhi_beg, is_mid_right);
	rasterize_half(bake_rast, smi, tmi, shi, thi, slo, tlo, shi, thi, yhi_beg, yhi, is_mid_right);
}

static int multiresbake_test_break(MultiresBakeRender *bkr)
{
	if (!bkr->stop) {
		/* this means baker is executed outside from job system */
		return 0;
	}

	return G.is_break;
}

static void do_multires_bake(MultiresBakeRender *bkr, Image *ima, MPassKnownData passKnownData,
                             MInitBakeData initBakeData, MApplyBakeData applyBakeData, MFreeBakeData freeBakeData)
{
	DerivedMesh *dm = bkr->lores_dm;
	ImBuf *ibuf = BKE_image_acquire_ibuf(ima, NULL, NULL);
	const int lvl = bkr->lvl;
	const int tot_face = dm->getNumTessFaces(dm);
	MVert *mvert = dm->getVertArray(dm);
	MFace *mface = dm->getTessFaceArray(dm);
	MTFace *mtface = dm->getTessFaceDataArray(dm, CD_MTFACE);
	float *pvtangent = NULL;

	if (CustomData_get_layer_index(&dm->faceData, CD_TANGENT) == -1)
		DM_add_tangent_layer(dm);

	pvtangent = DM_get_tessface_data_layer(dm, CD_TANGENT);

	if (tot_face > 0) {  /* sanity check */
		int f = 0;
		MBakeRast bake_rast;
		MResolvePixelData data = {NULL};

		data.mface = mface;
		data.mvert = mvert;
		data.mtface = mtface;
		data.pvtangent = pvtangent;
		data.precomputed_normals = dm->getTessFaceDataArray(dm, CD_NORMAL);  /* don't strictly need this */
		data.w = ibuf->x;
		data.h = ibuf->y;
		data.lores_dm = dm;
		data.hires_dm = bkr->hires_dm;
		data.lvl = lvl;
		data.pass_data = passKnownData;

		if (initBakeData)
			data.bake_data = initBakeData(bkr, ima);

		init_bake_rast(&bake_rast, ibuf, &data, flush_pixel);

		for (f = 0; f < tot_face; f++) {
			MTFace *mtfate = &mtface[f];
			int verts[3][2], nr_tris, t;

			if (multiresbake_test_break(bkr))
				break;

			if (mtfate->tpage != ima)
				continue;

			data.face_index = f;
			data.ibuf = ibuf;

			/* might support other forms of diagonal splits later on such as
			 * split by shortest diagonal.*/
			verts[0][0] = 0;
			verts[1][0] = 1;
			verts[2][0] = 2;

			verts[0][1] = 0;
			verts[1][1] = 2;
			verts[2][1] = 3;

			nr_tris = mface[f].v4 != 0 ? 2 : 1;
			for (t = 0; t < nr_tris; t++) {
				data.i0 = verts[0][t];
				data.i1 = verts[1][t];
				data.i2 = verts[2][t];

				bake_rasterize(&bake_rast, mtfate->uv[data.i0], mtfate->uv[data.i1], mtfate->uv[data.i2]);
			}

			bkr->baked_faces++;

			if (bkr->do_update)
				*bkr->do_update = TRUE;

			if (bkr->progress)
				*bkr->progress = ((float)bkr->baked_objects + (float)bkr->baked_faces / tot_face) / bkr->tot_obj;
		}

		if (applyBakeData)
			applyBakeData(data.bake_data);

		if (freeBakeData)
			freeBakeData(data.bake_data);
	}

	BKE_image_release_ibuf(ima, ibuf, NULL);
}

/* mode = 0: interpolate normals,
 * mode = 1: interpolate coord */
static void interp_bilinear_grid(CCGKey *key, CCGElem *grid, float crn_x, float crn_y, int mode, float res[3])
{
	int x0, x1, y0, y1;
	float u, v;
	float data[4][3];

	x0 = (int) crn_x;
	x1 = x0 >= (key->grid_size - 1) ? (key->grid_size - 1) : (x0 + 1);

	y0 = (int) crn_y;
	y1 = y0 >= (key->grid_size - 1) ? (key->grid_size - 1) : (y0 + 1);

	u = crn_x - x0;
	v = crn_y - y0;

	if (mode == 0) {
		copy_v3_v3(data[0], CCG_grid_elem_no(key, grid, x0, y0));
		copy_v3_v3(data[1], CCG_grid_elem_no(key, grid, x1, y0));
		copy_v3_v3(data[2], CCG_grid_elem_no(key, grid, x1, y1));
		copy_v3_v3(data[3], CCG_grid_elem_no(key, grid, x0, y1));
	}
	else {
		copy_v3_v3(data[0], CCG_grid_elem_co(key, grid, x0, y0));
		copy_v3_v3(data[1], CCG_grid_elem_co(key, grid, x1, y0));
		copy_v3_v3(data[2], CCG_grid_elem_co(key, grid, x1, y1));
		copy_v3_v3(data[3], CCG_grid_elem_co(key, grid, x0, y1));
	}

	interp_bilinear_quad_v3(data, u, v, res);
}

static void get_ccgdm_data(DerivedMesh *lodm, DerivedMesh *hidm,
                           const int *index_mf_to_mpoly, const int *index_mp_to_orig,
                           const int lvl, const int face_index, const float u, const float v, float co[3], float n[3])
{
	MFace mface;
	CCGElem **grid_data;
	CCGKey key;
	float crn_x, crn_y;
	int grid_size, S, face_side;
	int *grid_offset, g_index;

	lodm->getTessFace(lodm, face_index, &mface);

	grid_size = hidm->getGridSize(hidm);
	grid_data = hidm->getGridData(hidm);
	grid_offset = hidm->getGridOffset(hidm);
	hidm->getGridKey(hidm, &key);

	face_side = (grid_size << 1) - 1;

	if (lvl == 0) {
		g_index = grid_offset[face_index];
		S = mdisp_rot_face_to_crn(mface.v4 ? 4 : 3, face_side, u * (face_side - 1), v * (face_side - 1), &crn_x, &crn_y);
	}
	else {
		int side = (1 << (lvl - 1)) + 1;
		int grid_index = DM_origindex_mface_mpoly(index_mf_to_mpoly, index_mp_to_orig, face_index);
		int loc_offs = face_index % (1 << (2 * lvl));
		int cell_index = loc_offs % ((side - 1) * (side - 1));
		int cell_side = (grid_size - 1) / (side - 1);
		int row = cell_index / (side - 1);
		int col = cell_index % (side - 1);

		S = face_index / (1 << (2 * (lvl - 1))) - grid_offset[grid_index];
		g_index = grid_offset[grid_index];

		crn_y = (row * cell_side) + u * cell_side;
		crn_x = (col * cell_side) + v * cell_side;
	}

	CLAMP(crn_x, 0.0f, grid_size);
	CLAMP(crn_y, 0.0f, grid_size);

	if (n != NULL)
		interp_bilinear_grid(&key, grid_data[g_index + S], crn_x, crn_y, 0, n);

	if (co != NULL)
		interp_bilinear_grid(&key, grid_data[g_index + S], crn_x, crn_y, 1, co);
}

/* mode = 0: interpolate normals,
 * mode = 1: interpolate coord */
static void interp_bilinear_mface(DerivedMesh *dm, MFace *mface, const float u, const float v, const int mode, float res[3])
{
	float data[4][3];

	if (mode == 0) {
		dm->getVertNo(dm, mface->v1, data[0]);
		dm->getVertNo(dm, mface->v2, data[1]);
		dm->getVertNo(dm, mface->v3, data[2]);
		dm->getVertNo(dm, mface->v4, data[3]);
	}
	else {
		dm->getVertCo(dm, mface->v1, data[0]);
		dm->getVertCo(dm, mface->v2, data[1]);
		dm->getVertCo(dm, mface->v3, data[2]);
		dm->getVertCo(dm, mface->v4, data[3]);
	}

	interp_bilinear_quad_v3(data, u, v, res);
}

/* mode = 0: interpolate normals,
 * mode = 1: interpolate coord */
static void interp_barycentric_mface(DerivedMesh *dm, MFace *mface, const float u, const float v, const int mode, float res[3])
{
	float data[3][3];

	if (mode == 0) {
		dm->getVertNo(dm, mface->v1, data[0]);
		dm->getVertNo(dm, mface->v2, data[1]);
		dm->getVertNo(dm, mface->v3, data[2]);
	}
	else {
		dm->getVertCo(dm, mface->v1, data[0]);
		dm->getVertCo(dm, mface->v2, data[1]);
		dm->getVertCo(dm, mface->v3, data[2]);
	}

	interp_barycentric_tri_v3(data, u, v, res);
}

/* **************** Displacement Baker **************** */

static void *init_heights_data(MultiresBakeRender *bkr, Image *ima)
{
	MHeightBakeData *height_data;
	ImBuf *ibuf = BKE_image_acquire_ibuf(ima, NULL, NULL);
	DerivedMesh *lodm = bkr->lores_dm;

	height_data = MEM_callocN(sizeof(MHeightBakeData), "MultiresBake heightData");

	height_data->ima = ima;
	height_data->heights = MEM_callocN(sizeof(float) * ibuf->x * ibuf->y, "MultiresBake heights");
	height_data->height_max = -FLT_MAX;
	height_data->height_min = FLT_MAX;

	if (!bkr->use_lores_mesh) {
		SubsurfModifierData smd = {{NULL}};
		int ss_lvl = bkr->tot_lvl - bkr->lvl;

		CLAMP(ss_lvl, 0, 6);

		if (ss_lvl > 0) {
			smd.levels = smd.renderLevels = ss_lvl;
			smd.flags |= eSubsurfModifierFlag_SubsurfUv;

			if (bkr->simple)
				smd.subdivType = ME_SIMPLE_SUBSURF;

			height_data->ssdm = subsurf_make_derived_from_derived(bkr->lores_dm, &smd, NULL, 0);
		}
	}

	height_data->orig_index_mf_to_mpoly = lodm->getTessFaceDataArray(lodm, CD_ORIGINDEX);
	height_data->orig_index_mp_to_orig = lodm->getPolyDataArray(lodm, CD_ORIGINDEX);

	BKE_image_release_ibuf(ima, ibuf, NULL);

	return (void *)height_data;
}

static void apply_heights_data(void *bake_data)
{
	MHeightBakeData *height_data = (MHeightBakeData *)bake_data;
	ImBuf *ibuf = BKE_image_acquire_ibuf(height_data->ima, NULL, NULL);
	int x, y, i;
	float height, *heights = height_data->heights;
	float min = height_data->height_min, max = height_data->height_max;

	for (x = 0; x < ibuf->x; x++) {
		for (y = 0; y < ibuf->y; y++) {
			i = ibuf->x * y + x;

			if (((char *)ibuf->userdata)[i] != FILTER_MASK_USED)
				continue;

			if (ibuf->rect_float) {
				float *rrgbf = ibuf->rect_float + i * 4;

				if (max - min > 1e-5f) height = (heights[i] - min) / (max - min);
				else height = 0;

				rrgbf[0] = rrgbf[1] = rrgbf[2] = height;
			}
			else {
				char *rrgb = (char *)ibuf->rect + i * 4;

				if (max - min > 1e-5f) height = (heights[i] - min) / (max - min);
				else height = 0;

				rrgb[0] = rrgb[1] = rrgb[2] = FTOCHAR(height);
			}
		}
	}

	ibuf->userflags = IB_RECT_INVALID | IB_DISPLAY_BUFFER_INVALID;

	BKE_image_release_ibuf(height_data->ima, ibuf, NULL);
}

static void free_heights_data(void *bake_data)
{
	MHeightBakeData *height_data = (MHeightBakeData *)bake_data;

	if (height_data->ssdm)
		height_data->ssdm->release(height_data->ssdm);

	MEM_freeN(height_data->heights);
	MEM_freeN(height_data);
}

/* MultiresBake callback for heights baking
 * general idea:
 *   - find coord of point with specified UV in hi-res mesh (let's call it p1)
 *   - find coord of point and normal with specified UV in lo-res mesh (or subdivided lo-res
 *     mesh to make texture smoother) let's call this point p0 and n.
 *   - height wound be dot(n, p1-p0) */
static void apply_heights_callback(DerivedMesh *lores_dm, DerivedMesh *hires_dm, const void *bake_data,
                                   ImBuf *ibuf, const int face_index, const int lvl, const float st[2],
                                   float UNUSED(tangmat[3][3]), const int x, const int y)
{
	MTFace *mtface = CustomData_get_layer(&lores_dm->faceData, CD_MTFACE);
	MFace mface;
	MHeightBakeData *height_data = (MHeightBakeData *)bake_data;
	float uv[2], *st0, *st1, *st2, *st3;
	int pixel = ibuf->x * y + x;
	float vec[3], p0[3], p1[3], n[3], len;

	lores_dm->getTessFace(lores_dm, face_index, &mface);

	st0 = mtface[face_index].uv[0];
	st1 = mtface[face_index].uv[1];
	st2 = mtface[face_index].uv[2];

	if (mface.v4) {
		st3 = mtface[face_index].uv[3];
		resolve_quad_uv(uv, st, st0, st1, st2, st3);
	}
	else
		resolve_tri_uv(uv, st, st0, st1, st2);

	CLAMP(uv[0], 0.0f, 1.0f);
	CLAMP(uv[1], 0.0f, 1.0f);

	get_ccgdm_data(lores_dm, hires_dm,
	               height_data->orig_index_mf_to_mpoly, height_data->orig_index_mf_to_mpoly,
	               lvl, face_index, uv[0], uv[1], p1, 0);

	if (height_data->ssdm) {
		get_ccgdm_data(lores_dm, height_data->ssdm,
		               height_data->orig_index_mf_to_mpoly, height_data->orig_index_mf_to_mpoly,
		               0, face_index, uv[0], uv[1], p0, n);
	}
	else {
		lores_dm->getTessFace(lores_dm, face_index, &mface);

		if (mface.v4) {
			interp_bilinear_mface(lores_dm, &mface, uv[0], uv[1], 1, p0);
			interp_bilinear_mface(lores_dm, &mface, uv[0], uv[1], 0, n);
		}
		else {
			interp_barycentric_mface(lores_dm, &mface, uv[0], uv[1], 1, p0);
			interp_barycentric_mface(lores_dm, &mface, uv[0], uv[1], 0, n);
		}
	}

	sub_v3_v3v3(vec, p1, p0);
	len = dot_v3v3(n, vec);

	height_data->heights[pixel] = len;
	if (len < height_data->height_min) height_data->height_min = len;
	if (len > height_data->height_max) height_data->height_max = len;

	if (ibuf->rect_float) {
		float *rrgbf = ibuf->rect_float + pixel * 4;
		rrgbf[3] = 1.0f;

		ibuf->userflags = IB_RECT_INVALID;
	}
	else {
		char *rrgb = (char *)ibuf->rect + pixel * 4;
		rrgb[3] = 255;
	}

	ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;
}

/* **************** Normal Maps Baker **************** */

static void *init_normal_data(MultiresBakeRender *bkr, Image *UNUSED(ima))
{
	MNormalBakeData *normal_data;
	DerivedMesh *lodm = bkr->lores_dm;

	normal_data = MEM_callocN(sizeof(MNormalBakeData), "MultiresBake normalData");

	normal_data->orig_index_mf_to_mpoly = lodm->getTessFaceDataArray(lodm, CD_ORIGINDEX);
	normal_data->orig_index_mp_to_orig = lodm->getPolyDataArray(lodm, CD_ORIGINDEX);

	return (void *)normal_data;
}

static void free_normal_data(void *bake_data)
{
	MNormalBakeData *normal_data = (MNormalBakeData *)bake_data;

	MEM_freeN(normal_data);
}

/* MultiresBake callback for normals' baking
 * general idea:
 *   - find coord and normal of point with specified UV in hi-res mesh
 *   - multiply it by tangmat
 *   - vector in color space would be norm(vec) /2 + (0.5, 0.5, 0.5) */
static void apply_tangmat_callback(DerivedMesh *lores_dm, DerivedMesh *hires_dm, const void *bake_data,
                                   ImBuf *ibuf, const int face_index, const int lvl, const float st[2],
                                   float tangmat[3][3], const int x, const int y)
{
	MTFace *mtface = CustomData_get_layer(&lores_dm->faceData, CD_MTFACE);
	MFace mface;
	MNormalBakeData *normal_data = (MNormalBakeData *)bake_data;
	float uv[2], *st0, *st1, *st2, *st3;
	int pixel = ibuf->x * y + x;
	float n[3], vec[3], tmp[3] = {0.5, 0.5, 0.5};

	lores_dm->getTessFace(lores_dm, face_index, &mface);

	st0 = mtface[face_index].uv[0];
	st1 = mtface[face_index].uv[1];
	st2 = mtface[face_index].uv[2];

	if (mface.v4) {
		st3 = mtface[face_index].uv[3];
		resolve_quad_uv(uv, st, st0, st1, st2, st3);
	}
	else
		resolve_tri_uv(uv, st, st0, st1, st2);

	CLAMP(uv[0], 0.0f, 1.0f);
	CLAMP(uv[1], 0.0f, 1.0f);

	get_ccgdm_data(lores_dm, hires_dm,
	               normal_data->orig_index_mf_to_mpoly, normal_data->orig_index_mp_to_orig,
	               lvl, face_index, uv[0], uv[1], NULL, n);

	mul_v3_m3v3(vec, tangmat, n);
	normalize_v3(vec);
	mul_v3_fl(vec, 0.5);
	add_v3_v3(vec, tmp);

	if (ibuf->rect_float) {
		float *rrgbf = ibuf->rect_float + pixel * 4;
		rrgbf[0] = vec[0];
		rrgbf[1] = vec[1];
		rrgbf[2] = vec[2];
		rrgbf[3] = 1.0f;

		ibuf->userflags = IB_RECT_INVALID;
	}
	else {
		unsigned char *rrgb = (unsigned char *)ibuf->rect + pixel * 4;
		rgb_float_to_uchar(rrgb, vec);
		rrgb[3] = 255;
	}

	ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;
}

/* **************** Common functions public API relates on **************** */

static void count_images(MultiresBakeRender *bkr)
{
	int a, totface;
	DerivedMesh *dm = bkr->lores_dm;
	MTFace *mtface = CustomData_get_layer(&dm->faceData, CD_MTFACE);

	bkr->image.first = bkr->image.last = NULL;
	bkr->tot_image = 0;

	totface = dm->getNumTessFaces(dm);

	for (a = 0; a < totface; a++)
		mtface[a].tpage->id.flag &= ~LIB_DOIT;

	for (a = 0; a < totface; a++) {
		Image *ima = mtface[a].tpage;
		if ((ima->id.flag & LIB_DOIT) == 0) {
			LinkData *data = BLI_genericNodeN(ima);
			BLI_addtail(&bkr->image, data);
			bkr->tot_image++;
			ima->id.flag |= LIB_DOIT;
		}
	}

	for (a = 0; a < totface; a++)
		mtface[a].tpage->id.flag &= ~LIB_DOIT;
}

static void bake_images(MultiresBakeRender *bkr)
{
	LinkData *link;

	for (link = bkr->image.first; link; link = link->next) {
		Image *ima = (Image *)link->data;
		ImBuf *ibuf = BKE_image_acquire_ibuf(ima, NULL, NULL);

		if (ibuf->x > 0 && ibuf->y > 0) {
			ibuf->userdata = MEM_callocN(ibuf->y * ibuf->x, "MultiresBake imbuf mask");

			switch (bkr->mode) {
				case RE_BAKE_NORMALS:
					do_multires_bake(bkr, ima, apply_tangmat_callback, init_normal_data, NULL, free_normal_data);
					break;
				case RE_BAKE_DISPLACEMENT:
					do_multires_bake(bkr, ima, apply_heights_callback, init_heights_data,
					                 apply_heights_data, free_heights_data);
					break;
			}
		}

		BKE_image_release_ibuf(ima, ibuf, NULL);

		ima->id.flag |= LIB_DOIT;
	}
}

static void finish_images(MultiresBakeRender *bkr)
{
	LinkData *link;

	for (link = bkr->image.first; link; link = link->next) {
		Image *ima = (Image *)link->data;
		ImBuf *ibuf = BKE_image_acquire_ibuf(ima, NULL, NULL);

		if (ibuf->x <= 0 || ibuf->y <= 0)
			continue;

		RE_bake_ibuf_filter(ibuf, (char *)ibuf->userdata, bkr->bake_filter);

		ibuf->userflags |= IB_BITMAPDIRTY | IB_DISPLAY_BUFFER_INVALID;

		if (ibuf->rect_float)
			ibuf->userflags |= IB_RECT_INVALID;

		if (ibuf->mipmap[0]) {
			ibuf->userflags |= IB_MIPMAP_INVALID;
			imb_freemipmapImBuf(ibuf);
		}

		if (ibuf->userdata) {
			MEM_freeN(ibuf->userdata);
			ibuf->userdata = NULL;
		}

		BKE_image_release_ibuf(ima, ibuf, NULL);
	}
}

void RE_multires_bake_images(MultiresBakeRender *bkr)
{
	count_images(bkr);
	bake_images(bkr);
	finish_images(bkr);
}
