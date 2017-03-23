#include <stdio.h>
#include <stdlib.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/damage.h>
#include <xcb/shm.h>
#include <xcb/randr.h>


int g_first_xdamage_event = 0;
int g_first_xdamage_error = 0;


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
        xcb_disconnect(conn);
        return 0;
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


int main(int argc, char *argv[])
{
    printf("Hello World!\n");

    int screen_preferred = 0;
    xcb_connection_t *xconn = xcb_connect(NULL, &screen_preferred);

    printf("xcb_connect() result: %p, screen_preferred: %d\n", xconn, screen_preferred);

    if (!xconn) {
        fprintf(stderr, "Failed to connect to X server!\n");
        return EXIT_FAILURE;
    }

    if (!load_x_extensions(xconn)) return EXIT_FAILURE;

    xcb_window_t root_window = 0;
    xcb_screen_t *screen = screen_of_display(xconn, screen_preferred);
    if (!screen) {
        fprintf(stderr, "Cannot get parameters of the default screen!\n");
        xcb_disconnect(xconn);
    }

    printf("Preferred screen size: %d x %d, depth %d\n",
           (int)screen->width_in_pixels,
           (int)screen->height_in_pixels,
           (int)screen->root_depth);
    root_window = screen->root;

    // in XCB we need to manually generate IDs
    xcb_damage_damage_t dmg = xcb_generate_id(xconn);
    //xcb_damage_create(xconn, dmg, root_window, XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
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
        printf("got event %d\n", evt_type);

        if ((evt_type >= g_first_xdamage_event) &&
                (evt_type <= (g_first_xdamage_event + XCB_DAMAGE_NOTIFY))) {
            xcb_damage_notify_event_t *xdevt = (xcb_damage_notify_event_t *)evt;
            printf(" * this is  damage event, tmstamp = %u\n", xdevt->timestamp);
            printf("    damage handle = %u\n", xdevt->damage);
            printf("    area     : (%d,%d) sz (%d, %d)\n",
                   (int)xdevt->area.x,     (int)xdevt->area.y,
                   (int)xdevt->area.width, (int)xdevt->area.height);
            printf("    geometry : (%d,%d) sz (%d, %d)\n",
                   (int)xdevt->geometry.x,     (int)xdevt->geometry.y,
                   (int)xdevt->geometry.width, (int)xdevt->geometry.height);

            //xcb_damage_subtract(xconn, dmg, XCB_NONE, XCB_NONE);
        }
    }
    printf("Event loop ended, cleanup, exit\n");

    xcb_damage_destroy(xconn, dmg);

    xcb_disconnect(xconn);
    return 0;
}
