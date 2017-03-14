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

    const xcb_query_extension_reply_t *extension_data = xcb_get_extension_data(xconn, &xcb_damage_id);
    if (!extension_data) {
        fprintf(stderr, "Failed to load xdamage ext!\n");
        xcb_disconnect(xconn);
    }

    printf("XDamage extension data: first event code: %d, first err code: %d\n",
           (int)extension_data->first_event,
           (int)extension_data->first_error);

    xcb_damage_query_version_cookie_t cookie_qv = xcb_damage_query_version(
                xconn, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
    xcb_damage_query_version_reply_t *xd_version = xcb_damage_query_version_reply(xconn, cookie_qv, NULL);

    if (!xd_version) {
        fprintf(stderr, "Failed to load xdamage ext!\n");
        xcb_disconnect(xconn);
    }

    printf("Loaded XDamage extension version: %d.%d\n", xd_version->major_version, xd_version->major_version);

    xcb_window_t root_window = 0;
    xcb_screen_t *screen = screen_of_display(xconn, screen_preferred);
    if (screen) {
        printf("Preferred screen size: %d x %d @ %d BPP\n",
               (int)screen->width_in_pixels, (int)screen->height_in_pixels,
               (int)screen->root_depth);
        root_window = screen->root;
    }

    xcb_disconnect(xconn);
    return 0;
}
