/*
 * KeplerOS on X11: the shell is both the desktop and the window manager.
 *
 * Every window on screen is redirected off-screen (Composite) and the shell
 * draws the whole screen itself into the composite overlay window, exactly as
 * it drew into the framebuffer on bare metal. Other programs' windows (Kepler,
 * the web browser) become ordinary KeplerOS windows whose contents are copied
 * out of X whenever they change (Damage).
 *
 * Input: a full-screen input-only window sits above the programs' windows and
 * catches everything for the shell, except where a program's window is the
 * topmost thing on screen. There its input shape has a hole, so X delivers
 * the clicks and typing straight to that program.
 */

#define _GNU_SOURCE
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include "host.h"
#include "ui.h"

screen_t g_screen;

static Display *dpy;
static Window root, overlay, input_win, check_win;
static GC gc;
static XImage *screen_img;
static XShmSegmentInfo screen_shm;
static int damage_event, damage_error;
static int x_error;

enum {
    A_WM_PROTOCOLS,
    A_WM_DELETE_WINDOW,
    A_WM_TAKE_FOCUS,
    A_WM_STATE,
    A_WM_CHANGE_STATE,
    A_NET_SUPPORTED,
    A_NET_SUPPORTING_WM_CHECK,
    A_NET_WM_NAME,
    A_NET_ACTIVE_WINDOW,
    A_NET_WM_STATE,
    A_NET_WM_STATE_FULLSCREEN,
    A_NET_WM_WINDOW_TYPE,
    A_NET_WM_WINDOW_TYPE_DIALOG,
    A_NET_CLOSE_WINDOW,
    A_NET_CLIENT_LIST,
    A_UTF8_STRING,
    N_ATOMS
};
static const char *atom_names[N_ATOMS] = {
    "WM_PROTOCOLS",        "WM_DELETE_WINDOW",     "WM_TAKE_FOCUS",          "WM_STATE",
    "WM_CHANGE_STATE",     "_NET_SUPPORTED",       "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME",
    "_NET_ACTIVE_WINDOW",  "_NET_WM_STATE",        "_NET_WM_STATE_FULLSCREEN", "_NET_WM_WINDOW_TYPE",
    "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_CLOSE_WINDOW", "_NET_CLIENT_LIST",     "UTF8_STRING",
};
static Atom atoms[N_ATOMS];

static int on_x_error(Display *d, XErrorEvent *e)
{
    (void)d;
    x_error = e->error_code;
    return 0;
}

/* ---- the screen --------------------------------------------------------------------------- */

static bool shm_image(XImage **img, XShmSegmentInfo *shm, Visual *vis, int depth, int w, int h)
{
    *img = XShmCreateImage(dpy, vis, depth, ZPixmap, NULL, shm, w, h);
    if (!*img)
        return false;
    shm->shmid = shmget(IPC_PRIVATE, (size_t)(*img)->bytes_per_line * h, IPC_CREAT | 0600);
    if (shm->shmid < 0) {
        XDestroyImage(*img);
        *img = NULL;
        return false;
    }
    shm->shmaddr = (*img)->data = shmat(shm->shmid, NULL, 0);
    shm->readOnly = False;
    XShmAttach(dpy, shm);
    XSync(dpy, False);
    shmctl(shm->shmid, IPC_RMID, NULL); /* goes away with the last user */
    return true;
}

static void shm_free(XImage **img, XShmSegmentInfo *shm)
{
    if (!*img)
        return;
    XShmDetach(dpy, shm);
    XDestroyImage(*img); /* frees nothing it doesn't own: data is the segment */
    shmdt(shm->shmaddr);
    *img = NULL;
}

void video_flush(rect_t r)
{
    r = rect_intersect(r, rect_make(0, 0, g_screen.w, g_screen.h));
    if (rect_empty(r) || !screen_img)
        return;
    XShmPutImage(dpy, overlay, gc, screen_img, r.x, r.y, r.x, r.y, r.w, r.h, False);
    XSync(dpy, False); /* the shell draws into the same memory next */
}

gfx_t video_gfx(void) { return gfx_for_buffer(g_screen.back, g_screen.w, g_screen.h, g_screen.w); }

/* ---- input queue ---------------------------------------------------------------------------- */

static input_event_t queue[512];
static int q_head, q_tail;
static int qx, qy;          /* where the shell will think the pointer is once the queue drains */
static uint8_t q_buttons, q_mods;

static void push(const input_event_t *ev)
{
    int next = (q_head + 1) % ARRAY_LEN(queue);
    if (next == q_tail)
        return;
    queue[q_head] = *ev;
    q_head = next;
}

bool input_poll(input_event_t *ev)
{
    if (q_tail == q_head)
        return false;
    *ev = queue[q_tail];
    q_tail = (q_tail + 1) % ARRAY_LEN(queue);
    return true;
}

bool input_pending(void) { return q_tail != q_head; }
uint8_t keyboard_mods(void) { return q_mods; }

static void push_motion(int x, int y)
{
    x = CLAMP(x, 0, g_screen.w - 1);
    y = CLAMP(y, 0, g_screen.h - 1);
    if (x == qx && y == qy)
        return;
    input_event_t ev = { 0 };
    ev.type = EV_MOUSE;
    ev.dx = (int16_t)(x - qx);
    ev.dy = (int16_t)(y - qy);
    ev.buttons = q_buttons;
    qx = x;
    qy = y;
    push(&ev);
}

static void push_button(int x, int y, int xbutton, bool down)
{
    push_motion(x, y);
    input_event_t ev = { 0 };
    ev.type = EV_MOUSE;
    if (xbutton == 4 || xbutton == 5) {
        if (!down)
            return;
        ev.wheel = xbutton == 4 ? -1 : 1;
        ev.buttons = q_buttons;
        push(&ev);
        return;
    }
    int b = xbutton == 1 ? MOUSE_LEFT : xbutton == 3 ? MOUSE_RIGHT : xbutton == 2 ? MOUSE_MIDDLE : 0;
    if (!b)
        return;
    if (down)
        q_buttons |= b;
    else
        q_buttons &= ~b;
    ev.buttons = q_buttons;
    push(&ev);
}

static uint8_t mods_from_state(unsigned state)
{
    uint8_t m = 0;
    if (state & ShiftMask)
        m |= MOD_SHIFT;
    if (state & ControlMask)
        m |= MOD_CTRL;
    if (state & Mod1Mask)
        m |= MOD_ALT;
    return m;
}

static int map_keysym(KeySym ks)
{
    switch (ks) {
    case XK_BackSpace: return KEY_BACKSPACE;
    case XK_Tab:
    case XK_ISO_Left_Tab: return KEY_TAB;
    case XK_Return:
    case XK_KP_Enter: return KEY_ENTER;
    case XK_Escape: return KEY_ESC;
    case XK_Up:
    case XK_KP_Up: return KEY_UP;
    case XK_Down:
    case XK_KP_Down: return KEY_DOWN;
    case XK_Left:
    case XK_KP_Left: return KEY_LEFT;
    case XK_Right:
    case XK_KP_Right: return KEY_RIGHT;
    case XK_Home:
    case XK_KP_Home: return KEY_HOME;
    case XK_End:
    case XK_KP_End: return KEY_END;
    case XK_Prior:
    case XK_KP_Prior: return KEY_PGUP;
    case XK_Next:
    case XK_KP_Next: return KEY_PGDN;
    case XK_Insert: return KEY_INSERT;
    case XK_Delete:
    case XK_KP_Delete: return KEY_DELETE;
    case XK_Super_L:
    case XK_Super_R: return KEY_LWIN;
    case XK_Menu: return KEY_MENU;
    case XK_Shift_L:
    case XK_Shift_R: return KEY_SHIFT;
    case XK_Control_L:
    case XK_Control_R: return KEY_CTRL;
    case XK_Alt_L:
    case XK_Alt_R: return KEY_ALT;
    case XK_Caps_Lock: return KEY_CAPS;
    }
    if (ks >= XK_F1 && ks <= XK_F12)
        return KEY_F1 + (int)(ks - XK_F1);
    return 0;
}

static void push_key(XKeyEvent *e, bool down)
{
    /* look the character up without Ctrl/Alt so Ctrl+S arrives as 's' */
    XKeyEvent plain = *e;
    plain.state &= ~(ControlMask | Mod1Mask);
    char buf[8] = { 0 };
    KeySym ks = 0;
    int n = XLookupString(&plain, buf, sizeof buf, &ks, NULL);
    int key = map_keysym(ks);
    if (!key && n == 1 && (uint8_t)buf[0] >= 32 && (uint8_t)buf[0] < 127)
        key = (uint8_t)buf[0];
    if (!key && ks >= 32 && ks < 127)
        key = (int)ks;
    q_mods = mods_from_state(e->state);
    /* the state is from before this key changed */
    if (key == KEY_SHIFT)
        q_mods = down ? (q_mods | MOD_SHIFT) : (q_mods & ~MOD_SHIFT);
    else if (key == KEY_CTRL)
        q_mods = down ? (q_mods | MOD_CTRL) : (q_mods & ~MOD_CTRL);
    else if (key == KEY_ALT)
        q_mods = down ? (q_mods | MOD_ALT) : (q_mods & ~MOD_ALT);
    if (!key)
        return;
    input_event_t ev = { 0 };
    ev.type = EV_KEY;
    ev.pressed = down;
    ev.mods = q_mods;
    ev.key = (uint16_t)key;
    push(&ev);
}

/* ---- pointers ------------------------------------------------------------------------------------- */

#define N_CURSORS (CURSOR_HAND + 1)
static Cursor cursors[N_CURSORS], blank_cursor;
static int cur_cursor = -2;

static void make_cursors(void)
{
    for (int c = 0; c < N_CURSORS; c++) {
        rect_t b = cursor_bounds(c, 100, 100);
        int w = b.w + 2, h = b.h + 2;
        image_t *img = image_new(w, h);
        memset(img->px, 0, (size_t)w * h * 4);
        gfx_t g = gfx_for_image(img);
        cursor_draw(&g, c, 100 - b.x + 1, 100 - b.y + 1);
        XcursorImage *xi = XcursorImageCreate(w, h);
        xi->xhot = 100 - b.x + 1;
        xi->yhot = 100 - b.y + 1;
        for (int i = 0; i < w * h; i++) {
            uint32_t p = img->px[i], a = p >> 24;
            uint32_t r = ((p >> 16) & 255) * a / 255, gr = ((p >> 8) & 255) * a / 255, bl = (p & 255) * a / 255;
            xi->pixels[i] = a << 24 | r << 16 | gr << 8 | bl; /* premultiplied */
        }
        cursors[c] = XcursorImageLoadCursor(dpy, xi);
        XcursorImageDestroy(xi);
        image_free(img);
    }
    XcursorImage *xi = XcursorImageCreate(1, 1);
    xi->pixels[0] = 0;
    blank_cursor = XcursorImageLoadCursor(dpy, xi);
    XcursorImageDestroy(xi);
}

void host_set_cursor(int c)
{
    if (c == cur_cursor)
        return;
    cur_cursor = c;
    XDefineCursor(dpy, input_win, c >= 0 && c < N_CURSORS ? cursors[c] : blank_cursor);
}

void host_beep(void) { XBell(dpy, 0); }

/* ---- other programs' windows ------------------------------------------------------------------------ */

typedef struct client {
    Window xw;
    window_t *w; /* NULL for popups */
    bool popup;  /* override-redirect: menus, tooltips, drop-downs */
    int x, y, width, height;
    int depth;
    Visual *visual;
    Damage damage;
    XImage *img;
    XShmSegmentInfo shm;
    bool damaged;
    bool proto_delete, proto_focus;
    struct client *next;
} client_t;

static client_t *clients; /* popups are kept in the order they appeared: bottom first */

static client_t *find_client(Window xw)
{
    for (client_t *c = clients; c; c = c->next)
        if (c->xw == xw)
            return c;
    return NULL;
}

static void fetch(client_t *c)
{
    c->damaged = false;
    if (c->width <= 0 || c->height <= 0)
        return;
    if (c->img && (c->img->width != c->width || c->img->height != c->height))
        shm_free(&c->img, &c->shm);
    if (!c->img && !shm_image(&c->img, &c->shm, c->visual, c->depth, c->width, c->height))
        return;
    x_error = 0;
    if (!XShmGetImage(dpy, c->xw, c->img, 0, 0, AllPlanes) || x_error)
        return;
    if (c->depth != 32) {
        uint32_t *p = (uint32_t *)c->img->data;
        size_t n = (size_t)c->img->bytes_per_line / 4 * c->img->height;
        for (size_t i = 0; i < n; i++)
            p[i] |= 0xFF000000u;
    }
}

static void client_image(const client_t *c, image_t *out)
{
    out->px = (uint32_t *)c->img->data;
    out->w = c->img->bytes_per_line / 4;
    out->h = c->img->height;
}

static void cl_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    client_t *c = w->data;
    if (!c || !c->img)
        return;
    image_t im;
    client_image(c, &im);
    gfx_blit_region(g, 0, 0, &im, 0, 0, MIN(cw, c->img->width), MIN(ch, c->img->height));
}

static void send_protocol(client_t *c, Atom proto)
{
    XEvent ev = { 0 };
    ev.xclient.type = ClientMessage;
    ev.xclient.window = c->xw;
    ev.xclient.message_type = atoms[A_WM_PROTOCOLS];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)proto;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->xw, False, NoEventMask, &ev);
}

static void cl_close_request(window_t *w)
{
    client_t *c = w->data;
    if (!c)
        return;
    if (c->proto_delete)
        send_protocol(c, atoms[A_WM_DELETE_WINDOW]);
    else
        XKillClient(dpy, c->xw);
}

static void cl_close(window_t *w) { w->data = NULL; }

static const window_ops_t client_ops = { cl_paint, NULL, NULL, NULL, cl_close, NULL, NULL, cl_close_request };

static void read_protocols(client_t *c)
{
    Atom *protos;
    int n;
    c->proto_delete = c->proto_focus = false;
    if (XGetWMProtocols(dpy, c->xw, &protos, &n)) {
        for (int i = 0; i < n; i++) {
            if (protos[i] == atoms[A_WM_DELETE_WINDOW])
                c->proto_delete = true;
            if (protos[i] == atoms[A_WM_TAKE_FOCUS])
                c->proto_focus = true;
        }
        XFree(protos);
    }
}

static void read_title(client_t *c, char *out, int n)
{
    out[0] = 0;
    Atom type;
    int format;
    unsigned long items, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, c->xw, atoms[A_NET_WM_NAME], 0, 256, False, atoms[A_UTF8_STRING], &type, &format, &items, &after,
                           &data) == Success && data) {
        /* the shell's fonts are ASCII: keep what they can show */
        int k = 0;
        for (unsigned long i = 0; i < items && k < n - 1; i++)
            if (data[i] >= 32 && data[i] < 127)
                out[k++] = (char)data[i];
            else if (data[i] >= 0xC0 && k < n - 1)
                out[k++] = '-';
        out[k] = 0;
        XFree(data);
    }
    if (!out[0]) {
        char *name = NULL;
        if (XFetchName(dpy, c->xw, &name) && name) {
            strlcpy(out, name, n);
            XFree(name);
        }
    }
}

static void read_min_size(client_t *c)
{
    XSizeHints hints;
    long supplied;
    if (!c->w || !XGetWMNormalHints(dpy, c->xw, &hints, &supplied))
        return;
    if (hints.flags & PMinSize) {
        c->w->min_w = MAX(160, hints.min_width);
        c->w->min_h = MAX(100, hints.min_height + TITLE_H);
    }
}

static bool has_atom(Window xw, Atom prop, Atom value)
{
    Atom type;
    int format;
    unsigned long items, after;
    unsigned char *data = NULL;
    bool found = false;
    if (XGetWindowProperty(dpy, xw, prop, 0, 32, False, XA_ATOM, &type, &format, &items, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < items; i++)
            if (((Atom *)data)[i] == value)
                found = true;
        XFree(data);
    }
    return found;
}

static void set_wm_state(Window xw, long state)
{
    long data[2] = { state, None };
    XChangeProperty(dpy, xw, atoms[A_WM_STATE], atoms[A_WM_STATE], 32, PropModeReplace, (unsigned char *)data, 2);
}

static void set_fullscreen_prop(client_t *c, bool on)
{
    if (on)
        XChangeProperty(dpy, c->xw, atoms[A_NET_WM_STATE], XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&atoms[A_NET_WM_STATE_FULLSCREEN], 1);
    else
        XDeleteProperty(dpy, c->xw, atoms[A_NET_WM_STATE]);
}

static void update_client_list(void)
{
    Window list[64];
    int n = 0;
    for (client_t *c = clients; c && n < 64; c = c->next)
        if (!c->popup)
            list[n++] = c->xw;
    XChangeProperty(dpy, root, atoms[A_NET_CLIENT_LIST], XA_WINDOW, 32, PropModeReplace, (unsigned char *)list, n);
}

static client_t *new_client(Window xw, XWindowAttributes *wa, bool popup)
{
    client_t *c = kzalloc(sizeof *c);
    c->xw = xw;
    c->popup = popup;
    c->x = wa->x;
    c->y = wa->y;
    c->width = wa->width;
    c->height = wa->height;
    c->depth = wa->depth;
    c->visual = wa->visual;
    c->damage = XDamageCreate(dpy, xw, XDamageReportNonEmpty);
    c->damaged = true;
    /* popups stack in the order they appear; managed windows' order is the shell's */
    client_t **pp = &clients;
    while (*pp)
        pp = &(*pp)->next;
    *pp = c;
    return c;
}

static void free_client(client_t *c, bool destroyed)
{
    client_t **pp = &clients;
    while (*pp && *pp != c)
        pp = &(*pp)->next;
    if (*pp)
        *pp = c->next;
    if (!destroyed) {
        x_error = 0;
        XDamageDestroy(dpy, c->damage);
    }
    shm_free(&c->img, &c->shm);
    if (c->w) {
        window_t *w = c->w;
        c->w = NULL;
        w->data = NULL;
        w->closing = true;
        wm_close(w);
    } else if (c->popup) {
        ui_invalidate(rect_make(c->x, c->y, c->width, c->height));
    }
    kfree(c);
}

static void manage(Window xw)
{
    XWindowAttributes wa;
    x_error = 0;
    if (!XGetWindowAttributes(dpy, xw, &wa) || x_error || wa.override_redirect || wa.class == InputOnly)
        return;
    client_t *c = find_client(xw);
    if (c) {
        XMapWindow(dpy, xw);
        return;
    }
    c = new_client(xw, &wa, false);
    read_protocols(c);

    char title[96];
    read_title(c, title, sizeof title);
    XClassHint cls = { 0 };
    char app[32] = "";
    if (XGetClassHint(dpy, xw, &cls)) {
        if (cls.res_class)
            strlcpy(app, cls.res_class, sizeof app);
        XFree(cls.res_name);
        XFree(cls.res_class);
    }
    if (!app[0])
        strlcpy(app, title[0] ? title : "Application", sizeof app);
    if (!title[0])
        strlcpy(title, app, sizeof title);

    const app_info_t *info = app_find(app);
    Window owner_x = None;
    bool dialog = XGetTransientForHint(dpy, xw, &owner_x) ||
                  has_atom(xw, atoms[A_NET_WM_WINDOW_TYPE], atoms[A_NET_WM_WINDOW_TYPE_DIALOG]);

    rect_t a = ui_work_area();
    int cw = MIN(wa.width, a.w), ch = MIN(wa.height, a.h - TITLE_H);
    window_t *w = wm_create(title, info ? info->icon : ICON_ALLPROGS, cw, ch, &client_ops, c,
                            WF_RESIZABLE | (dialog ? WF_NO_MAX : 0));
    c->w = w;
    w->bg = HEX(0xFFFFFF);
    w->min_w = 160;
    w->min_h = 100;
    wm_set_app(w, app, NULL, NULL, NULL);
    read_min_size(c);
    if (dialog || cw > a.w * 3 / 4)
        wm_center(w);

    XSelectInput(dpy, xw, PropertyChangeMask | StructureNotifyMask);
    XGrabButton(dpy, AnyButton, AnyModifier, xw, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    set_wm_state(xw, NormalState);
    rect_t r = wm_client_rect(w);
    c->x = r.x;
    c->y = r.y;
    c->width = r.w;
    c->height = r.h;
    XMoveResizeWindow(dpy, xw, r.x, r.y, r.w, r.h);
    XMapWindow(dpy, xw);
    if (has_atom(xw, atoms[A_NET_WM_STATE], atoms[A_NET_WM_STATE_FULLSCREEN]))
        wm_set_fullscreen(w, true);
    update_client_list();
    kprintf("x11: managing \"%s\" (%s)\n", title, app);
}

static void track_popup(Window xw)
{
    if (xw == input_win || xw == overlay || find_client(xw))
        return;
    XWindowAttributes wa;
    x_error = 0;
    if (!XGetWindowAttributes(dpy, xw, &wa) || x_error || wa.class == InputOnly || wa.map_state != IsViewable)
        return;
    client_t *c = new_client(xw, &wa, true);
    ui_invalidate(rect_make(c->x, c->y, c->width, c->height));
}

void host_paint_popups(gfx_t *g, rect_t dirty)
{
    if (!ui_desktop_shown())
        return;
    for (client_t *c = clients; c; c = c->next) {
        if (!c->popup || !c->img)
            continue;
        rect_t r = rect_make(c->x, c->y, c->width, c->height);
        if (rect_empty(rect_intersect(r, dirty)))
            continue;
        image_t im;
        client_image(c, &im);
        gfx_t pg = gfx_sub(g, c->x, c->y, c->width, c->height);
        if (c->depth == 32) {
            image_t vis = { im.px, im.w, MIN(im.h, c->height) };
            gfx_blit_alpha(&pg, 0, 0, &vis, 255);
        } else {
            gfx_blit_region(&pg, 0, 0, &im, 0, 0, MIN(c->width, c->img->width), MIN(c->height, c->img->height));
        }
    }
}

/* ---- X events ----------------------------------------------------------------------------------------- */

static void on_configure_request(XConfigureRequestEvent *e)
{
    client_t *c = find_client(e->window);
    if (c && c->w) {
        int cw = (e->value_mask & CWWidth) ? e->width : c->width;
        int ch = (e->value_mask & CWHeight) ? e->height : c->height;
        if (!(c->w->flags & WF_FULLSCREEN) && (cw != c->width || ch != c->height)) {
            rect_t a = ui_work_area();
            wm_set_client_size(c->w, MIN(cw, a.w), MIN(ch, a.h - TITLE_H));
        }
        /* tell it where it really is */
        XConfigureEvent ce = { 0 };
        ce.type = ConfigureNotify;
        ce.event = ce.window = c->xw;
        ce.x = c->x;
        ce.y = c->y;
        ce.width = c->width;
        ce.height = c->height;
        XSendEvent(dpy, c->xw, False, StructureNotifyMask, (XEvent *)&ce);
        return;
    }
    XWindowChanges wc = { e->x, e->y, e->width, e->height, e->border_width, e->above, e->detail };
    XConfigureWindow(dpy, e->window, e->value_mask, &wc);
}

static void on_client_message(XClientMessageEvent *e)
{
    client_t *c = find_client(e->window);
    if (!c || !c->w)
        return;
    if (e->message_type == atoms[A_NET_WM_STATE]) {
        Atom fs = atoms[A_NET_WM_STATE_FULLSCREEN];
        if ((Atom)e->data.l[1] == fs || (Atom)e->data.l[2] == fs) {
            bool now = c->w->flags & WF_FULLSCREEN;
            bool on = e->data.l[0] == 1 || (e->data.l[0] == 2 && !now);
            wm_set_fullscreen(c->w, on);
            set_fullscreen_prop(c, on);
        }
    } else if (e->message_type == atoms[A_NET_ACTIVE_WINDOW]) {
        wm_focus(c->w);
    } else if (e->message_type == atoms[A_WM_CHANGE_STATE] && e->data.l[0] == IconicState) {
        wm_minimize(c->w);
    } else if (e->message_type == atoms[A_NET_CLOSE_WINDOW]) {
        cl_close_request(c->w);
    }
}

static void on_property(XPropertyEvent *e)
{
    client_t *c = find_client(e->window);
    if (!c || !c->w)
        return;
    if (e->atom == XA_WM_NAME || e->atom == atoms[A_NET_WM_NAME]) {
        char title[96];
        read_title(c, title, sizeof title);
        if (title[0] && strcmp(title, c->w->title))
            wm_set_title(c->w, title);
    } else if (e->atom == XA_WM_NORMAL_HINTS) {
        read_min_size(c);
    } else if (e->atom == atoms[A_WM_PROTOCOLS]) {
        read_protocols(c);
    }
}

static void handle(XEvent *ev)
{
    switch (ev->type) {
    case MapRequest:
        manage(ev->xmaprequest.window);
        break;
    case MapNotify:
        if (ev->xmap.event == root && ev->xmap.override_redirect)
            track_popup(ev->xmap.window);
        break;
    case UnmapNotify:
        if (ev->xunmap.event == root) {
            client_t *c = find_client(ev->xunmap.window);
            if (c) {
                if (c->w)
                    set_wm_state(c->xw, WithdrawnState);
                free_client(c, false);
                update_client_list();
            }
        }
        break;
    case DestroyNotify:
        if (ev->xdestroywindow.event == root) {
            client_t *c = find_client(ev->xdestroywindow.window);
            if (c) {
                free_client(c, true);
                update_client_list();
            }
        }
        break;
    case ConfigureNotify:
        if (ev->xconfigure.event == root) {
            client_t *c = find_client(ev->xconfigure.window);
            if (c && c->popup) {
                ui_invalidate(rect_make(c->x, c->y, c->width, c->height));
                c->x = ev->xconfigure.x;
                c->y = ev->xconfigure.y;
                c->width = ev->xconfigure.width;
                c->height = ev->xconfigure.height;
                c->damaged = true;
            }
        }
        break;
    case ConfigureRequest:
        on_configure_request(&ev->xconfigurerequest);
        break;
    case ClientMessage:
        on_client_message(&ev->xclient);
        break;
    case PropertyNotify:
        on_property(&ev->xproperty);
        break;
    case ButtonPress:
        if (ev->xbutton.window != input_win && ev->xbutton.window != root) {
            /* a click into a program's window: bring it forward, then let the click through */
            client_t *c = find_client(ev->xbutton.window);
            if (c && c->w && wm_active() != c->w)
                wm_focus(c->w);
            XAllowEvents(dpy, ReplayPointer, ev->xbutton.time);
            break;
        }
        push_button(ev->xbutton.x_root, ev->xbutton.y_root, ev->xbutton.button, true);
        break;
    case ButtonRelease:
        push_button(ev->xbutton.x_root, ev->xbutton.y_root, ev->xbutton.button, false);
        break;
    case MotionNotify: {
        XEvent next;
        while (XCheckTypedWindowEvent(dpy, ev->xmotion.window, MotionNotify, &next))
            *ev = next;
        push_motion(ev->xmotion.x_root, ev->xmotion.y_root);
        break;
    }
    case EnterNotify:
    case LeaveNotify:
        /* leaving into a program's window: let hover effects settle where the pointer went */
        push_motion(ev->xcrossing.x_root, ev->xcrossing.y_root);
        break;
    case KeyPress:
    case KeyRelease:
        push_key(&ev->xkey, ev->type == KeyPress);
        break;
    default:
        if (ev->type == damage_event + XDamageNotify) {
            XDamageNotifyEvent *de = (XDamageNotifyEvent *)ev;
            XDamageSubtract(dpy, de->damage, None, None);
            client_t *c = find_client(de->drawable);
            if (c)
                c->damaged = true;
        }
        break;
    }
}

void host_pump(void)
{
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        handle(&ev);
    }
    for (client_t *c = clients; c; c = c->next) {
        if (!c->damaged)
            continue;
        fetch(c);
        if (c->w && !c->w->dead)
            wm_invalidate_client(c->w);
        else if (c->popup)
            ui_invalidate(rect_make(c->x, c->y, c->width, c->height));
    }
}

/* ---- keeping X in step with the shell ------------------------------------------------------------------- */

static Window focused = None;
static Window stack_applied[64];
static int n_stack_applied;
static uint32_t holes_sig = 1;

static bool shown(const window_t *w) { return w && !w->dead && !(w->flags & WF_MINIMIZED); }

static uint32_t mix(uint32_t h, int v) { return (h ^ (uint32_t)v) * 16777619u; }

static void update_holes(void)
{
    bool desk = ui_desktop_shown();
    rect_t surf[8];
    int n = 0;
    if (desk) {
        if (!wm_fullscreen())
            surf[n++] = dock_rect();
        n += menu_rects(surf + n, 4);
        if (spotlight_is_open())
            surf[n++] = spotlight_rect();
    }
    uint32_t sig = mix(2166136261u, desk);
    for (window_t *w = wm_first(); w && desk; w = w->next) {
        if (!shown(w))
            continue;
        sig = mix(mix(mix(mix(mix(sig, w->x), w->y), w->w), w->h), w->ops == &client_ops);
    }
    for (int i = 0; i < n; i++)
        sig = mix(mix(mix(mix(sig, surf[i].x), surf[i].y), surf[i].w), surf[i].h);
    if (sig == holes_sig)
        return;
    holes_sig = sig;

    XRectangle full = { 0, 0, (unsigned short)g_screen.w, (unsigned short)g_screen.h };
    XserverRegion shape = XFixesCreateRegion(dpy, &full, 1);
    if (desk) {
        XserverRegion holes = XFixesCreateRegion(dpy, NULL, 0);
        for (window_t *w = wm_first(); w; w = w->next) {
            if (!shown(w))
                continue;
            XRectangle r;
            if (w->ops == &client_ops) {
                rect_t c = wm_client_rect(w);
                r = (XRectangle){ (short)c.x, (short)c.y, (unsigned short)c.w, (unsigned short)c.h };
                XserverRegion one = XFixesCreateRegion(dpy, &r, 1);
                XFixesUnionRegion(dpy, holes, holes, one);
                XFixesDestroyRegion(dpy, one);
            } else {
                r = (XRectangle){ (short)w->x, (short)w->y, (unsigned short)w->w, (unsigned short)w->h };
                XserverRegion one = XFixesCreateRegion(dpy, &r, 1);
                XFixesSubtractRegion(dpy, holes, holes, one);
                XFixesDestroyRegion(dpy, one);
            }
        }
        for (int i = 0; i < n; i++) {
            XRectangle r = { (short)surf[i].x, (short)surf[i].y, (unsigned short)surf[i].w, (unsigned short)surf[i].h };
            XserverRegion one = XFixesCreateRegion(dpy, &r, 1);
            XFixesSubtractRegion(dpy, holes, holes, one);
            XFixesDestroyRegion(dpy, one);
        }
        XFixesSubtractRegion(dpy, shape, shape, holes);
        XFixesDestroyRegion(dpy, holes);
    }
    XFixesSetWindowShapeRegion(dpy, input_win, ShapeInput, 0, 0, shape);
    XFixesDestroyRegion(dpy, shape);
}

void host_sync(void)
{
    /* programs' windows go where the shell has them */
    for (client_t *c = clients; c; c = c->next) {
        if (!c->w || c->w->dead)
            continue;
        rect_t r = wm_client_rect(c->w);
        if (r.x != c->x || r.y != c->y || r.w != c->width || r.h != c->height) {
            bool resized = r.w != c->width || r.h != c->height;
            c->x = r.x;
            c->y = r.y;
            c->width = MAX(1, r.w);
            c->height = MAX(1, r.h);
            XMoveResizeWindow(dpy, c->xw, c->x, c->y, c->width, c->height);
            if (resized)
                c->damaged = true;
        }
    }

    /* stacking: the input window on top, then the programs in the shell's order */
    Window order[64];
    int n = 0;
    order[n++] = input_win;
    window_t *ws[64];
    int nw = 0;
    for (window_t *w = wm_first(); w && nw < 63; w = w->next)
        if (!w->dead && w->ops == &client_ops && w->data)
            ws[nw++] = w;
    for (int i = nw - 1; i >= 0; i--)
        order[n++] = ((client_t *)ws[i]->data)->xw;
    if (n != n_stack_applied || memcmp(order, stack_applied, n * sizeof(Window))) {
        XRestackWindows(dpy, order, n);
        memcpy(stack_applied, order, n * sizeof(Window));
        n_stack_applied = n;
    }

    /* the keyboard */
    window_t *a = wm_active();
    Window want = input_win;
    client_t *ac = NULL;
    if (!ui_wants_keyboard() && shown(a) && a->ops == &client_ops && a->data) {
        ac = a->data;
        want = ac->xw;
    }
    if (want != focused) {
        focused = want;
        x_error = 0;
        XSetInputFocus(dpy, want, RevertToPointerRoot, CurrentTime);
        if (ac && ac->proto_focus)
            send_protocol(ac, atoms[A_WM_TAKE_FOCUS]);
        Window act = ac ? ac->xw : None;
        XChangeProperty(dpy, root, atoms[A_NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&act, 1);
    }

    update_holes();
    XFlush(dpy);
}

void host_wait(int ms)
{
    XFlush(dpy);
    if (XPending(dpy) || input_pending())
        return;
    struct pollfd p = { ConnectionNumber(dpy), POLLIN, 0 };
    poll(&p, 1, MAX(0, ms));
}

/* ---- starting programs ------------------------------------------------------------------------------------ */

void host_launch(const char *program, const char *arg)
{
    pid_t pid = fork();
    if (pid != 0)
        return;
    setsid();
    char path[64];
    ksnprintf(path, sizeof path, "%s", program);
    if (arg && *arg)
        execlp(path, path, arg, (char *)NULL);
    else
        execlp(path, path, (char *)NULL);
    _exit(127);
}

/* ---- start-up ---------------------------------------------------------------------------------------------- */

static void grab_key(KeySym ks, unsigned mods)
{
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (!kc)
        return;
    static const unsigned extra[4] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
    for (int i = 0; i < 4; i++)
        XGrabKey(dpy, kc, mods | extra[i], root, False, GrabModeAsync, GrabModeAsync);
}

bool host_x11_init(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        kprintf("x11: can't open the display\n");
        return false;
    }
    int scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    XSetErrorHandler(on_x_error);
    for (int i = 0; i < N_ATOMS; i++)
        atoms[i] = XInternAtom(dpy, atom_names[i], False);

    int ev, err, major = 0, minor = 2;
    if (!XShmQueryExtension(dpy) || !XCompositeQueryExtension(dpy, &ev, &err) ||
        !XCompositeQueryVersion(dpy, &major, &minor) || (major == 0 && minor < 3) ||
        !XDamageQueryExtension(dpy, &damage_event, &damage_error) || !XFixesQueryExtension(dpy, &ev, &err)) {
        kprintf("x11: the X server lacks MIT-SHM, Composite, Damage or XFixes\n");
        return false;
    }
    int fmaj = 2, fmin = 0;
    XFixesQueryVersion(dpy, &fmaj, &fmin);

    /* become the window manager */
    x_error = 0;
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | PropertyChangeMask);
    XSync(dpy, False);
    if (x_error) {
        kprintf("x11: another window manager is running\n");
        return false;
    }
    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
    overlay = XCompositeGetOverlayWindow(dpy, root);
    XserverRegion none = XFixesCreateRegion(dpy, NULL, 0);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeInput, 0, 0, none);
    XFixesDestroyRegion(dpy, none);

    g_screen.w = DisplayWidth(dpy, scr);
    g_screen.h = DisplayHeight(dpy, scr);
    if (!shm_image(&screen_img, &screen_shm, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr), g_screen.w, g_screen.h) ||
        screen_img->bits_per_pixel != 32 || screen_img->bytes_per_line != g_screen.w * 4) {
        kprintf("x11: need a 32-bit TrueColor screen\n");
        return false;
    }
    g_screen.back = (uint32_t *)screen_img->data;
    g_screen.fb = NULL;
    g_screen.pitch = g_screen.w;
    memset32(g_screen.back, 0xFF000000, (size_t)g_screen.w * g_screen.h);
    g_sys.screen_w = g_screen.w;
    g_sys.screen_h = g_screen.h;
    ksnprintf(g_sys.video, sizeof g_sys.video, "X11 display, %s", ServerVendor(dpy));
    gc = XCreateGC(dpy, overlay, 0, NULL);

    XSetWindowAttributes swa = { 0 };
    swa.override_redirect = True;
    swa.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask |
                     KeyPressMask | KeyReleaseMask;
    input_win = XCreateWindow(dpy, root, 0, 0, g_screen.w, g_screen.h, 0, 0, InputOnly, CopyFromParent,
                              CWOverrideRedirect | CWEventMask, &swa);
    XMapRaised(dpy, input_win);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | PropertyChangeMask | KeyPressMask |
                                KeyReleaseMask);
    qx = g_screen.w / 2;
    qy = g_screen.h / 2;
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, qx, qy);

    /* tell programs a window manager is here (EWMH) */
    check_win = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, root, atoms[A_NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, check_win, atoms[A_NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, check_win, atoms[A_NET_WM_NAME], atoms[A_UTF8_STRING], 8, PropModeReplace,
                    (unsigned char *)"KeplerOS", 8);
    Atom supported[] = { atoms[A_NET_SUPPORTED],    atoms[A_NET_SUPPORTING_WM_CHECK], atoms[A_NET_WM_NAME],
                         atoms[A_NET_ACTIVE_WINDOW], atoms[A_NET_WM_STATE],           atoms[A_NET_WM_STATE_FULLSCREEN],
                         atoms[A_NET_CLOSE_WINDOW],  atoms[A_NET_CLIENT_LIST],        atoms[A_NET_WM_WINDOW_TYPE] };
    XChangeProperty(dpy, root, atoms[A_NET_SUPPORTED], XA_ATOM, 32, PropModeReplace, (unsigned char *)supported,
                    ARRAY_LEN(supported));

    make_cursors();
    XDefineCursor(dpy, root, cursors[CURSOR_ARROW]);
    host_set_cursor(CURSOR_ARROW);

    /* the Windows key and the shell's shortcuts work whichever program has the keyboard */
    grab_key(XK_Super_L, AnyModifier);
    grab_key(XK_Super_R, AnyModifier);
    grab_key(XK_Tab, Mod1Mask);
    grab_key(XK_F4, Mod1Mask);
    grab_key(XK_Delete, ControlMask | Mod1Mask);

    signal(SIGCHLD, SIG_IGN); /* started programs are reaped by themselves */

    /* windows that were up before we started */
    Window r_, p_, *kids = NULL;
    unsigned nk = 0;
    if (XQueryTree(dpy, root, &r_, &p_, &kids, &nk)) {
        for (unsigned i = 0; i < nk; i++) {
            XWindowAttributes wa;
            if (kids[i] != input_win && kids[i] != check_win && XGetWindowAttributes(dpy, kids[i], &wa) &&
                wa.map_state == IsViewable && !wa.override_redirect)
                manage(kids[i]);
        }
        XFree(kids);
    }
    XSync(dpy, False);
    kprintf("x11: window manager up on a %dx%d screen\n", g_screen.w, g_screen.h);
    return true;
}
