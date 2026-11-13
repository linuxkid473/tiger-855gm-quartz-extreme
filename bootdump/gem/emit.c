/* Emits an i830-style (Mesa i915 classic, 855GM variant) batch that draws one textured quad.
   Output: text on stdout:  "B <hexdword>" per batch dword, "R <dwordindex> <bo> <delta>" per relocation.
   bo 0 = color target, bo 1 = texture.  usage: emit [tri|rect] [w] [h] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t GLuint;
#include "intel_reg.h"
#include "enums.h"
#include "i830_reg_mesa.h"
#ifndef SZ_TO_HW
#define SZ_TO_HW(sz) ((sz)-2 & 0x3)
#endif
#ifndef VRTX_TEX_SET_FMT
#define VRTX_TEX_SET_FMT(n, x) ((x)<<((n)*2))
#endif
static uint32_t b[512]; static int n;
static struct { int idx, bo, delta; } rl[16]; static int nr;
#define OUT(x) (b[n++] = (x))
#define RELOC(BO, DELTA) do { rl[nr].idx = n; rl[nr].bo = (BO); rl[nr].delta = (DELTA); nr++; OUT(0); } while (0)
static void emit_arr(const uint32_t *a, int c) { for (int i = 0; i < c; i++) OUT(a[i]); }
static float vtx[3*8];
static void fv(int *i, float f) { uint32_t u; memcpy(&u, &f, 4); b[(*i)++] = u; }

int main(int argc, char **argv)
{
   int rect = argc > 1 && !strcmp(argv[1], "rect");
   int solid = argc > 1 && !strcmp(argv[1], "solid");   /* triangles with diffuse colour only, no texture */
   int blt = argc > 1 && !strcmp(argv[1], "blt");       /* 2D blitter fill: harness sanity check */
   int tex = !solid;
   int W = argc > 2 ? atoi(argv[2]) : 64, H = argc > 3 ? atoi(argv[3]) : 64;
   int cpitch = (W * 4 + 63) & ~63;     /* render target pitch (bytes) */
   int tw = 64, th = 64, tpitch = tw * 4;

   if (blt) {   /* XY_COLOR_BLT fill of the whole target with 0xff00ff00 */
      OUT(XY_COLOR_BLT_CMD | 0x300000 | 4);
      OUT(BR13_8888 | (0xf0 << 16) | cpitch);
      OUT(0); OUT((H << 16) | W);
      RELOC(0, 0);
      OUT(0xff00ff00);
      OUT(MI_FLUSH); OUT(MI_BATCH_BUFFER_END);
      if (n & 1) OUT(MI_NOOP);
      for (int i = 0; i < n; i++) printf("B %08x\n", b[i]);
      for (int i = 0; i < nr; i++) printf("R %d %d %d\n", rl[i].idx, rl[i].bo, rl[i].delta);
      printf("P %d %d %d\n", W, H, cpitch);
      return 0;
   }

   /* ---- invariant state (i830_emit_invarient_state) ---- */
   OUT(_3DSTATE_DFLT_DIFFUSE_CMD); OUT(0);
   OUT(_3DSTATE_DFLT_SPEC_CMD); OUT(0);
   OUT(_3DSTATE_DFLT_Z_CMD); OUT(0);
   OUT(_3DSTATE_FOG_MODE_CMD); OUT(FOGFUNC_ENABLE | FOG_LINEAR_CONST | FOGSRC_INDEX_Z | ENABLE_FOG_DENSITY); OUT(0); OUT(0);
   for (int u = 0; u < 4; u++)
      OUT(_3DSTATE_MAP_TEX_STREAM_CMD | MAP_UNIT(u) | DISABLE_TEX_STREAM_BUMP | ENABLE_TEX_STREAM_COORD_SET |
          TEX_STREAM_COORD_SET(u) | ENABLE_TEX_STREAM_MAP_IDX | TEX_STREAM_MAP_IDX(u));
   for (int u = 0; u < 4; u++) { OUT(_3DSTATE_MAP_COORD_TRANSFORM); OUT(DISABLE_TEX_TRANSFORM | TEXTURE_SET(u)); }
   OUT(_3DSTATE_VERTEX_TRANSFORM); OUT(DISABLE_VIEWPORT_TRANSFORM | DISABLE_PERSPECTIVE_DIVIDE);
   OUT(_3DSTATE_W_STATE_CMD); OUT(MAGIC_W_STATE_DWORD1); OUT(0x3f800000);
   OUT(_3DSTATE_COLOR_FACTOR_CMD); OUT(0x80808080);

   /* ---- raster rules ---- */
   OUT(_3DSTATE_RASTER_RULES_CMD | ENABLE_POINT_RASTER_RULE | OGL_POINT_RASTER_RULE | ENABLE_LINE_STRIP_PROVOKE_VRTX |
       ENABLE_TRI_FAN_PROVOKE_VRTX | ENABLE_TRI_STRIP_PROVOKE_VRTX | LINE_STRIP_PROVOKE_VRTX(1) |
       TRI_FAN_PROVOKE_VRTX(2) | TRI_STRIP_PROVOKE_VRTX(2));

   /* ---- context state (I830_UPLOAD_CTX), order = I830_CTXREG_* ---- */
   uint32_t v0 = _3DSTATE_VFT0_CMD | VFT0_XYZW | VFT0_DIFFUSE | (tex ? VFT0_TEX_COUNT(1) : 0);
   uint32_t v2 = _3DSTATE_VFT1_CMD | (tex ? VRTX_TEX_SET_FMT(0, SZ_TO_HW(2)) : 0);
   OUT(_3DSTATE_MODES_1_CMD | ENABLE_COLR_BLND_FUNC | BLENDFUNC_ADD | ENABLE_SRC_BLND_FACTOR |
       SRC_BLND_FACT(BLENDFACT_ONE) | ENABLE_DST_BLND_FACTOR | DST_BLND_FACT(BLENDFACT_ZERO));
   OUT(_3DSTATE_MODES_2_CMD | ENABLE_GLOBAL_DEPTH_BIAS | GLOBAL_DEPTH_BIAS(0) | ENABLE_ALPHA_TEST_FUNC |
       ALPHA_TEST_FUNC(COMPAREFUNC_ALWAYS) | ALPHA_REF_VALUE(0));
   OUT(_3DSTATE_MODES_3_CMD | ENABLE_DEPTH_TEST_FUNC | DEPTH_TEST_FUNC(COMPAREFUNC_LESS) | ENABLE_ALPHA_SHADE_MODE |
       ALPHA_SHADE_MODE(SHADE_MODE_LINEAR) | ENABLE_FOG_SHADE_MODE | FOG_SHADE_MODE(SHADE_MODE_LINEAR) |
       ENABLE_SPEC_SHADE_MODE | SPEC_SHADE_MODE(SHADE_MODE_LINEAR) | ENABLE_COLOR_SHADE_MODE |
       COLOR_SHADE_MODE(SHADE_MODE_LINEAR) | ENABLE_CULL_MODE | CULLMODE_NONE);
   OUT(_3DSTATE_MODES_4_CMD | ENABLE_LOGIC_OP_FUNC | LOGIC_OP_FUNC(LOGICOP_COPY) | ENABLE_STENCIL_TEST_MASK |
       STENCIL_TEST_MASK(0xff) | ENABLE_STENCIL_WRITE_MASK | STENCIL_WRITE_MASK(0xff));
   OUT(_3DSTATE_MODES_5_CMD | FLUSH_TEXTURE_CACHE | ENABLE_SPRITE_POINT_TEX | SPRITE_POINT_TEX_OFF |
       ENABLE_FIXED_LINE_WIDTH | FIXED_LINE_WIDTH(0x2) | ENABLE_FIXED_POINT_WIDTH | FIXED_POINT_WIDTH(1));
   OUT(_3DSTATE_INDPT_ALPHA_BLEND_CMD | DISABLE_INDPT_ALPHA_BLEND | ENABLE_ALPHA_BLENDFUNC | ABLENDFUNC_ADD);
   OUT(_3DSTATE_STENCIL_TEST_CMD | ENABLE_STENCIL_PARMS | STENCIL_FAIL_OP(STENCILOP_KEEP) |
       STENCIL_PASS_DEPTH_FAIL_OP(STENCILOP_KEEP) | STENCIL_PASS_DEPTH_PASS_OP(STENCILOP_KEEP) |
       ENABLE_STENCIL_TEST_FUNC | STENCIL_TEST_FUNC(COMPAREFUNC_ALWAYS) | ENABLE_STENCIL_REF_VALUE | STENCIL_REF_VALUE(0));
   OUT(_3DSTATE_ENABLES_1_CMD | DISABLE_LOGIC_OP | DISABLE_STENCIL_TEST | DISABLE_DEPTH_BIAS | DISABLE_SPEC_ADD |
       DISABLE_FOG | DISABLE_ALPHA_TEST | DISABLE_COLOR_BLEND | DISABLE_DEPTH_TEST);
   OUT(_3DSTATE_ENABLES_2_CMD | DISABLE_STENCIL_WRITE | ENABLE_TEX_CACHE | DISABLE_DITHER | ENABLE_COLOR_MASK |
       ENABLE_COLOR_WRITE | DISABLE_DEPTH_WRITE);   /* IGT: no depth buffer is defined, so depth writes must be off */
   OUT(_3DSTATE_AA_CMD | AA_LINE_ECAAR_WIDTH_ENABLE | AA_LINE_ECAAR_WIDTH_1_0 | AA_LINE_REGION_WIDTH_ENABLE |
       AA_LINE_REGION_WIDTH_1_0 | AA_LINE_DISABLE);
   OUT(_3DSTATE_FOG_COLOR_CMD | FOG_COLOR_RED(0) | FOG_COLOR_GREEN(0) | FOG_COLOR_BLUE(0));
   OUT(_3DSTATE_CONST_BLEND_COLOR_CMD); OUT(0);
   OUT(v0); OUT(v2);
   OUT(_3DSTATE_MAP_COORD_SETBIND_CMD);
   OUT(tex ? (8 << 0) : 0);          /* Mesa: mcsb1 = 0 when the vertex has no texture coords */

   /* ---- buffers (I830_UPLOAD_BUFFERS), no depth region ---- */
   OUT(_3DSTATE_BUF_INFO_CMD);
   OUT(BUF_3D_ID_COLOR_BACK | BUF_3D_PITCH(cpitch));
   RELOC(0, 0);
   OUT(_3DSTATE_DST_BUF_VARS_CMD);
   OUT(DSTORG_HORT_BIAS(0x8) | DSTORG_VERT_BIAS(0x8) | COLR_BUF_ARGB8888);
   OUT(_3DSTATE_SCISSOR_RECT_0_CMD); OUT(0); OUT(0);
   OUT(_3DSTATE_SCISSOR_ENABLE_CMD | DISABLE_SCISSOR_RECT);
   OUT(_3DSTATE_DRAWRECT_INFO); OUT(0); OUT(0);
   OUT(((W - 1) & 0xffff) | ((H - 1) << 16)); OUT(0); OUT(MI_NOOP);

   /* ---- stipple ---- */
   OUT(_3DSTATE_STIPPLE); OUT(0);      /* 2-dword command: header + pattern (was missing: swallowed the next command) */

   /* ---- texture unit 0 (i830_update_tex_unit, 855GM variant) ---- */
   if (tex) {
   OUT(_3DSTATE_LOAD_STATE_IMMEDIATE_2 | (LOAD_TEXTURE_MAP0 << 0) | 4);
   RELOC(1, 0);
   uint32_t s1 = ((th - 1) << TM0S1_HEIGHT_SHIFT) | ((tw - 1) << TM0S1_WIDTH_SHIFT) | MAPSURF_32BIT | MT_32BIT_ARGB8888;
   uint32_t s2 = (((tpitch / 4) - 1) << TM0S2_PITCH_SHIFT) | TM0S2_CUBE_FACE_ENA_MASK | TM0S2_LOD_PRECLAMP;
   uint32_t s3 = (0 << TM0S3_LOD_BIAS_SHIFT) | (FILTER_NEAREST << TM0S3_MIN_FILTER_SHIFT) |
                 (MIPFILTER_NONE << TM0S3_MIP_FILTER_SHIFT) | (FILTER_NEAREST << TM0S3_MAG_FILTER_SHIFT);
   OUT(s1); OUT(s2); OUT(s3); OUT(0x00000000 /* border */);
   OUT(_3DSTATE_MAP_COORD_SET_CMD | MAP_UNIT(0) | ENABLE_TEXCOORD_PARAMS | TEXCOORDTYPE_CARTESIAN | ENABLE_ADDR_V_CNTL |
       TEXCOORD_ADDR_V_MODE(TEXCOORDMODE_CLAMP) | ENABLE_ADDR_U_CNTL | TEXCOORD_ADDR_U_MODE(TEXCOORDMODE_CLAMP));
   OUT(_3DSTATE_MAP_CUBE | MAP_UNIT(0));
   }

   /* ---- texture blend: replace with texel ---- */
   OUT(_3DSTATE_MAP_BLEND_OP_CMD(0) | TEXPIPE_COLOR | ENABLE_TEXOUTPUT_WRT_SEL | TEXOP_OUTPUT_CURRENT |
       DISABLE_TEX_CNTRL_STAGE | TEXOP_SCALE_1X | TEXOP_MODIFY_PARMS | TEXOP_LAST_STAGE | TEXBLENDOP_ARG1);
   OUT(_3DSTATE_MAP_BLEND_OP_CMD(0) | TEXPIPE_ALPHA | ENABLE_TEXOUTPUT_WRT_SEL | TEXOP_OUTPUT_CURRENT |
       TEXOP_SCALE_1X | TEXOP_MODIFY_PARMS | TEXBLENDOP_ARG1);
   OUT(_3DSTATE_MAP_BLEND_ARG_CMD(0) | TEXPIPE_COLOR | TEXBLEND_ARG1 | TEXBLENDARG_MODIFY_PARMS | (tex ? TEXBLENDARG_TEXEL0 : TEXBLENDARG_DIFFUSE));
   OUT(_3DSTATE_MAP_BLEND_ARG_CMD(0) | TEXPIPE_ALPHA | TEXBLEND_ARG1 | TEXBLENDARG_MODIFY_PARMS | (tex ? TEXBLENDARG_TEXEL0 : TEXBLENDARG_DIFFUSE));

   /* ---- primitive, inline vertices: x y z w diffuse s t  (7 dwords) ---- */
   int nv = rect ? 3 : 6; int vs = tex ? 7 : 5;
   OUT(_3DPRIMITIVE | (rect ? PRIM3D_RECTLIST : PRIM3D_TRILIST) | (nv * vs - 1));
   float x0 = 4, y0 = 4, x1 = W - 4, y1 = H - 4;
   float P[6][2] = {{x0,y0},{x1,y0},{x1,y1},{x0,y0},{x1,y1},{x0,y1}};
   float T[6][2] = {{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
   if (rect) { float PR[3][2] = {{x1,y1},{x0,y1},{x0,y0}}, TR[3][2] = {{1,1},{0,1},{0,0}};
               memcpy(P, PR, sizeof PR); memcpy(T, TR, sizeof TR); }
   for (int i = 0; i < nv; i++) {
      int k = 0; (void)k;
      uint32_t u; float f;
      f = P[i][0]; memcpy(&u, &f, 4); OUT(u);
      f = P[i][1]; memcpy(&u, &f, 4); OUT(u);
      f = 0.5f;    memcpy(&u, &f, 4); OUT(u);
      f = 1.0f;    memcpy(&u, &f, 4); OUT(u);
      OUT(0xffffffff);
      if (tex) { f = T[i][0]; memcpy(&u, &f, 4); OUT(u);
      f = T[i][1]; memcpy(&u, &f, 4); OUT(u); }
   }
   OUT(MI_FLUSH | FLUSH_MAP_CACHE);
   OUT(MI_BATCH_BUFFER_END);
   if (n & 1) OUT(MI_NOOP);

   for (int i = 0; i < n; i++) printf("B %08x\n", b[i]);
   for (int i = 0; i < nr; i++) printf("R %d %d %d\n", rl[i].idx, rl[i].bo, rl[i].delta);
   printf("P %d %d %d\n", W, H, cpitch);
   return 0;
}
