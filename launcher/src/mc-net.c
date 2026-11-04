/* mc-net.c — HTTP (libcurl) and the Minecraft account for tux-mc.
 *
 * The account is Prism's (accounts.json): name, skin, capes and tokens. For
 * cape/skin changes we need a Minecraft access token: Prism's own while it's
 * valid, else a fresh one from Prism's XSTS token, else the full Microsoft ->
 * Xbox Live -> XSTS -> Minecraft chain from Prism's refresh token. Fresh tokens
 * stay in memory; accounts.json is never written (Prism owns it).
 */
#include "mc.h"

#include <curl/curl.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

static size_t sink(char *p, size_t s, size_t n, void *ud)
{
    g_byte_array_append(ud, (guint8 *)p, s * n);
    return s * n;
}

static CURL *easy(const char *url, struct curl_slist **h, const char *token, const char *ctype, GByteArray *out)
{
    CURL *c = curl_easy_init();
    if (!c)
        return NULL;
    if (token) {
        char *auth = g_strdup_printf("Authorization: Bearer %s", token);
        *h = curl_slist_append(*h, auth);
        g_free(auth);
    }
    if (ctype) {
        char *ct = g_strdup_printf("Content-Type: %s", ctype);
        *h = curl_slist_append(*h, ct);
        g_free(ct);
    }
    *h = curl_slist_append(*h, "Accept: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, *h);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tux-mc/2.0");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (out) {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    }
    return c;
}

long mc_http(const char *method, const char *url, const char *token, const char *ctype, const char *body,
             GByteArray *out)
{
    struct curl_slist *h = NULL;
    CURL *c = easy(url, &h, token, ctype, out);
    if (!c)
        return 0;
    if (method)
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    if (body)
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    long code = 0;
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return code;
}

long mc_http_upload_skin(const char *token, const char *variant, const char *path, GByteArray *out)
{
    struct curl_slist *h = NULL;
    CURL *c = easy("https://api.minecraftservices.com/minecraft/profile/skins", &h, token, NULL, out);
    if (!c)
        return 0;
    curl_mime *mime = curl_mime_init(c);
    curl_mimepart *p = curl_mime_addpart(mime);
    curl_mime_name(p, "variant");
    curl_mime_data(p, variant, CURL_ZERO_TERMINATED);
    p = curl_mime_addpart(mime);
    curl_mime_name(p, "file");
    curl_mime_filedata(p, path);
    curl_mime_type(p, "image/png");
    curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
    long code = 0;
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_mime_free(mime);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return code;
}

/* ------------------------------------------------------------- tokens -- */
static JsonObject *parse(GByteArray *b, JsonParser **keep)
{
    *keep = json_parser_new();
    if (!b->len || !json_parser_load_from_data(*keep, (char *)b->data, b->len, NULL))
        return NULL;
    JsonNode *r = json_parser_get_root(*keep);
    return r && JSON_NODE_HOLDS_OBJECT(r) ? json_node_get_object(r) : NULL;
}

/* XSTS (rp://api.minecraftservices.com/) -> Minecraft access token */
static bool login_with_xbox(app_t *a, const char *uhs, const char *xsts)
{
    char *body = g_strdup_printf("{\"identityToken\":\"XBL3.0 x=%s;%s\"}", uhs, xsts);
    GByteArray *b = g_byte_array_new();
    long code = mc_http("POST", "https://api.minecraftservices.com/authentication/login_with_xbox", NULL,
                        "application/json", body, b);
    g_free(body);
    JsonParser *p;
    JsonObject *o = parse(b, &p);
    bool ok = false;
    if (code == 200 && o && json_object_has_member(o, "access_token")) {
        g_free(a->token);
        a->token = g_strdup(json_object_get_string_member(o, "access_token"));
        a->token_exp = g_get_real_time() / G_USEC_PER_SEC + json_object_get_int_member_with_default(o, "expires_in", 3600) - 60;
        ok = true;
    }
    g_object_unref(p);
    g_byte_array_unref(b);
    return ok;
}

static char *xbox_token(JsonObject *o, char **uhs)
{
    if (!o || !json_object_has_member(o, "Token"))
        return NULL;
    if (uhs) {
        JsonObject *dc = json_object_get_object_member(o, "DisplayClaims");
        JsonArray *xui = dc ? json_object_get_array_member(dc, "xui") : NULL;
        if (xui && json_array_get_length(xui))
            *uhs = g_strdup(json_object_get_string_member(json_array_get_object_element(xui, 0), "uhs"));
    }
    return g_strdup(json_object_get_string_member(o, "Token"));
}

/* Microsoft refresh token -> MSA access -> Xbox user token -> XSTS -> Minecraft */
static bool full_refresh(app_t *a, char **err)
{
    if (!a->msa_refresh || !a->msa_client) {
        *err = g_strdup("no Microsoft login in Prism");
        return false;
    }
    char *re = g_uri_escape_string(a->msa_refresh, NULL, FALSE);
    char *body = g_strdup_printf("client_id=%s&grant_type=refresh_token&refresh_token=%s"
                                 "&scope=XboxLive.signin%%20offline_access",
                                 a->msa_client, re);
    g_free(re);
    GByteArray *b = g_byte_array_new();
    long code = mc_http("POST", "https://login.microsoftonline.com/consumers/oauth2/v2.0/token", NULL,
                        "application/x-www-form-urlencoded", body, b);
    g_free(body);
    JsonParser *p;
    JsonObject *o = parse(b, &p);
    if (code != 200 || !o || !json_object_has_member(o, "access_token")) {
        *err = g_strdup_printf("Microsoft login refresh failed (%ld)", code);
        g_object_unref(p);
        g_byte_array_unref(b);
        return false;
    }
    char *msa = g_strdup(json_object_get_string_member(o, "access_token"));
    g_object_unref(p);
    g_byte_array_set_size(b, 0);

    body = g_strdup_printf("{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
                           "\"RpsTicket\":\"d=%s\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
                           msa);
    g_free(msa);
    code = mc_http("POST", "https://user.auth.xboxlive.com/user/authenticate", NULL, "application/json", body, b);
    g_free(body);
    o = parse(b, &p);
    char *user = code == 200 ? xbox_token(o, NULL) : NULL;
    g_object_unref(p);
    g_byte_array_set_size(b, 0);
    if (!user) {
        *err = g_strdup_printf("Xbox Live login failed (%ld)", code);
        g_byte_array_unref(b);
        return false;
    }

    body = g_strdup_printf("{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
                           "\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}",
                           user);
    g_free(user);
    code = mc_http("POST", "https://xsts.auth.xboxlive.com/xsts/authorize", NULL, "application/json", body, b);
    g_free(body);
    o = parse(b, &p);
    char *uhs = NULL, *xsts = code == 200 ? xbox_token(o, &uhs) : NULL;
    g_object_unref(p);
    g_byte_array_unref(b);
    if (!xsts || !uhs) {
        *err = g_strdup_printf("Xbox XSTS failed (%ld)", code);
        g_free(xsts);
        g_free(uhs);
        return false;
    }
    bool ok = login_with_xbox(a, uhs, xsts);
    g_free(a->xsts);
    g_free(a->uhs);
    a->xsts = xsts;
    a->uhs = uhs;
    a->xsts_exp = g_get_real_time() / G_USEC_PER_SEC + 3600 * 12;
    if (!ok)
        *err = g_strdup("Minecraft login failed");
    return ok;
}

char *mc_auth_token(app_t *a, char **err)
{
    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    if (a->token && now < a->token_exp)
        return g_strdup(a->token);
    if (a->xsts && a->uhs && now < a->xsts_exp && login_with_xbox(a, a->uhs, a->xsts))
        return g_strdup(a->token);
    if (full_refresh(a, err))
        return g_strdup(a->token);
    return NULL;
}

/* ------------------------------------------------------------ account -- */
void mc_load_account(app_t *a)
{
    char *path = g_build_filename(a->prism, "accounts.json", NULL);
    JsonParser *p = json_parser_new();
    bool ok = json_parser_load_from_file(p, path, NULL);
    g_free(path);
    JsonNode *root = ok ? json_parser_get_root(p) : NULL;
    JsonArray *accs = root && JSON_NODE_HOLDS_OBJECT(root)
                          ? json_object_get_array_member(json_node_get_object(root), "accounts") : NULL;
    JsonObject *acc = NULL;
    for (guint i = 0; accs && i < json_array_get_length(accs); i++) {
        JsonObject *o = json_array_get_object_element(accs, i);
        if (!acc || (json_object_has_member(o, "active") && json_object_get_boolean_member(o, "active")))
            acc = o;
    }
    if (!acc || !json_object_has_member(acc, "profile")) {
        g_object_unref(p);
        return;
    }
    JsonObject *prof = json_object_get_object_member(acc, "profile");
    a->name = g_strdup(json_object_get_string_member_with_default(prof, "name", "Player"));
    a->uuid = g_strdup(json_object_get_string_member_with_default(prof, "id", ""));
    if (json_object_has_member(acc, "ygg")) {
        JsonObject *y = json_object_get_object_member(acc, "ygg");
        a->token = g_strdup(json_object_get_string_member_with_default(y, "token", ""));
        a->token_exp = json_object_get_int_member_with_default(y, "exp", 0) - 60;
    }
    if (json_object_has_member(acc, "xrp-mc")) {
        JsonObject *x = json_object_get_object_member(acc, "xrp-mc");
        a->xsts = g_strdup(json_object_get_string_member_with_default(x, "token", ""));
        a->xsts_exp = json_object_get_int_member_with_default(x, "exp", 0) - 60;
        if (json_object_has_member(x, "extra"))
            a->uhs = g_strdup(json_object_get_string_member_with_default(
                json_object_get_object_member(x, "extra"), "uhs", ""));
    }
    if (json_object_has_member(acc, "msa")) {
        JsonObject *m = json_object_get_object_member(acc, "msa");
        a->msa_refresh = g_strdup(json_object_get_string_member_with_default(m, "refresh_token", ""));
    }
    a->msa_client = g_strdup(json_object_get_string_member_with_default(acc, "msa-client-id", ""));
    if (json_object_has_member(prof, "skin")) {
        JsonObject *sk = json_object_get_object_member(prof, "skin");
        const char *data = json_object_get_string_member_with_default(sk, "data", NULL);
        a->slim = !g_ascii_strcasecmp(json_object_get_string_member_with_default(sk, "variant", ""), "SLIM");
        if (data) {
            gsize n;
            guchar *raw = g_base64_decode(data, &n);
            GBytes *b = g_bytes_new_take(raw, n);
            a->skin = mc_png_bytes(b);
            g_bytes_unref(b);
        }
    }
    /* a skin uploaded from here is newer than Prism's copy until Prism refreshes */
    char *mine = g_build_filename(g_get_user_cache_dir(), "tux-mc", "skin.png", NULL);
    GStatBuf st1, st2;
    char *acc_path = g_build_filename(a->prism, "accounts.json", NULL);
    if (g_stat(mine, &st1) == 0 && g_stat(acc_path, &st2) == 0 && st1.st_mtime > st2.st_mtime) {
        cairo_surface_t *s = cairo_image_surface_create_from_png(mine);
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
            if (a->skin)
                cairo_surface_destroy(a->skin);
            a->skin = s;
            char *slim = g_strconcat(mine, ".slim", NULL);
            a->slim = g_file_test(slim, G_FILE_TEST_EXISTS);
            g_free(slim);
        } else {
            cairo_surface_destroy(s);
        }
    }
    g_free(acc_path);
    g_free(mine);

    const char *active = json_object_get_string_member_with_default(prof, "cape", "");
    a->active_cape = *active ? g_strdup(active) : NULL;
    JsonArray *capes = json_object_has_member(prof, "capes") ? json_object_get_array_member(prof, "capes") : NULL;
    for (guint i = 0; capes && i < json_array_get_length(capes) && a->ncapes < MAXCAPE; i++) {
        JsonObject *c = json_array_get_object_element(capes, i);
        cape_t *k = &a->capes[a->ncapes++];
        k->a = a;
        k->id = g_strdup(json_object_get_string_member_with_default(c, "id", ""));
        k->alias = g_strdup(json_object_get_string_member_with_default(c, "alias", "Cape"));
        k->url = g_strdup(json_object_get_string_member_with_default(c, "url", ""));
    }
    g_object_unref(p);
}
