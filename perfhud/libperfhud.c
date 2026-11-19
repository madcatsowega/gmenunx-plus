/*
 * libperfhud.so - Zero-flicker in-game FPS / CPU% / MHz HUD for RetroFW 2.3 (RS-97 Plus)
 *
 * Loaded via LD_PRELOAD when Performance HUD is enabled in GMenuNX+.
 * Does NOT link against libSDL or libSDL2 so non-SDL helper processes (/bin/sh, mount)
 * pay near-zero overhead. Resolves real SDL functions lazily via RTLD_NEXT.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>

/* Compact 5x7 uppercase + digits + symbols bitmap font (5 columns per glyph, bit0=top..bit6=bottom) */
static const uint8_t FONT_5X7[][5] = {
	/* 0: ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00},
	/* 1: '0' */ {0x3E, 0x51, 0x49, 0x45, 0x3E},
	/* 2: '1' */ {0x00, 0x42, 0x7F, 0x40, 0x00},
	/* 3: '2' */ {0x42, 0x61, 0x51, 0x49, 0x46},
	/* 4: '3' */ {0x21, 0x41, 0x45, 0x4B, 0x31},
	/* 5: '4' */ {0x18, 0x14, 0x12, 0x7F, 0x10},
	/* 6: '5' */ {0x27, 0x45, 0x45, 0x45, 0x39},
	/* 7: '6' */ {0x3C, 0x4A, 0x49, 0x49, 0x30},
	/* 8: '7' */ {0x01, 0x71, 0x09, 0x05, 0x03},
	/* 9: '8' */ {0x36, 0x49, 0x49, 0x49, 0x36},
	/* 10:'9' */ {0x06, 0x49, 0x49, 0x29, 0x1E},
	/* 11:'%' */ {0x23, 0x13, 0x08, 0x64, 0x62},
	/* 12:'F' */ {0x7F, 0x09, 0x09, 0x09, 0x01},
	/* 13:'P' */ {0x7F, 0x09, 0x09, 0x09, 0x06},
	/* 14:'S' */ {0x46, 0x49, 0x49, 0x49, 0x31},
	/* 15:'M' */ {0x7F, 0x02, 0x0C, 0x02, 0x7F},
	/* 16:'H' */ {0x7F, 0x08, 0x08, 0x08, 0x7F},
	/* 17:'Z' */ {0x61, 0x51, 0x49, 0x45, 0x43},
	/* 18:'C' */ {0x3E, 0x41, 0x41, 0x41, 0x22},
	/* 19:'U' */ {0x3F, 0x40, 0x40, 0x40, 0x3F},
	/* 20:'-' */ {0x08, 0x08, 0x08, 0x08, 0x08},
	/* 21:'.' */ {0x00, 0x60, 0x60, 0x00, 0x00},
	/* 22:':' */ {0x00, 0x36, 0x36, 0x00, 0x00},
	/* 23:'B' */ {0x7F, 0x49, 0x49, 0x49, 0x36},
	/* 24:'A' */ {0x7E, 0x11, 0x11, 0x11, 0x7E},
	/* 25:'T' */ {0x01, 0x01, 0x7F, 0x01, 0x01},
};

static int glyph_index(char c) {
	if (c >= '0' && c <= '9') return 1 + (c - '0');
	switch (c) {
		case '%': return 11;
		case 'F': case 'f': return 12;
		case 'P': case 'p': return 13;
		case 'S': case 's': return 14;
		case 'M': case 'm': return 15;
		case 'H': case 'h': return 16;
		case 'Z': case 'z': return 17;
		case 'C': case 'c': return 18;
		case 'U': case 'u': return 19;
		case '-': return 20;
		case '.': return 21;
		case ':': return 22;
		case 'B': case 'b': return 23;
		case 'A': case 'a': return 24;
		case 'T': case 't': return 25;
		default: return 0;
	}
}

/* HUD state */
static int hud_inited = 0;
static int hud_pos = 0; /* 0=TL, 1=TR, 2=BL, 3=BR */
static int hud_fallback_mhz = 0;
static volatile uint32_t *cpm_map = NULL;
static int mem_fd = -1;

static uint64_t last_sample_ns = 0;
static uint32_t frame_count = 0;
static int cur_fps = 0;
static int cur_cpu_pct = 0;
static int cur_mhz = 0;
static uint64_t prev_cpu_total = 0;
static uint64_t prev_cpu_idle = 0;

static char hud_fps_str[16] = "--FPS";
static char hud_rest_str[32] = " --% ---M";

/* Real SDL 1.2 pointers */
typedef int (*fn_SDL_Flip)(SDL_Surface *);
typedef void (*fn_SDL_UpdateRect)(SDL_Surface *, Sint32, Sint32, Uint32, Uint32);
typedef void (*fn_SDL_UpdateRects)(SDL_Surface *, int, SDL_Rect *);
typedef int (*fn_SDL_LockSurface)(SDL_Surface *);
typedef void (*fn_SDL_UnlockSurface)(SDL_Surface *);

static fn_SDL_Flip real_SDL_Flip = NULL;
static fn_SDL_UpdateRect real_SDL_UpdateRect = NULL;
static fn_SDL_UpdateRects real_SDL_UpdateRects = NULL;
static fn_SDL_LockSurface real_SDL_LockSurface = NULL;
static fn_SDL_UnlockSurface real_SDL_UnlockSurface = NULL;

/* Real SDL 2 pointers (opaque structs so we don't need SDL2 headers) */
typedef struct { int x, y, w, h; } Sdl2Rect;
typedef void (*fn_SDL_RenderPresent)(void *);
typedef int (*fn_SDL_GetRendererOutputSize)(void *, int *, int *);
typedef int (*fn_SDL_GetRenderDrawColor)(void *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);
typedef int (*fn_SDL_SetRenderDrawColor)(void *, uint8_t, uint8_t, uint8_t, uint8_t);
typedef int (*fn_SDL_RenderFillRect)(void *, const Sdl2Rect *);

static fn_SDL_RenderPresent real_SDL_RenderPresent = NULL;
static fn_SDL_GetRendererOutputSize real_SDL_GetRendererOutputSize = NULL;
static fn_SDL_GetRenderDrawColor real_SDL_GetRenderDrawColor = NULL;
static fn_SDL_SetRenderDrawColor real_SDL_SetRenderDrawColor = NULL;
static fn_SDL_RenderFillRect real_SDL_RenderFillRect = NULL;

static int in_present = 0;

static void *resolve_sym(const char *name, const char *libname) {
	void *sym = dlsym(RTLD_NEXT, name);
	if (!sym && libname) {
		void *h = dlopen(libname, RTLD_LAZY | RTLD_NOLOAD);
		if (h) sym = dlsym(h, name);
	}
	return sym;
}

static uint64_t monotonic_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int read_jz_mhz(void) {
	if (!cpm_map) return hud_fallback_mhz;
	uint32_t cpccr = cpm_map[0x00 >> 2];
	uint32_t cppcr0 = cpm_map[0x10 >> 2];
	uint32_t cppsr = cpm_map[0x14 >> 2];
	static const unsigned divs[] = {1, 2, 3, 4, 6, 8};
	unsigned cdiv = (cpccr & 0xfu) < 6 ? divs[cpccr & 0xfu] : 1;
	unsigned pll_hz;
	if ((cppsr & (1u << 30)) || (cppsr & (1u << 31))) {
		pll_hz = 12000000u;
	} else {
		unsigned m = ((cppcr0 >> 24) & 0x7fu) * 2u;
		unsigned n = (cppcr0 >> 18) & 0xfu;
		unsigned od = (cppcr0 >> 16) & 0x3u;
		if (n == 0) n = 1;
		pll_hz = (unsigned)((12000000ull * m) / (n * (1u << od)));
	}
	return (int)((pll_hz / cdiv) / 1000000u);
}

static void sample_cpu_pct(void) {
	FILE *f = fopen("/proc/stat", "r");
	if (!f) return;
	unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
	int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu",
	               &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
	fclose(f);
	if (n < 4) return;
	uint64_t idle_all = (uint64_t)(idle + iowait);
	uint64_t total = (uint64_t)(user + nice + sys + idle + iowait + irq + softirq);
	if (prev_cpu_total > 0 && total > prev_cpu_total) {
		uint64_t dt = total - prev_cpu_total;
		uint64_t di = (idle_all >= prev_cpu_idle) ? (idle_all - prev_cpu_idle) : 0;
		uint64_t busy = (dt > di) ? (dt - di) : 0;
		cur_cpu_pct = (int)((busy * 100ull) / dt);
		if (cur_cpu_pct > 100) cur_cpu_pct = 100;
	}
	prev_cpu_total = total;
	prev_cpu_idle = idle_all;
}

static void hud_init_once(void) {
	if (hud_inited) return;
	hud_inited = 1;

	const char *pos = getenv("GMENUNX_HUD_POS");
	if (pos) {
		if (!strcmp(pos, "TR") || !strcmp(pos, "Top-Right")) hud_pos = 1;
		else if (!strcmp(pos, "BL") || !strcmp(pos, "Bottom-Left")) hud_pos = 2;
		else if (!strcmp(pos, "BR") || !strcmp(pos, "Bottom-Right")) hud_pos = 3;
		else hud_pos = 0;
	}
	const char *mhzEnv = getenv("GMENUNX_CPU_MHZ");
	if (mhzEnv) hud_fallback_mhz = atoi(mhzEnv);

	/* Read-only CPM mmap to display live JZ4760B CPU frequency */
	mem_fd = open("/dev/mem", O_RDONLY);
	if (mem_fd >= 0) {
		void *p = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, mem_fd, 0x10000000u);
		if (p != MAP_FAILED) {
			cpm_map = (volatile uint32_t *)p;
		} else {
			close(mem_fd);
			mem_fd = -1;
		}
	}

	cur_mhz = read_jz_mhz();
	sample_cpu_pct();
	last_sample_ns = monotonic_ns();
}

static void hud_on_frame(void) {
	hud_init_once();
	frame_count++;

	uint64_t now = monotonic_ns();
	uint64_t dt_ns = now - last_sample_ns;
	if (dt_ns >= 500000000ull) {
		cur_fps = (int)((uint64_t)frame_count * 1000000000ull / dt_ns);
		if (cur_fps > 999) cur_fps = 999;
		frame_count = 0;
		last_sample_ns = now;
		sample_cpu_pct();
		cur_mhz = read_jz_mhz();

		snprintf(hud_fps_str, sizeof(hud_fps_str), "%2dFPS", cur_fps);
		if (cur_mhz > 0) {
			snprintf(hud_rest_str, sizeof(hud_rest_str), " %2d%% %dM", cur_cpu_pct, cur_mhz);
		} else {
			snprintf(hud_rest_str, sizeof(hud_rest_str), " %2d%%", cur_cpu_pct);
		}
	}
}

static uint32_t pack_rgb(const SDL_PixelFormat *fmt, uint8_t r, uint8_t g, uint8_t b) {
	return (((uint32_t)r >> fmt->Rloss) << fmt->Rshift) |
	       (((uint32_t)g >> fmt->Gloss) << fmt->Gshift) |
	       (((uint32_t)b >> fmt->Bloss) << fmt->Bshift) |
	       fmt->Amask;
}

static void put_px(SDL_Surface *s, int x, int y, uint32_t col, int bpp) {
	if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
	uint8_t *row = (uint8_t *)s->pixels + y * s->pitch;
	if (bpp == 2) {
		((uint16_t *)row)[x] = (uint16_t)col;
	} else if (bpp == 4) {
		((uint32_t *)row)[x] = col;
	} else if (bpp == 3) {
		uint8_t *p = row + x * 3;
		p[0] = (uint8_t)(col & 0xff);
		p[1] = (uint8_t)((col >> 8) & 0xff);
		p[2] = (uint8_t)((col >> 16) & 0xff);
	}
}

static void fill_box_sdl1(SDL_Surface *s, int x, int y, int w, int h, uint32_t col, int bpp) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > s->w) w = s->w - x;
	if (y + h > s->h) h = s->h - y;
	if (w <= 0 || h <= 0) return;

	for (int yy = y; yy < y + h; yy++) {
		uint8_t *row = (uint8_t *)s->pixels + yy * s->pitch;
		if (bpp == 2) {
			uint16_t *p = (uint16_t *)row + x;
			uint16_t c16 = (uint16_t)col;
			for (int i = 0; i < w; i++) p[i] = c16;
		} else if (bpp == 4) {
			uint32_t *p = (uint32_t *)row + x;
			for (int i = 0; i < w; i++) p[i] = col;
		} else if (bpp == 3) {
			uint8_t *p = row + x * 3;
			for (int i = 0; i < w; i++) {
				p[0] = (uint8_t)(col & 0xff);
				p[1] = (uint8_t)((col >> 8) & 0xff);
				p[2] = (uint8_t)((col >> 16) & 0xff);
				p += 3;
			}
		}
	}
}

static void draw_text_sdl1(SDL_Surface *s, int x, int y, const char *txt, uint32_t col, int bpp, int scaleY) {
	for (; *txt; txt++) {
		const uint8_t *g = FONT_5X7[glyph_index(*txt)];
		for (int cx = 0; cx < 5; cx++) {
			uint8_t bits = g[cx];
			for (int cy = 0; cy < 7; cy++) {
				if (bits & (1u << cy)) {
					put_px(s, x + cx, y + cy * scaleY, col, bpp);
					if (scaleY == 2) put_px(s, x + cx, y + cy * scaleY + 1, col, bpp);
				}
			}
		}
		x += 6;
	}
}

static void draw_hud_sdl1(SDL_Surface *s) {
	if (!s || !s->pixels || !s->format) return;
	int bpp = s->format->BytesPerPixel;
	if (bpp < 2 || bpp > 4) return;
	if (s->w < 100 || s->h < 40) return;

	int scaleY = (s->w == 320 && s->h == 480) ? 2 : 1;
	int lenFps = (int)strlen(hud_fps_str);
	int lenRest = (int)strlen(hud_rest_str);
	int totalChars = lenFps + lenRest;

	int boxW = totalChars * 6 + 5;
	int boxH = (7 + 4) * scaleY;
	int marginX = 3;
	int marginY = 3 * scaleY;

	int bx = (hud_pos == 1 || hud_pos == 3) ? (s->w - boxW - marginX) : marginX;
	int by = (hud_pos == 2 || hud_pos == 3) ? (s->h - boxH - marginY) : marginY;
	if (bx < 0) bx = 0;
	if (by < 0) by = 0;

	int locked = 0;
	if (SDL_MUSTLOCK(s)) {
		if (!real_SDL_LockSurface)
			real_SDL_LockSurface = (fn_SDL_LockSurface)resolve_sym("SDL_LockSurface", "libSDL-1.2.so.0");
		if (!real_SDL_UnlockSurface)
			real_SDL_UnlockSurface = (fn_SDL_UnlockSurface)resolve_sym("SDL_UnlockSurface", "libSDL-1.2.so.0");
		if (real_SDL_LockSurface && real_SDL_LockSurface(s) == 0) locked = 1;
		else return;
	}

	uint32_t bgCol = pack_rgb(s->format, 14, 18, 30);
	uint32_t brdCol = pack_rgb(s->format, 68, 92, 130);
	uint32_t fpsCol;
	if (cur_fps >= 55) fpsCol = pack_rgb(s->format, 96, 255, 112);
	else if (cur_fps >= 30) fpsCol = pack_rgb(s->format, 255, 220, 72);
	else fpsCol = pack_rgb(s->format, 255, 88, 88);
	uint32_t txtCol = pack_rgb(s->format, 240, 245, 255);

	fill_box_sdl1(s, bx, by, boxW, boxH, bgCol, bpp);
	/* 1px border */
	fill_box_sdl1(s, bx, by, boxW, scaleY, brdCol, bpp);
	fill_box_sdl1(s, bx, by + boxH - scaleY, boxW, scaleY, brdCol, bpp);
	fill_box_sdl1(s, bx, by, 1, boxH, brdCol, bpp);
	fill_box_sdl1(s, bx + boxW - 1, by, 1, boxH, brdCol, bpp);

	int tx = bx + 3;
	int ty = by + 2 * scaleY;
	draw_text_sdl1(s, tx, ty, hud_fps_str, fpsCol, bpp, scaleY);
	draw_text_sdl1(s, tx + lenFps * 6, ty, hud_rest_str, txtCol, bpp, scaleY);

	if (locked && real_SDL_UnlockSurface) real_SDL_UnlockSurface(s);
}

int SDL_Flip(SDL_Surface *screen) {
	if (!real_SDL_Flip)
		real_SDL_Flip = (fn_SDL_Flip)resolve_sym("SDL_Flip", "libSDL-1.2.so.0");
	if (!real_SDL_Flip) return -1;

	if (!in_present) {
		in_present = 1;
		hud_on_frame();
		draw_hud_sdl1(screen);
		int rc = real_SDL_Flip(screen);
		in_present = 0;
		return rc;
	}
	return real_SDL_Flip(screen);
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h) {
	if (!real_SDL_UpdateRect)
		real_SDL_UpdateRect = (fn_SDL_UpdateRect)resolve_sym("SDL_UpdateRect", "libSDL-1.2.so.0");
	if (!real_SDL_UpdateRect) return;

	if (!in_present && screen) {
		int isFull = (w == 0 && h == 0) || ((int)w >= screen->w / 2 && (int)h >= screen->h / 2);
		if (isFull) {
			in_present = 1;
			hud_on_frame();
			draw_hud_sdl1(screen);
			real_SDL_UpdateRect(screen, x, y, w, h);
			in_present = 0;
			return;
		}
	}
	real_SDL_UpdateRect(screen, x, y, w, h);
}

void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects) {
	if (!real_SDL_UpdateRects)
		real_SDL_UpdateRects = (fn_SDL_UpdateRects)resolve_sym("SDL_UpdateRects", "libSDL-1.2.so.0");
	if (!real_SDL_UpdateRects) return;

	if (!in_present && screen && numrects > 0) {
		in_present = 1;
		hud_on_frame();
		draw_hud_sdl1(screen);
		real_SDL_UpdateRects(screen, numrects, rects);
		in_present = 0;
		return;
	}
	real_SDL_UpdateRects(screen, numrects, rects);
}

/* SDL 2 Renderer Present hook */
static void draw_text_sdl2(void *renderer, int x, int y, const char *txt, int scaleY) {
	for (; *txt; txt++) {
		const uint8_t *g = FONT_5X7[glyph_index(*txt)];
		for (int cx = 0; cx < 5; cx++) {
			uint8_t bits = g[cx];
			for (int cy = 0; cy < 7; cy++) {
				if (bits & (1u << cy)) {
					Sdl2Rect r = {x + cx, y + cy * scaleY, 1, scaleY};
					real_SDL_RenderFillRect(renderer, &r);
				}
			}
		}
		x += 6;
	}
}

void SDL_RenderPresent(void *renderer) {
	if (!real_SDL_RenderPresent)
		real_SDL_RenderPresent = (fn_SDL_RenderPresent)resolve_sym("SDL_RenderPresent", "libSDL2-2.0.so.0");
	if (!real_SDL_RenderPresent) return;

	if (!in_present && renderer) {
		in_present = 1;
		hud_on_frame();

		if (!real_SDL_GetRendererOutputSize)
			real_SDL_GetRendererOutputSize = (fn_SDL_GetRendererOutputSize)resolve_sym("SDL_GetRendererOutputSize", "libSDL2-2.0.so.0");
		if (!real_SDL_GetRenderDrawColor)
			real_SDL_GetRenderDrawColor = (fn_SDL_GetRenderDrawColor)resolve_sym("SDL_GetRenderDrawColor", "libSDL2-2.0.so.0");
		if (!real_SDL_SetRenderDrawColor)
			real_SDL_SetRenderDrawColor = (fn_SDL_SetRenderDrawColor)resolve_sym("SDL_SetRenderDrawColor", "libSDL2-2.0.so.0");
		if (!real_SDL_RenderFillRect)
			real_SDL_RenderFillRect = (fn_SDL_RenderFillRect)resolve_sym("SDL_RenderFillRect", "libSDL2-2.0.so.0");

		if (real_SDL_GetRendererOutputSize && real_SDL_SetRenderDrawColor && real_SDL_RenderFillRect) {
			int w = 0, h = 0;
			if (real_SDL_GetRendererOutputSize(renderer, &w, &h) == 0 && w >= 100 && h >= 40) {
				uint8_t or_ = 0, og = 0, ob = 0, oa = 255;
				if (real_SDL_GetRenderDrawColor) real_SDL_GetRenderDrawColor(renderer, &or_, &og, &ob, &oa);

				int scaleY = (w == 320 && h == 480) ? 2 : 1;
				int lenFps = (int)strlen(hud_fps_str);
				int lenRest = (int)strlen(hud_rest_str);
				int boxW = (lenFps + lenRest) * 6 + 5;
				int boxH = 11 * scaleY;
				int bx = (hud_pos == 1 || hud_pos == 3) ? (w - boxW - 3) : 3;
				int by = (hud_pos == 2 || hud_pos == 3) ? (h - boxH - 3 * scaleY) : (3 * scaleY);

				Sdl2Rect bg = {bx, by, boxW, boxH};
				real_SDL_SetRenderDrawColor(renderer, 14, 18, 30, 220);
				real_SDL_RenderFillRect(renderer, &bg);

				if (cur_fps >= 55) real_SDL_SetRenderDrawColor(renderer, 96, 255, 112, 255);
				else if (cur_fps >= 30) real_SDL_SetRenderDrawColor(renderer, 255, 220, 72, 255);
				else real_SDL_SetRenderDrawColor(renderer, 255, 88, 88, 255);
				draw_text_sdl2(renderer, bx + 3, by + 2 * scaleY, hud_fps_str, scaleY);

				real_SDL_SetRenderDrawColor(renderer, 240, 245, 255, 255);
				draw_text_sdl2(renderer, bx + 3 + lenFps * 6, by + 2 * scaleY, hud_rest_str, scaleY);

				real_SDL_SetRenderDrawColor(renderer, or_, og, ob, oa);
			}
		}

		real_SDL_RenderPresent(renderer);
		in_present = 0;
		return;
	}
	real_SDL_RenderPresent(renderer);
}
