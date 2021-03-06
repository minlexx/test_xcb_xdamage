#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/damage.h>
#include <xcb/shm.h>
#include <xcb/xcb_image.h>
#include <xcb/randr.h>

#include "png_saver.h"


int g_first_xdamage_event = 0;
int g_first_xdamage_error = 0;
int g_can_use_xshm = 0;


int load_x_extensions(xcb_connection_t *conn) {
    const xcb_query_extension_reply_t *ext_damage_data = xcb_get_extension_data(
                conn, &xcb_damage_id);
    if (!ext_damage_data) {
        fprintf(stderr, "Failed to load XDamage extension!\n");
        xcb_disconnect(conn);
        return 0;
    }
    const xcb_query_extension_reply_t *ext_shm_data = xcb_get_extension_data(
                conn, &xcb_shm_id);
    if (!ext_shm_data) {
        fprintf(stderr, "Failed to load XShm extension!\n");
        //xcb_disconnect(conn);
        //return 0;
        g_can_use_xshm = 0;
    } else {
        g_can_use_xshm = 1;
    }
    const xcb_query_extension_reply_t *ext_randr_data = xcb_get_extension_data(
                conn, &xcb_randr_id);
    if (!ext_randr_data) {
        fprintf(stderr, "Failed to load XRandR extension!\n");
        xcb_disconnect(conn);
        return 0;
    }

    printf("XDamage extension data: first event code: %d, first err code: %d\n",
           (int)ext_damage_data->first_event,
           (int)ext_damage_data->first_error);
    g_first_xdamage_event = ext_damage_data->first_event;
    g_first_xdamage_error = ext_damage_data->first_error;

    xcb_damage_query_version_reply_t *xdamage_version = xcb_damage_query_version_reply(
                conn, xcb_damage_query_version(
                    conn,
                    XCB_DAMAGE_MAJOR_VERSION,
                    XCB_DAMAGE_MINOR_VERSION),
                NULL);
    if (!xdamage_version) {
        fprintf(stderr, "Failed to get XDamage extension version!\n");
        xcb_disconnect(conn);
        return 0;
    }
    printf("Loaded XDamage extension version: %d.%d\n",
           xdamage_version->major_version, xdamage_version->minor_version);
    free(xdamage_version);

    if (g_can_use_xshm) {
        xcb_shm_query_version_reply_t *xshm_version = xcb_shm_query_version_reply(
                    conn, xcb_shm_query_version(conn), NULL);
        if (!xshm_version) {
            fprintf(stderr, "Failed to get XShm extension version!\n");
            xcb_disconnect(conn);
            return 0;
        }
        printf("Loaded XShm extension version: %d.%d; pixmap format: %d; shared pixmaps: %d\n",
               (int)xshm_version->major_version,
               (int)xshm_version->minor_version,
               (int)xshm_version->pixmap_format,
               (int)xshm_version->shared_pixmaps);
        free(xshm_version);
    }

    xcb_randr_query_version_reply_t *xrandr_version = xcb_randr_query_version_reply(
                conn, xcb_randr_query_version(
                    conn,
                    XCB_RANDR_MAJOR_VERSION,
                    XCB_RANDR_MINOR_VERSION
                ),
                NULL
    );
    if (!xrandr_version) {
        fprintf(stderr, "Failed to get XRandR extension version!\n");
        xcb_disconnect(conn);
        return 0;
    }
    printf("Loaded XRandR extension version %d.%d\n",
           xrandr_version->major_version, xrandr_version->minor_version);
    free(xrandr_version);

    return 1;
}


xcb_screen_t *screen_of_display (xcb_connection_t *conn, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
    for (; iter.rem; --screen, xcb_screen_next (&iter))
        if (screen == 0)
            return iter.data;
    return NULL;
}


void fix_update_rectangle_bounds(xcb_rectangle_t *r, const xcb_rectangle_t *bounds) {
    // rectangle end coordinates
    int rx2 = r->x + r->width  - 1;
    int ry2 = r->y + r->height - 1;
    // bounds end coordinates
    int bx2 = bounds->x + bounds->width  - 1;
    int by2 = bounds->y + bounds->height - 1;
    // fix rect coords
    // r->x must be between [bounds->x .. bx2]
    if (r->x < bounds->x) r->x = bounds->x;
    if (r->y < bounds->y) r->y = bounds->y;
    if (r->x > bx2) r->x = bx2;
    if (r->y > by2) r->y = by2;
    if (rx2 < bounds->x)  rx2 = bounds->x;
    if (ry2 < bounds->y)  ry2 = bounds->y;
    if (rx2 > bx2) rx2 = bx2;
    if (ry2 > by2) ry2 = by2;
    // recalc new rect size
    r->width = rx2 - r->x + 1;
    r->height = ry2 - r->y + 1;
}


int main(int argc, char *argv[])
{
    printf("Hello World!\n");

    int i = 0;

    int screen_preferred = 0;
    xcb_connection_t *xconn = xcb_connect(NULL, &screen_preferred);

    printf("xcb_connect() result: %p, screen_preferred: %d\n", xconn, screen_preferred);

    if (!xconn) {
        fprintf(stderr, "Failed to connect to X server!\n");
        return EXIT_FAILURE;
    }

    if (!load_x_extensions(xconn)) return EXIT_FAILURE;

    xcb_window_t root_window = XCB_NONE;
    xcb_screen_t *screen = screen_of_display(xconn, screen_preferred);
    if (!screen) {
        fprintf(stderr, "Cannot get parameters of the default screen!\n");
        xcb_disconnect(xconn);
        return EXIT_FAILURE;
    }

    printf("Preferred screen size: %d x %d, depth %d\n",
           (int)screen->width_in_pixels,
           (int)screen->height_in_pixels,
           (int)screen->root_depth);
    root_window = screen->root;

    // get screen configurations using XRandR
    // 1. get primary output ID
    xcb_randr_output_t primary_output_xid = XCB_NONE;
    xcb_randr_get_output_primary_reply_t *primary_reply = xcb_randr_get_output_primary_reply(
        xconn, xcb_randr_get_output_primary(xconn, root_window), NULL);
    if (primary_reply) {
        primary_output_xid = primary_reply->output;
        printf("RandR: got primary output XID = %ul\n", primary_output_xid);
        free(primary_reply);
    }

    // 2. get screen configurations
    xcb_randr_get_screen_resources_cookie_t screenres_cookie =
            xcb_randr_get_screen_resources(xconn, root_window);
    xcb_randr_get_screen_resources_reply_t *screenres_reply =
            xcb_randr_get_screen_resources_reply(xconn, screenres_cookie, NULL);
    // The returned value must be freed by the caller using free()
    if (!screenres_reply) {
        fprintf(stderr, "Cannot get XRandR screens configuration!\n");
        xcb_disconnect(xconn);
        return EXIT_FAILURE;
    }
    printf("XRandR screen resources:\n  num crtcs: %d\n  num outputs: %d\n  num modes: %d\n",
           (int)screenres_reply->num_crtcs,
           (int)screenres_reply->num_outputs,
           (int)screenres_reply->num_modes
    );

    // 3. loop through outputs to get information about primary output
    char primary_screen_name[32] = {0};   // primary output name
    xcb_rectangle_t primary_screen_rect;  // primary output rectangle
    // --------------------------------------
    xcb_timestamp_t config_timestamp = screenres_reply->config_timestamp;
    int num_outputs = xcb_randr_get_screen_resources_outputs_length(screenres_reply);
    xcb_randr_output_t *randr_outputs = xcb_randr_get_screen_resources_outputs(screenres_reply);
    for (i=0; i<num_outputs; i++) {
        xcb_randr_get_output_info_reply_t *output_info = xcb_randr_get_output_info_reply(
                    xconn,
                    xcb_randr_get_output_info(xconn, randr_outputs[i], config_timestamp),
                    NULL);
        // checks
        if (output_info == NULL) continue;
        if ((output_info->crtc == XCB_NONE) ||
                (output_info->connection == XCB_RANDR_CONNECTION_DISCONNECTED)) {
            free(output_info);
            continue;
        }
        if (primary_output_xid == randr_outputs[i]) {
            // now get some real info about output's CRTC
            xcb_randr_get_crtc_info_reply_t *crtc_info = xcb_randr_get_crtc_info_reply(
                        xconn,
                        xcb_randr_get_crtc_info(xconn, output_info->crtc, config_timestamp),
                        NULL);
            if (crtc_info) {
                //
                uint8_t *output_name = xcb_randr_get_output_info_name(output_info);
                int namelen = xcb_randr_get_output_info_name_length(output_info);
                memset(primary_screen_name, 0, sizeof(primary_screen_name));
                if (namelen >= sizeof(primary_screen_name))
                    namelen = sizeof(primary_screen_name)-1;
                strncpy(primary_screen_name, output_name, namelen);
                primary_screen_rect.x = crtc_info->x;
                primary_screen_rect.y = crtc_info->y;
                primary_screen_rect.width = crtc_info->width;
                primary_screen_rect.height = crtc_info->height;
                //
                printf("  Found primary output info!\n");
                printf("    name = %s, namelen = %d\n", primary_screen_name, namelen);
                printf("    pos (%d,%d) sz (%d,%d)\n", crtc_info->x, crtc_info->y,
                       crtc_info->width, crtc_info->height);
                //
                free(crtc_info);
            }
        }
        //
        free(output_info);
    }
    free(screenres_reply);

    // shared memory vars
    xcb_shm_seg_t shm_segment = XCB_NONE;
    int shm_id = -1;  // invalid value
    uint8_t *shm_data = NULL;
    xcb_image_t *screenshot = NULL;
    xcb_image_t *screenshot_shm = NULL;

    if (g_can_use_xshm == 0) {
        // not using shared memory
        // get image of primary screen and try to save it
        screenshot = xcb_image_get(xconn, root_window,
                                    primary_screen_rect.x,
                                    primary_screen_rect.y,
                                    primary_screen_rect.width,
                                    primary_screen_rect.height,
                                    0xFFFFFFFF, XCB_IMAGE_FORMAT_Z_PIXMAP);
        if (screenshot) {
            printf("We took a screenshot of primary screen!\n");
            printf("  img data: image structure = %p, base = %p, data = %p\n",
                   screenshot, screenshot->base, screenshot->data);
            save_as_png(screenshot);
            xcb_image_destroy(screenshot);
            screenshot = NULL;
        }
    } else {
        // use shared memory

        // create an image to store data to
        //  " If base == 0 and bytes == ~0 and data == 0 on "
        //  " entry, no storage will be auto-allocated.     "
        screenshot_shm = xcb_image_create_native(
                    xconn,
                    primary_screen_rect.width,
                    primary_screen_rect.height,
                    XCB_IMAGE_FORMAT_Z_PIXMAP,
                    screen->root_depth,  // this would probably be = 24
                    NULL,         // base == 0
                    (uint32_t)~0, // bytes == ~0
                    NULL);        // and data == 0, no storage will be allocated
        if( !screenshot_shm) {
            fprintf(stderr, "ERROR: cannot create image with size %d x %d!\n",
                    primary_screen_rect.width, primary_screen_rect.height);
            xcb_disconnect(xconn);
            return EXIT_FAILURE;
        }
        printf("Allocated native image structure = %p, base = %p, data = %p\n",
               screenshot_shm, screenshot_shm->base, screenshot_shm->data);

        // create chared memory segment in our process
        // shm segment is allocated and attached only once, large enough to
        //    store the whole image of primary monitor. it will be detached
        //    at the end of the program
        shm_id = shmget(IPC_PRIVATE, screenshot_shm->size, IPC_CREAT | 0777);
        if (shm_id == -1) {
            fprintf(stderr, "ERROR: cannot get %ul bytes of shared memory!\n",
                    screenshot->size);
            xcb_disconnect(xconn);
            return EXIT_FAILURE;
        }
        // attach to our process address sapce
        shm_data = shmat(shm_id, NULL, 0);

        // attach this also to X server
        shm_segment = xcb_generate_id(xconn);
        xcb_shm_attach(xconn, shm_segment, shm_id, 0);

        // get image data from X server into shared memory segment
        xcb_shm_get_image_cookie_t sgi_cookie = xcb_shm_get_image(
                    xconn,
                    root_window,
                    primary_screen_rect.x,
                    primary_screen_rect.y,
                    primary_screen_rect.width,
                    primary_screen_rect.height,
                    0xFFFFFFFF,
                    XCB_IMAGE_FORMAT_Z_PIXMAP,
                    shm_segment,
                    0);

        xcb_shm_get_image_reply_t *sgi_reply = xcb_shm_get_image_reply(
                    xconn, sgi_cookie, NULL);

        if (sgi_reply) {
            printf("Got xcb_shm_get_image_reply(), data at %p, will save "
                   "as PNG from shared mem\n", shm_data);

            screenshot_shm->data = shm_data;
            save_as_png(screenshot_shm);

            free(sgi_reply);
        }

        xcb_image_destroy(screenshot_shm);
        screenshot_shm = NULL;
    }

    // in XCB we need to manually generate IDs
    xcb_damage_damage_t dmg = xcb_generate_id(xconn);
    xcb_damage_create(xconn, dmg, root_window, XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);

    printf("Generated ID for damage handle = %u\n", dmg);

    xcb_flush(xconn);

    // now event loop
    printf("Entering event loop...\n");
    xcb_generic_event_t *evt = NULL;
    while(1) {
        evt = xcb_wait_for_event(xconn);
        /* Strip off the highest bit (set if the event is generated) !!! */
        int evt_type = (int)evt->response_type & 0x7F;
        //printf("got event %d\n", evt_type);

        if ((evt_type >= g_first_xdamage_event) &&
                (evt_type <= (g_first_xdamage_event + XCB_DAMAGE_NOTIFY))) {
            xcb_damage_notify_event_t *xdevt = (xcb_damage_notify_event_t *)evt;
            //
            // this is dangerous, becauses it causes writes to output in console and
            // cause even more damage events
#if 0
            printf(" * this is  damage event, tmstamp = %u\n", xdevt->timestamp);
            printf("    damage handle = %u\n", xdevt->damage);
            // this is just damaged area rectangle
            printf("    area     : (%d,%d) sz (%d, %d)\n",
                   (int)xdevt->area.x,     (int)xdevt->area.y,
                   (int)xdevt->area.width, (int)xdevt->area.height);
            // geometry is always the full window size
            printf("    geometry : (%d,%d) sz (%d, %d)\n",
                   (int)xdevt->geometry.x,     (int)xdevt->geometry.y,
                   (int)xdevt->geometry.width, (int)xdevt->geometry.height);
#endif

            fix_update_rectangle_bounds(&xdevt->area, &primary_screen_rect);
            // after that fix rectangle can have a null size
            if ((xdevt->area.width <= 1) && (xdevt->area.height <= 1))
                continue;  // skip, rect too small or null

            // save damage rect screenshot
            if (g_can_use_xshm == 0) {
                // usual alow XGetImage with all image bytes sent over wire
                screenshot = xcb_image_get(xconn, root_window,
                                           xdevt->area.x, xdevt->area.y,
                                           xdevt->area.width, xdevt->area.height,
                                           0xFFFFFFFF, XCB_IMAGE_FORMAT_Z_PIXMAP);
            } else {
                // use shared memory
                // get image data into shared memory segment
                xcb_shm_get_image_cookie_t sgi_cookie = xcb_shm_get_image(
                            xconn,             root_window,
                            xdevt->area.x,     xdevt->area.y,
                            xdevt->area.width, xdevt->area.height,
                            0xFFFFFFFF,        XCB_IMAGE_FORMAT_Z_PIXMAP,
                            shm_segment,       0);

                xcb_shm_get_image_reply_t *sgi_reply = xcb_shm_get_image_reply(
                            xconn, sgi_cookie, NULL);

                if (sgi_reply) {
                    screenshot_shm = xcb_image_create_native(
                                xconn,
                                xdevt->area.width,
                                xdevt->area.height,
                                XCB_IMAGE_FORMAT_Z_PIXMAP,
                                screen->root_depth,  // this would probably be = 24
                                NULL,         // base == 0
                                (uint32_t)~0, // bytes == ~0
                                NULL);

                    if (screenshot_shm) {
                        screenshot_shm->data = shm_data;
                        save_as_png(screenshot_shm);
                        xcb_image_destroy(screenshot_shm);
                        screenshot_shm = NULL;
                    }

                    free(sgi_reply);
                }
            }
            if (screenshot) {
                save_as_png(screenshot);
                xcb_image_destroy(screenshot);
                screenshot = NULL;
            }

            //xcb_damage_subtract(xconn, dmg, XCB_NONE, XCB_NONE);
        }
    }
    printf("Event loop ended, cleanup, exit\n");

    xcb_damage_destroy(xconn, dmg);

    if (g_can_use_xshm && (shm_id != -1)) {
        xcb_shm_detach(xconn, shm_segment);  // inform X server to detach it's shm segment
        shmdt(shm_data);  // detach shared memory segment from our address space
        shmctl(shm_id, IPC_RMID, 0);  // mark segment as not used any more (by our process)
    }

    xcb_disconnect(xconn);
    return 0;
}
