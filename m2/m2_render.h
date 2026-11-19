/* m2_render.h — the game's render sites a fix detours or writes, found in the running image.
 *
 * m2-sdk.dll resolves them by byte signature and checks each against the others and against the frame
 * stores they write (mercs2_formats::tree_sites), so a fix built on them arms on every build the
 * signatures match rather than on one exe's addresses:
 *
 *     m2_tree_sites t;
 *     t.size = sizeof t;
 *     if (!m2_render_tree_sites(&t)) m2_logf("tree sites: %s", m2_render_last_error());
 *
 * The call waits up to 30 s for SecuROM to decrypt the game's code. Never call it from DllMain.
 */
#ifndef M2_RENDER_H
#define M2_RENDER_H

#include "m2_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The billboard-tree render loops (BUG-014: the Z-prepass loop pushes no world matrix, so its packets
 * draw with the one last pushed before it; the shadow loop pushes the identity first). */
typedef struct m2_tree_sites {
    unsigned int size;            /* set by the caller: sizeof(m2_tree_sites) */
    void* zpass_loop;             /* void __cdecl (void): the tree Z-prepass loop */
    void* shadow_loop;            /* void __cdecl (void): the tree shadow loop */
    const float* identity;        /* the 4x4 identity matrix the shadow loop copies (16 floats) */
    void* matrix_copy;            /* the 64-byte matrix copy both pushes call: EAX = destination,
                                   * ECX = source; not callable as a C function */
    unsigned short* matrix_index; /* the world-matrix index each packet is drawn with */
    unsigned short* draw_record;  /* the draw-record index stored beside it; the tree identity push sets 1 */
    unsigned int* batches;        /* the tree batch count both loops iterate */
    unsigned short* matrices;     /* the frame's world-matrix counter: a push takes i = (*matrices)++ */
    unsigned int* parity;         /* the frame's buffer parity, 0 or 1 */
    float* matrix_table;          /* entry i of the frame: matrix_table + (*parity * matrix_cap + i) * 16 */
    unsigned int matrix_cap;      /* entries per parity */
} m2_tree_sites;

/* Fill *out (out->size set by the caller). 1 on success; 0 with m2_render_last_error() set and *out
 * untouched: a NULL out, a size smaller than this header's struct, or sites that did not resolve within
 * 30 s (the message names what did not match). */
M2_API int m2_render_tree_sites(m2_tree_sites* out);

/* The display-mode sites (BUG-016: the render state's constructor keeps an enumerated mode only at 85 Hz or
 * less and only near the aspect ratios it knows, and ApplySettings falls back to 640x480 at 60 Hz when the
 * Mercs2.ini mode is not kept, then returns an error nobody checks and the renderer reads a NULL device).
 * Since 0.6.1. */
typedef struct m2_display_mode_sites {
    unsigned int size;            /* set by the caller: sizeof(m2_display_mode_sites) */
    void* refresh_cmp;            /* `cmp esi, 0x55; ja skip` in the constructor's per-mode loop: the 9 bytes
                                   * m2_render_display_mode_filter's patch replaces. ESI = the mode's refresh
                                   * rate, EDX = its width, ECX = its height */
    void* skip;                   /* the loop's skip label: drops the mode */
    void* keep;                   /* the loop's keep path (it reads nothing the aspect test writes) */
    unsigned int index_in_ebx;    /* 1: the loop's EnumAdapterModes index is EBX; 0: the dword at
                                   * [esp + index_stack_offset] */
    unsigned int index_stack_offset;
    void* find_mode;              /* the mode search: ECX = render state, EAX = an 8-byte out record, stdcall
                                   * (width, height, bpp, refresh), returns EAX; no match writes (-1, -1) */
    void* first_search_call;      /* ApplySettings' `call rel32` searching for the settings' mode */
    void* fallback_call;          /* ApplySettings' `call rel32` searching for 640x480 at 60 Hz, after it wrote
                                   * those into the settings */
    void* set_mode;               /* stdcall (record lo, record hi): sets the present parameters from the
                                   * record's EnumAdapterModes index byte */
    void** pending_state;         /* the render state ApplySettings is applying (the search's ECX) */
    unsigned int* settings_width; /* the settings the first search reads and the fallback writes */
    unsigned int* settings_height;
    unsigned int* settings_bpp_index; /* bits per pixel = (index + 1) << 4 */
    unsigned int* settings_refresh;
    unsigned int list_offset;     /* in the render state: the mode list (8-byte records: [0] index byte,
                                   * [1] refresh, [2] u16 height, [4] u16 width, [6] bpp, [7] widescreen) */
    unsigned int count_offset;    /* its record count */
    unsigned int adapter_offset;  /* the adapter ordinal */
} m2_display_mode_sites;

/* Fill *out (out->size set by the caller). 1 on success; 0 with m2_render_last_error() set and *out
 * untouched, as m2_render_tree_sites. Waits up to 30 s for SecuROM; never from DllMain. Since 0.6.1. */
M2_API int m2_render_display_mode_sites(m2_display_mode_sites* out);

/* Assemble the mode-list filter's replacement for address `at` (executable memory the caller owns): code that
 * drops a mode whose refresh rate is above max_refresh or whose enumeration index is above max_index and
 * keeps every other mode without the aspect test, into `code` (at most code_cap bytes; 32 is enough), and into
 * `patch` the 9 bytes that replace refresh_cmp with a jump to `at`. Returns the code's length; 0 with
 * m2_render_last_error() set when the sites have not resolved (call m2_render_display_mode_sites first) or
 * code_cap is too small. Since 0.6.1. */
M2_API unsigned int m2_render_display_mode_filter(void* at, unsigned int max_refresh, unsigned int max_index,
                                                  unsigned char* code, unsigned int code_cap,
                                                  unsigned char patch[9]);

/* The position in the mode list (`count` 8-byte records at `list`) of the mode to use when the ini mode is not
 * listed: width x height, else desktop_width x desktop_height (0 x 0 when unknown), each at its refresh rate
 * closest to 60 Hz (the lower of two equally close). -1 when the list has neither. *used_desktop (when not
 * NULL) is set to 1 when the desktop resolution was the one found. Since 0.6.1. */
M2_API int m2_render_pick_display_mode(const void* list, unsigned int count, unsigned int width, unsigned int height,
                                       unsigned int desktop_width, unsigned int desktop_height, int* used_desktop);

/* The last failure a render call recorded on the calling thread, or NULL. Valid until that thread's next
 * failure; another thread's failure never touches it (since 0.5.1). Also in m2-sdk.log. */
M2_API const char* m2_render_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* M2_RENDER_H */
