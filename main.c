#include <stdio.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/damage.h>


xcb_screen_t *screen_of_display (xcb_connection_t *c, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(c));
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

    const xcb_query_extension_reply_t *extension_data = xcb_get_extension_data(
                xconn, &xcb_damage_id);
    if (!extension_data) {
        fprintf(stderr, "Failed to load xdamage ext!\n");
        xcb_disconnect(xconn);
    }

    printf("XDamage extension data: first event code: %d, first err code: %d\n",
           (int)extension_data->first_event,
           (int)extension_data->first_error);

    xcb_damage_query_version_cookie_t cookie_qv = xcb_damage_query_version(
                xconn, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
    xcb_damage_query_version_reply_t *xd_version = xcb_damage_query_version_reply(
                xconn, cookie_qv, NULL);

    if (!xd_version) {
        fprintf(stderr, "Failed to load xdamage ext!\n");
        xcb_disconnect(xconn);
    }

    printf("Loaded XDamage extension version: %d.%d\n",
           xd_version->major_version, xd_version->major_version);

    xcb_window_t root_window = 0;
    xcb_screen_t *screen = screen_of_display(xconn, screen_preferred);
    if (!screen) {
        fprintf(stderr, "Cannot get parameters of the default screen!\n");
        xcb_disconnect(xconn);
    }

    printf("Preferred screen size: %d x %d @ %d BPP\n",
           (int)screen->width_in_pixels, (int)screen->height_in_pixels,
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
        //uint8_t evt_type = evt->response_type & ~0x80; // why? but in tutorial it's so
        /* Strip off the highest bit (set if the event is generated) !!! */
        int evt_type = (int)evt->response_type & 0x7F;
        printf("got event %d\n", evt_type);

        if ((evt_type >= extension_data->first_event) &&
                (evt_type <= (extension_data->first_event + XCB_DAMAGE_NOTIFY))) {
            xcb_damage_notify_event_t *xdevt = (xcb_damage_notify_event_t *)evt;
            printf(" * this is  damage event, tmstamp = %u\n", xdevt->timestamp);
            printf("    damage handle = %u\n", xdevt->damage);
            printf("    area     : (%d,%d) sz (%d, %d)\n",
                   (int)xdevt->area.x,     (int)xdevt->area.y,
                   (int)xdevt->area.width, (int)xdevt->area.height);
            printf("    geometry : (%d,%d) sz (%d, %d)\n",
                   (int)xdevt->geometry.x,     (int)xdevt->geometry.y,
                   (int)xdevt->geometry.width, (int)xdevt->geometry.height);

            xcb_damage_subtract(xconn, dmg, XCB_NONE, XCB_NONE);
        }
    }
    printf("Event loop ended, cleanup, exit\n");

    xcb_damage_destroy(xconn, dmg);

    xcb_disconnect(xconn);
    return 0;
}
