/* mc-account.c — the account box at the bottom of the sidebar. Clicking it
 * opens a small menu: the UUID, the arm model Mojang has for your skin (and a
 * fix when it doesn't match the picture), signing in again, Prism's account
 * manager and your NameMC page. Accounts themselves stay Prism's: tux-mc never
 * writes accounts.json. */
#include "mc.h"

#include <glib/gstdio.h>
#include <string.h>

static void on_copy(GtkButton *b, gpointer data)
{
    app_t *a = data;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(b)), a->uuid ? a->uuid : "");
    mc_status(a, "UUID copied");
}

/* the picture says slim, the account says classic (or the other way): upload
 * the same picture again with the model it was drawn for */
static void on_fix_model(GtkButton *b, gpointer data)
{
    app_t *a = data;
    if (!a->skin)
        return;
    char *dir = g_build_filename(g_get_user_cache_dir(), "tux-mc", NULL);
    g_mkdir_with_parents(dir, 0755);
    char *p = g_build_filename(dir, "skin-fix.png", NULL);
    if (cairo_surface_write_to_png(a->skin, p) == CAIRO_STATUS_SUCCESS)
        mc_skin_upload(a, p, mc3d_skin_is_slim(a->skin));
    else
        mc_status(a, "Couldn't save the skin to upload it");
    g_free(p);
    g_free(dir);
    gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "pop")));
}

static void relog_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    char *err = NULL, *tok = mc_auth_token(data, &err);
    if (tok)
        g_task_return_boolean(t, TRUE);
    else
        g_task_return_new_error(t, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", err ? err : "no answer");
    g_free(tok);
    g_free(err);
}

static void relog_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    GError *e = NULL;
    if (g_task_propagate_boolean(G_TASK(r), &e))
        mc_status(data, "Signed in again: skin and cape changes work");
    else {
        mc_status(data, "Signing in failed: %s. Prism can sign you in again (Accounts in Prism).", e->message);
        g_error_free(e);
    }
}

static void on_relog(GtkButton *b, gpointer data)
{
    app_t *a = data;
    gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "pop")));
    mc_status(a, "Signing in…");
    GTask *t = g_task_new(NULL, NULL, relog_done, a);
    g_task_set_task_data(t, a, NULL);
    g_task_run_in_thread(t, relog_thread);
    g_object_unref(t);
}

static void on_prism(GtkButton *b, gpointer data)
{
    app_t *a = data;
    gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "pop")));
    const char *args[] = {"--show-window", NULL};
    GError *e = NULL;
    if (mc_sys_run_prism(args, &e))
        mc_status(a, "In Prism: Accounts (top right) → Manage Accounts. Reopen tux-mc after a switch.");
    else {
        mc_status(a, "Couldn't open Prism: %s", e->message);
        g_error_free(e);
    }
}

static void on_namemc(GtkButton *b, gpointer data)
{
    app_t *a = data;
    gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "pop")));
    char *url = g_strdup_printf("https://namemc.com/profile/%s", a->uuid ? a->uuid : a->name);
    GtkUriLauncher *l = gtk_uri_launcher_new(url);
    gtk_uri_launcher_launch(l, a->win, NULL, NULL, NULL);
    g_object_unref(l);
    g_free(url);
}

static GtkWidget *menu_button(GtkWidget *box, GtkWidget *pop, const char *label, GCallback cb, app_t *a)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(b))), 0);
    g_object_set_data(G_OBJECT(b), "pop", pop);
    g_signal_connect(b, "clicked", cb, a);
    gtk_box_append(GTK_BOX(box), b);
    return b;
}

/* built when it opens, so the model line is current */
static void on_open(GtkButton *btn, gpointer data)
{
    app_t *a = data;
    GtkWidget *pop = g_object_get_data(G_OBJECT(btn), "pop");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_size_request(box, 320, -1);

    gtk_box_append(GTK_BOX(box), mc_label(a->name ? a->name : "Not signed in", "mcl-h2"));
    if (a->uuid && *a->uuid) {
        GtkWidget *ur = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *ul = mc_label(a->uuid, "dim");
        gtk_label_set_selectable(GTK_LABEL(ul), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(ul), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_hexpand(ul, TRUE);
        gtk_label_set_xalign(GTK_LABEL(ul), 0);
        GtkWidget *cp = gtk_button_new_from_icon_name("edit-copy-symbolic");
        gtk_widget_set_tooltip_text(cp, "Copy the UUID");
        g_signal_connect(cp, "clicked", G_CALLBACK(on_copy), a);
        gtk_box_append(GTK_BOX(ur), ul);
        gtk_box_append(GTK_BOX(ur), cp);
        gtk_box_append(GTK_BOX(box), ur);
    }

    if (a->skin) {
        bool pic = mc3d_skin_is_slim(a->skin);
        char *t = g_strdup_printf("Skin model on your account: %s", a->slim ? "Slim (3 px arms)" : "Classic (4 px arms)");
        GtkWidget *ml = mc_label(t, NULL);
        gtk_label_set_xalign(GTK_LABEL(ml), 0);
        gtk_widget_set_margin_top(ml, 6);
        gtk_box_append(GTK_BOX(box), ml);
        g_free(t);
        if (pic != a->slim) {
            GtkWidget *w = mc_label(pic ? "Your skin is drawn with slim arms, so in game its arms have see-through "
                                          "strips. Upload it again as Slim to fix that."
                                        : "Your skin is drawn with wide arms but the account says Slim, so the arms "
                                          "get cut off in game. Upload it again as Classic to fix that.",
                                    "warn");
            gtk_label_set_wrap(GTK_LABEL(w), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(w), 40);
            gtk_label_set_xalign(GTK_LABEL(w), 0);
            gtk_box_append(GTK_BOX(box), w);
            GtkWidget *fx = gtk_button_new_with_label(pic ? "Set to Slim" : "Set to Classic");
            g_object_set_data(G_OBJECT(fx), "pop", pop);
            g_signal_connect(fx, "clicked", G_CALLBACK(on_fix_model), a);
            gtk_box_append(GTK_BOX(box), fx);
        }
    }

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    menu_button(box, pop, "Sign in again", G_CALLBACK(on_relog), a);
    menu_button(box, pop, "Accounts in Prism…", G_CALLBACK(on_prism), a);
    if (a->name)
        menu_button(box, pop, "Profile on NameMC", G_CALLBACK(on_namemc), a);
    gtk_popover_set_child(GTK_POPOVER(pop), box);
    gtk_popover_popup(GTK_POPOVER(pop));
}

static void open_later(gpointer b) { gtk_widget_activate(b); }

GtkWidget *mc_account_button(app_t *a)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "mcl-who-btn");
    gtk_widget_set_tooltip_text(b, "Account settings");
    GtkWidget *who = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(who, "mcl-who");
    GtkWidget *n = mc_label(a->name ? a->name : "not logged in", "mcl-who-name");
    gtk_label_set_xalign(GTK_LABEL(n), 0);
    GtkWidget *s = mc_label("Microsoft account", "dim");
    gtk_label_set_xalign(GTK_LABEL(s), 0);
    gtk_box_append(GTK_BOX(who), n);
    gtk_box_append(GTK_BOX(who), s);
    gtk_button_set_child(GTK_BUTTON(b), who);
    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_RIGHT);
    gtk_widget_set_parent(pop, b);
    g_object_set_data(G_OBJECT(b), "pop", pop);
    g_signal_connect_swapped(b, "destroy", G_CALLBACK(gtk_widget_unparent), pop);
    g_signal_connect(b, "clicked", G_CALLBACK(on_open), a);
    if (g_getenv("TUX_MC_ACCOUNT")) {                       /* screenshots: no click, so no grab */
        gtk_popover_set_autohide(GTK_POPOVER(pop), FALSE);
        g_timeout_add_once(900, open_later, b);
    }
    return b;
}
