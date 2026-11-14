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

/* The last failure a render call recorded on the calling thread, or NULL. Valid until that thread's next
 * failure; another thread's failure never touches it (since 0.5.1). Also in m2-sdk.log. */
M2_API const char* m2_render_last_error(void);

#endif /* M2_RENDER_H */
