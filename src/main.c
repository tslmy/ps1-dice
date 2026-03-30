#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxpad.h>
#include <psxapi.h>
#include <inline_c.h>
#include "libs/game_pad.h"

/* ── Display / OT constants ─────────────────────────────────────────── */

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define OT_LENGTH 1024
#define BUFFER_LENGTH 16384

/* ── Fixed-point helpers (20.12) ────────────────────────────────────── */

#define FP_SHIFT 12
#define FP_ONE (1 << FP_SHIFT)
#define FP_HALF (FP_ONE >> 1)

#define INT_TO_FP(x) ((x) << FP_SHIFT)
#define FP_TO_INT(x) ((x) >> FP_SHIFT)
#define FP_MUL(a, b) ((int32_t)(((int64_t)(a) * (b)) >> FP_SHIFT))
#define FP_DIV(a, b) ((int32_t)(((int64_t)(a) << FP_SHIFT) / (b)))

/* ── Render context (kept from original template) ───────────────────── */

typedef struct
{
    DISPENV disp_env;
    DRAWENV draw_env;
    uint32_t ot[OT_LENGTH];
    uint8_t buffer[BUFFER_LENGTH];
} RenderBuffer;

typedef struct
{
    RenderBuffer buffers[2];
    uint8_t *next_packet;
    int active_buffer;
} RenderContext;

static void initialize_render_context(RenderContext *ctx, int w, int h, int r, int g, int b)
{
    SetDefDrawEnv(&ctx->buffers[0].draw_env, 0, 0, w, h);
    SetDefDispEnv(&ctx->buffers[0].disp_env, 0, h, w, h);
    SetDefDrawEnv(&ctx->buffers[1].draw_env, 0, h, w, h);
    SetDefDispEnv(&ctx->buffers[1].disp_env, 0, 0, w, h);

    setRGB0(&ctx->buffers[0].draw_env, r, g, b);
    setRGB0(&ctx->buffers[1].draw_env, r, g, b);
    ctx->buffers[0].draw_env.isbg = 1;
    ctx->buffers[1].draw_env.isbg = 1;

    ctx->active_buffer = 0;
    ctx->next_packet = ctx->buffers[0].buffer;
    ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);
    SetDispMask(1);
}

static void flip_buffers(RenderContext *ctx)
{
    DrawSync(0);
    VSync(0);

    RenderBuffer *draw = &ctx->buffers[ctx->active_buffer];
    RenderBuffer *disp = &ctx->buffers[ctx->active_buffer ^ 1];

    PutDispEnv(&disp->disp_env);
    DrawOTagEnv(&draw->ot[OT_LENGTH - 1], &draw->draw_env);

    ctx->active_buffer ^= 1;
    ctx->next_packet = disp->buffer;
    ClearOTagR(disp->ot, OT_LENGTH);
}

static void *new_primitive(RenderContext *ctx, int z, size_t size)
{
    RenderBuffer *buf = &ctx->buffers[ctx->active_buffer];
    uint8_t *prim = ctx->next_packet;
    if (z < 0)
        z = 0;
    if (z >= OT_LENGTH)
        z = OT_LENGTH - 1;
    addPrim(&buf->ot[z], prim);
    ctx->next_packet += size;
    assert(ctx->next_packet <= &buf->buffer[BUFFER_LENGTH]);
    return (void *)prim;
}

static void draw_text(RenderContext *ctx, int x, int y, int z, const char *text)
{
    RenderBuffer *buf = &ctx->buffers[ctx->active_buffer];
    ctx->next_packet = (uint8_t *)
        FntSort(&buf->ot[z], ctx->next_packet, x, y, text);
    assert(ctx->next_packet <= &buf->buffer[BUFFER_LENGTH]);
}

/* ── Texture loading ────────────────────────────────────────────────── */

extern const uint32_t tim_dice_atlas[];

static TIM_IMAGE dice_tim;

static void load_tim(TIM_IMAGE *out, const uint32_t *data)
{
    GetTimInfo(data, out);
    LoadImage(out->prect, out->paddr);
    if (out->mode & 0x8)
        LoadImage(out->crect, out->caddr);
    DrawSync(0);
}

/* ── Cube geometry ──────────────────────────────────────────────────── */

#define CUBE_SIZE 56 /* half-extent in GTE units */

static const SVECTOR cube_verts[8] = {
    {-CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE, 0},
    {CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE, 0},
    {CUBE_SIZE, CUBE_SIZE, -CUBE_SIZE, 0},
    {-CUBE_SIZE, CUBE_SIZE, -CUBE_SIZE, 0},
    {-CUBE_SIZE, -CUBE_SIZE, CUBE_SIZE, 0},
    {CUBE_SIZE, -CUBE_SIZE, CUBE_SIZE, 0},
    {CUBE_SIZE, CUBE_SIZE, CUBE_SIZE, 0},
    {-CUBE_SIZE, CUBE_SIZE, CUBE_SIZE, 0},
};

/*  6 quad faces. Vertex order: v0 v1 v2 v3 for POLY_FT4. */
static const int cube_faces[6][4] = {
    {4, 5, 7, 6}, /* front  (+Z) → face 1 */
    {1, 0, 2, 3}, /* back   (-Z) → face 6 */
    {0, 4, 3, 7}, /* left   (-X) → face 2 */
    {5, 1, 6, 2}, /* right  (+X) → face 5 */
    {2, 3, 6, 7}, /* top    (+Y) → face 3 */
    {0, 1, 4, 5}, /* bottom (-Y) → face 4 */
};

/* Die value each face shows. Opposite faces sum to 7. */
static const int face_value[6] = {1, 6, 2, 5, 3, 4};

/* Face normals (ONE = 4096 in PSX fixed-point). */
static const SVECTOR face_normals[6] = {
    {0, 0, ONE, 0},
    {0, 0, -ONE, 0},
    {-ONE, 0, 0, 0},
    {ONE, 0, 0, 0},
    {0, ONE, 0, 0},
    {0, -ONE, 0, 0},
};

/* UV coords for each face in the 192×32 atlas (each die face is 32×32). */
typedef struct
{
    uint8_t u0, v0, u1, v1, u2, v2, u3, v3;
} FaceUV;
static FaceUV atlas_uv[6];

static void init_atlas_uvs(void)
{
    for (int i = 0; i < 6; i++)
    {
        int col = face_value[i] - 1;
        uint8_t u = (uint8_t)(col * 32);
        atlas_uv[i].u0 = u;
        atlas_uv[i].v0 = 0;
        atlas_uv[i].u1 = u + 31;
        atlas_uv[i].v1 = 0;
        atlas_uv[i].u2 = u;
        atlas_uv[i].v2 = 31;
        atlas_uv[i].u3 = u + 31;
        atlas_uv[i].v3 = 31;
    }
}

/* ── Simple LCG random ──────────────────────────────────────────────── */

static uint32_t rng_state = 1;

static void seed_rng(uint32_t s) { rng_state = s ? s : 1; }

static uint32_t rand_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static int rand_range(int lo, int hi)
{
    return lo + (int)(rand_next() % (uint32_t)(hi - lo + 1));
}

/* ── Dice physics ───────────────────────────────────────────────────── */

typedef struct
{
    int32_t rx, ry, rz;    /* rotation (PSX angle, full circle = 4096) */
    int32_t vrx, vry, vrz; /* angular velocity (angle-units / frame) */
    int32_t pos_y;         /* vertical position (fixed-point 20.12) */
    int32_t vel_y;         /* vertical velocity */
    int32_t ground_y;
    int bounces;
} DicePhysics;

#define VEL_DAMP 63257 /* ~0.965 × 65536 */
#define VEL_DAMP_SHIFT 16
#define GRAVITY (FP_ONE / 8)
#define RESTITUTION (FP_ONE * 55 / 100)
#define SETTLE_THRESHOLD 3

static void dice_start_roll(DicePhysics *d, uint32_t frame_counter)
{
    seed_rng(frame_counter * 7919u + 104729u);
    d->vrx = rand_range(40, 100) * (rand_next() & 1 ? 1 : -1);
    d->vry = rand_range(40, 100) * (rand_next() & 1 ? 1 : -1);
    d->vrz = rand_range(30, 80) * (rand_next() & 1 ? 1 : -1);
    d->pos_y = INT_TO_FP(-200);
    d->vel_y = 0;
    d->ground_y = INT_TO_FP(0);
    d->bounces = 0;
}

/* returns 1 when the die has settled */
static int dice_update(DicePhysics *d)
{
    d->vrx = (int32_t)(((int64_t)d->vrx * VEL_DAMP) >> VEL_DAMP_SHIFT);
    d->vry = (int32_t)(((int64_t)d->vry * VEL_DAMP) >> VEL_DAMP_SHIFT);
    d->vrz = (int32_t)(((int64_t)d->vrz * VEL_DAMP) >> VEL_DAMP_SHIFT);

    d->rx = (d->rx + d->vrx) & 0xFFF;
    d->ry = (d->ry + d->vry) & 0xFFF;
    d->rz = (d->rz + d->vrz) & 0xFFF;

    d->vel_y += GRAVITY;
    d->pos_y += d->vel_y;

    if (d->pos_y >= d->ground_y)
    {
        d->pos_y = d->ground_y;
        if (d->vel_y > FP_ONE / 4)
        {
            d->vel_y = -FP_MUL(d->vel_y, RESTITUTION);
            d->bounces++;
        }
        else
        {
            d->vel_y = 0;
        }
    }

    int avx = d->vrx < 0 ? -d->vrx : d->vrx;
    int avy = d->vry < 0 ? -d->vry : d->vry;
    int avz = d->vrz < 0 ? -d->vrz : d->vrz;
    int avy2 = d->vel_y < 0 ? -d->vel_y : d->vel_y;

    if (avx < SETTLE_THRESHOLD && avy < SETTLE_THRESHOLD &&
        avz < SETTLE_THRESHOLD && avy2 < FP_ONE / 8 &&
        d->pos_y >= d->ground_y - FP_ONE)
    {
        d->rx = (((d->rx + 512) & ~0x3FF) + 200) & 0xFFF;
        d->ry = (((d->ry + 512) & ~0x3FF) + 156) & 0xFFF;
        d->rz = ((d->rz + 512) & ~0x3FF) & 0xFFF;
        d->vrx = d->vry = d->vrz = 0;
        d->vel_y = 0;
        d->pos_y = d->ground_y;
        return 1;
    }
    return 0;
}

/* Which face points most upward (+Y) after rotation → die result 1-6. */
static int dice_result(const DicePhysics *d)
{
    MATRIX mtx;
    SVECTOR rot;
    rot.vx = d->rx;
    rot.vy = d->ry;
    rot.vz = d->rz;
    RotMatrix(&rot, &mtx);

    int best = 0;
    int32_t best_y = -99999;
    for (int i = 0; i < 6; i++)
    {
        int32_t ny = ((int32_t)mtx.m[1][0] * face_normals[i].vx +
                      (int32_t)mtx.m[1][1] * face_normals[i].vy +
                      (int32_t)mtx.m[1][2] * face_normals[i].vz) >>
                     12;
        if (ny > best_y)
        {
            best_y = ny;
            best = i;
        }
    }
    return face_value[best];
}

/* ── Game states ────────────────────────────────────────────────────── */

typedef enum
{
    STATE_MENU,
    STATE_ROLLING,
    STATE_RESULT
} GameState;

/* ── Draw the 3D cube ───────────────────────────────────────────────── */

static void draw_cube(RenderContext *ctx, const DicePhysics *d, TIM_IMAGE *tim)
{
    MATRIX mtx;
    SVECTOR rot;
    VECTOR trans;

    rot.vx = d->rx;
    rot.vy = d->ry;
    rot.vz = d->rz;
    RotMatrix(&rot, &mtx);

    trans.vx = 0;
    trans.vy = FP_TO_INT(d->pos_y) - 20;
    trans.vz = 500;
    TransMatrix(&mtx, &trans);

    gte_SetRotMatrix(&mtx);
    gte_SetTransMatrix(&mtx);

    uint16_t tpage = getTPage(tim->mode & 0x3, 0,
                              tim->prect->x, tim->prect->y);
    uint16_t clut = 0;
    if (tim->mode & 0x8)
        clut = getClut(tim->crect->x, tim->crect->y);

    for (int f = 0; f < 6; f++)
    {
        const int *idx = cube_faces[f];
        long otz, p, flg;

        SVECTOR v0 = cube_verts[idx[0]];
        SVECTOR v1 = cube_verts[idx[1]];
        SVECTOR v2 = cube_verts[idx[2]];
        SVECTOR v3 = cube_verts[idx[3]];

        /* project v0, v1, v2 */
        gte_ldv3(&v0, &v1, &v2);
        gte_rtpt();

        /* backface cull — front-facing = negative nclip on PS1 */
        gte_nclip();
        gte_stopz(&p);
        if (p >= 0)
            continue;

        /* save screen XY for v0, v1, v2 before pipeline shifts */
        int32_t sxy0, sxy1, sxy2;
        gte_stsxy0(&sxy0);
        gte_stsxy1(&sxy1);
        gte_stsxy2(&sxy2);

        /* project v3 — SZ pipeline now holds all 4 Z values */
        gte_ldv0(&v3);
        gte_rtps();

        int32_t sxy3;
        gte_stsxy2(&sxy3);

        /* average Z for depth-sort */
        gte_avsz4();
        gte_stotz(&otz);
        if (otz <= 0 || otz >= OT_LENGTH)
            continue;

        POLY_FT4 *poly = (POLY_FT4 *)new_primitive(ctx, otz, sizeof(POLY_FT4));
        setPolyFT4(poly);

        /* write saved screen coords into polygon */
        *(int32_t *)&poly->x0 = sxy0;
        *(int32_t *)&poly->x1 = sxy1;
        *(int32_t *)&poly->x2 = sxy2;
        *(int32_t *)&poly->x3 = sxy3;

        /* texture coords from atlas */
        FaceUV *uv = &atlas_uv[f];
        setUV4(poly, uv->u0, uv->v0, uv->u1, uv->v1,
                      uv->u2, uv->v2, uv->u3, uv->v3);

        poly->tpage = tpage;
        poly->clut = clut;
        setRGB0(poly, 128, 128, 128);
    }
}

/* ── Ground plane ───────────────────────────────────────────────────── */

static void draw_ground(RenderContext *ctx)
{
    TILE *t = (TILE *)new_primitive(ctx, OT_LENGTH - 2, sizeof(TILE));
    setTile(t);
    setXY0(t, 0, 160);
    setWH(t, SCREEN_XRES, SCREEN_YRES - 160);
    setRGB0(t, 20, 20, 50);
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(int argc, const char **argv)
{
    ResetGraph(0);
    FntLoad(960, 0);
    FntOpen(0, 0, SCREEN_XRES, SCREEN_YRES, 0, 512);

    RenderContext ctx;
    initialize_render_context(&ctx, SCREEN_XRES, SCREEN_YRES, 10, 10, 40);

    InitGeom();
    gte_SetGeomOffset(SCREEN_XRES / 2, SCREEN_YRES / 2);
    gte_SetGeomScreen(350);

    /* Set ZSF3/ZSF4 for gte_avsz3/4 depth sorting.
       OTZ = ZSFn * sum(SZ) / 4096.  Typical: ZSF3 = OT/3, ZSF4 = OT/4. */
    {
        int zsf3 = OT_LENGTH / 3;
        int zsf4 = OT_LENGTH / 4;
        __asm__ volatile("ctc2 %0, $29" : : "r"(zsf3));
        __asm__ volatile("ctc2 %0, $30" : : "r"(zsf4));
    }

    load_tim(&dice_tim, tim_dice_atlas);
    init_atlas_uvs();

    GamePad pad = init_game_pad(0);

    GameState state = STATE_MENU;
    DicePhysics dice = {0};
    dice.pos_y = INT_TO_FP(0);
    uint32_t frame_counter = 0;
    int roll_result = 0;
    char text_buf[64];

    for (;;)
    {
        sync_pad(&pad);
        frame_counter++;

        switch (state)
        {
        case STATE_MENU:
            draw_text(&ctx, 88, 60, 0, "DICE ROLLER");
            draw_text(&ctx, 72, 120, 0, "PRESS X TO ROLL");
            draw_text(&ctx, 60, 180, 0, "PS1 HOMEBREW DEMO");

            dice.rx = (frame_counter * 8) & 0xFFF;
            dice.ry = (frame_counter * 6) & 0xFFF;
            dice.rz = 0;
            dice.pos_y = INT_TO_FP(0);
            draw_cube(&ctx, &dice, &dice_tim);
            draw_ground(&ctx);

            if (is_button_just_released(&pad, PAD_BUTTON_X))
            {
                dice_start_roll(&dice, frame_counter);
                state = STATE_ROLLING;
            }
            break;

        case STATE_ROLLING:
            draw_text(&ctx, 104, 16, 0, "ROLLING...");

            if (dice_update(&dice))
            {
                roll_result = dice_result(&dice);
                state = STATE_RESULT;
            }

            draw_cube(&ctx, &dice, &dice_tim);
            draw_ground(&ctx);
            break;

        case STATE_RESULT:
            sprintf(text_buf, "YOU ROLLED: %d", roll_result);
            draw_text(&ctx, 80, 16, 0, text_buf);
            draw_text(&ctx, 64, 210, 0, "X: ROLL AGAIN");

            draw_cube(&ctx, &dice, &dice_tim);
            draw_ground(&ctx);

            if (is_button_just_released(&pad, PAD_BUTTON_X))
            {
                dice_start_roll(&dice, frame_counter);
                state = STATE_ROLLING;
            }
            break;
        }

        flip_buffers(&ctx);
    }

    cleanup_game_pad(&pad);
    return 0;
}
