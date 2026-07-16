// Minimal helper executable launched by the plugin after fork.
// SPDX-License-Identifier: MIT
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <csignal>
#include <unistd.h>
#include <sys/prctl.h>

/* All state the helper keeps */
struct State {
  GtkWidget* window = nullptr;
  GtkWidget* scroll = nullptr;
  GtkWidget* webview = nullptr;
  WebKitUserContentManager* mgr = nullptr;
};

static State* g_state = nullptr;

/* ---- JS eval ---- */
struct JsEvalCtx { int id; };

static void on_js_finished(GObject* obj, GAsyncResult* res, gpointer data) {
  auto* ctx = static_cast<JsEvalCtx*>(data);
  GError* err = nullptr;
  JSCValue* val = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj), res, &err);
  if (val) {
    char* str = jsc_value_to_string(val);
    if (str) {
      for (char* p = str; *p; ++p) if (*p == '\n') *p = ' ';
      dprintf(STDOUT_FILENO, "JS_RESULT %d %s\n", ctx->id, str);
      g_free(str);
    }
  }
  delete ctx;
}

/* ---- callbacks ---- */
static void on_page_loaded(WebKitWebView* wv, WebKitLoadEvent event, gpointer) {
  const char* name = "?";
  if (event == WEBKIT_LOAD_STARTED) name = "STARTED";
  else if (event == WEBKIT_LOAD_REDIRECTED) name = "REDIRECTED";
  else if (event == WEBKIT_LOAD_COMMITTED) name = "COMMITTED";
  else if (event == WEBKIT_LOAD_FINISHED) name = "FINISHED";
  const gchar* uri = webkit_web_view_get_uri(wv);
  fprintf(stderr, "[WvH] load-changed: %s uri=%s\n", name, uri ? uri : "(null)");

  if (event == WEBKIT_LOAD_FINISHED) {
    dprintf(STDOUT_FILENO, "PAGE_LOADED\n");
    // Replace no-op IPlugSendMsg (set by utils.js fallback) with real bridge
    webkit_web_view_evaluate_javascript(wv,
        "window.IPlugSendMsg=function(msg){"
        "window.webkit.messageHandlers.iPlug.postMessage(typeof msg==='string'?msg:JSON.stringify(msg));"
        "};"
        "try{document.documentElement.focus();}catch(e){}"
        "try{document.body.focus();}catch(e){}"
        "try{window.focus();}catch(e){}",
        -1, nullptr, nullptr, nullptr, nullptr, nullptr);
  }
}

static void on_web_process_crashed(WebKitWebView*, gpointer) {
  fprintf(stderr, "[WvH] WEB PROCESS CRASHED!\n");
  dprintf(STDOUT_FILENO, "CRASHED\n");
}


static gboolean on_script_msg(WebKitUserContentManager*, WebKitJavascriptResult* result, gpointer) {
  JSCValue* val = webkit_javascript_result_get_js_value(result);
  char* str = jsc_value_to_string(val);
  if (str) {
    for (char* p = str; *p; ++p) if (*p == '\n') *p = ' ';
    fprintf(stderr, "[WvH] SCRIPT_MSG: %s\n", str);
    dprintf(STDOUT_FILENO, "SCRIPT_MSG %s\n", str);
    g_free(str);
  }
  return TRUE;
}

static void on_decide_policy(WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer) {
  if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
    auto* nav = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(nav));
    if (uri && strstr(uri, "sounddna://") == uri) {
      dprintf(STDOUT_FILENO, "SCRIPT_MSG %s\n", uri + 11);
      webkit_policy_decision_ignore(decision);
      return;
    }
    webkit_policy_decision_use(decision);
  } else {
    webkit_policy_decision_use(decision);
  }
}

static gboolean on_window_delete(GtkWidget*, GdkEvent*, gpointer) {
  dprintf(STDOUT_FILENO, "CLOSED\n");
  gtk_main_quit();
  return TRUE;
}

static gboolean on_key_press(GtkWidget*, GdkEventKey* ev, gpointer) {
  if (ev->keyval == GDK_KEY_Escape) {
    dprintf(STDOUT_FILENO, "CLOSED\n");
    gtk_main_quit();
    return TRUE;
  }
  return FALSE;
}

/* ---- X11 poll for WM_DELETE_WINDOW ---- */
static gboolean x11_close_poll(gpointer data) {
  auto* state = static_cast<State*>(data);
  if (!state->window) return G_SOURCE_REMOVE;
  Display* dpy = gdk_x11_get_default_xdisplay();
  if (!dpy) return G_SOURCE_CONTINUE;
  static Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  struct Pred {
    static Bool check(Display* d, XEvent* ev, XPointer arg) {
      if (ev->type != ClientMessage) return False;
      static Atom wp = XInternAtom(d, "WM_PROTOCOLS", False);
      auto* wm_del = (Atom*)arg;
      return ev->xclient.message_type == wp && (Atom)ev->xclient.data.l[0] == *wm_del;
    }
  };
  XEvent xev;
  if (XCheckIfEvent(dpy, &xev, Pred::check, (XPointer)&wm_delete)) {
    dprintf(STDOUT_FILENO, "CLOSED\n");
    gtk_main_quit();
  }
  return G_SOURCE_CONTINUE;
}

/* ---- GTK-level file drop handling ---- */
// Reads a file, base64-encodes it, and injects into page JS so app.loadAudioFile / loadDNAFile work.
// Also saves to temp file and notifies parent for audio analysis.
static void handle_file_drop(const char* path, int x, int y) {
  if (!g_state || !g_state->webview) return;
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "[WvH] drop: cannot open %s\n", path); return; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz < 0 || sz > 64LL * 1024 * 1024) { fclose(f); return; }
  auto* buf = (unsigned char*)malloc(sz);
  if (!buf) { fclose(f); return; }
  size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got == 0) { free(buf); return; }

  // Determine extension and drop type
  const char* ext = strrchr(path, '.');
  const char* dropType = "target";
  if (ext && (strcasecmp(ext, ".dna") == 0)) dropType = "source";
  bool isAudio = (ext && (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".aiff") == 0 ||
                          strcasecmp(ext, ".aif") == 0 || strcasecmp(ext, ".flac") == 0));

  // For audio files: save to temp and notify parent for analysis
  if (isAudio && got > 44) { // at least WAV header
    char tmppath[256];
    snprintf(tmppath, sizeof(tmppath), "/tmp/sounddna_drop_%d.wav", getpid());
    FILE* tmpf = fopen(tmppath, "wb");
    if (tmpf) {
      fwrite(buf, 1, got, tmpf);
      fclose(tmpf);
      dprintf(STDOUT_FILENO, "AUDIO_DROP %s\n", tmppath);
    }
  }

  // Base64 encode
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t b64len = 4 * ((got + 2) / 3);
  auto* b64out = (char*)malloc(b64len + 1);
  if (!b64out) { free(buf); return; }
  size_t j = 0;
  for (size_t i = 0; i < got; i += 3) {
    unsigned a = buf[i], b = i+1 < got ? buf[i+1] : 0, c = i+2 < got ? buf[i+2] : 0;
    unsigned v = (a << 16) | (b << 8) | c;
    b64out[j++] = b64[(v >> 18) & 0x3f];
    b64out[j++] = b64[(v >> 12) & 0x3f];
    b64out[j++] = i+1 < got ? b64[(v >> 6) & 0x3f] : '=';
    b64out[j++] = i+2 < got ? b64[v & 0x3f] : '=';
  }
  b64out[j] = '\0';
  free(buf);

  // Escape single quotes in path for JS string
  std::string escaped;
  for (const char* p = path; *p; ++p) {
    if (*p == '\\') escaped += "\\\\";
    else if (*p == '\'') escaped += "\\'";
    else escaped += *p;
  }

  // Build JS to create File and call handleDrop on page
  std::string js = "try{"
    "var bytes=Uint8Array.from(atob('" + std::string(b64out) + "'),function(c){return c.charCodeAt(0);});"
    "var f=new File([new Blob([bytes])],'" + escaped + "');"
    "window.app.handleDrop({dataTransfer:{files:[f]},preventDefault:function(){}},'" + std::string(dropType) + "');"
    "}catch(e){}";
  free(b64out);

  webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(g_state->webview),
      js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

static gboolean on_drag_drop(GtkWidget* w, GdkDragContext* ctx, int x, int y, guint time, gpointer) {
  fprintf(stderr, "[WvH] drag-drop at %d,%d\n", x, y);
  gtk_drag_get_data(w, ctx, gdk_atom_intern("text/uri-list", FALSE), time);
  return TRUE;
}

static void on_drag_data_received(GtkWidget* w, GdkDragContext* ctx, int x, int y,
    GtkSelectionData* sel, guint info, guint time, gpointer) {
  gchar** uris = gtk_selection_data_get_uris(sel);
  if (uris && uris[0]) {
    fprintf(stderr, "[WvH] drop file: %s\n", uris[0]);
    const char* p = uris[0];
    if (strncmp(p, "file://", 7) == 0) p += 7;
    handle_file_drop(p, x, y);
    g_strfreev(uris);
  }
  gtk_drag_finish(ctx, uris != nullptr, FALSE, time);
}

static gboolean on_drag_motion(GtkWidget* w, GdkDragContext* ctx, int x, int y, guint time, gpointer) {
  gdk_drag_status(ctx, GDK_ACTION_COPY, time);
  return TRUE;
}
/* ---- command reader ---- */
static gboolean on_command(GIOChannel* ch, GIOCondition cond, gpointer) {
  if (cond & (G_IO_HUP | G_IO_ERR)) { gtk_main_quit(); return FALSE; }
  char* line = nullptr;
  gsize len = 0;
  if (g_io_channel_read_line(ch, &line, &len, nullptr, nullptr) != G_IO_STATUS_NORMAL) {
    g_free(line); gtk_main_quit(); return FALSE;
  }
  if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
  auto* s = g_state;
  if (!s) { g_free(line); return TRUE; }
  if (strcmp(line, "QUIT") == 0) {
    g_free(line);
    if (s->webview) {
      webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(s->webview));
      webkit_web_view_terminate_web_process(WEBKIT_WEB_VIEW(s->webview));
    }
    gtk_main_quit();
    return FALSE;
  }
  if (strncmp(line, "LOAD_HTML ", 10) == 0) {
    fprintf(stderr, "[WvH] LOAD_HTML (%zu bytes)\n", strlen(line + 10));
    if (s->webview) webkit_web_view_load_html(WEBKIT_WEB_VIEW(s->webview), line + 10, nullptr);
  }
  else if (strncmp(line, "LOAD_URI ", 9) == 0) {
    fprintf(stderr, "[WvH] LOAD_URI %s\n", line + 9);
    if (s->webview) webkit_web_view_load_uri(WEBKIT_WEB_VIEW(s->webview), line + 9);
  }
  else if (strncmp(line, "LOAD_URL ", 9) == 0) {
    fprintf(stderr, "[WvH] LOAD_URL %s\n", line + 9);
    if (s->webview) webkit_web_view_load_uri(WEBKIT_WEB_VIEW(s->webview), line + 9);
  }
  else if (strncmp(line, "EVAL_JS ", 8) == 0) {
    char* rest = line + 8; char* id_str = rest; char* space = strchr(rest, ' ');
    if (space) { *space = '\0'; char* script = space + 1; int id = atoi(id_str);
      webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(s->webview), script, -1, nullptr, nullptr, nullptr, on_js_finished, new JsEvalCtx{id}); }
  }
  else if (strncmp(line, "RESIZE ", 7) == 0) {
    int nw, nh;
    if (sscanf(line + 7, "%d %d", &nw, &nh) == 2 && s->window)
      gtk_window_resize(GTK_WINDOW(s->window), nw, nh);
  }
  else if (strncmp(line, "SHOW ", 5) == 0) {
    if (s->window) gtk_widget_set_visible(s->window, line[5] == '1');
  }
  else if (strcmp(line, "RELOAD") == 0) {
    if (s->webview) webkit_web_view_reload(WEBKIT_WEB_VIEW(s->webview));
  }
  else if (strncmp(line, "SCROLL ", 7) == 0) {
    bool en = line[7] == '1';
    if (s->scroll) gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s->scroll),
        en ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
        en ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
  }
  else if (strncmp(line, "INTERACT ", 9) == 0) {
    if (s->webview) gtk_widget_set_sensitive(s->webview, line[9] == '1');
  }
  g_free(line);
  return TRUE;
}

int main(int argc, char* argv[]) {
  // Parse args: parentXid w h devtools
  if (argc < 5) { fprintf(stderr, "Usage: %s parentXid w h devtools\n", argv[0]); return 1; }
  unsigned long parentXid = strtoul(argv[1], nullptr, 10);
  int w = atoi(argv[2]), h = atoi(argv[3]);
  bool devtools = atoi(argv[4]) != 0;

  // Prctl PDEATHSIG: if parent dies, we get SIGTERM
  // (Linux-specific; ignore failure)
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  signal(SIGPIPE, SIG_DFL);
  signal(SIGCHLD, SIG_DFL);

  if (!gtk_init_check(nullptr, nullptr)) return 2;

  State s;
  g_state = &s;

  s.mgr = webkit_user_content_manager_new();
  webkit_user_content_manager_register_script_message_handler(s.mgr, "iPlug");
  g_signal_connect(s.mgr, "script-message-received::iPlug", G_CALLBACK(on_script_msg), nullptr);
  WebKitUserScript* zoom = webkit_user_script_new(
      "document.documentElement.dataset.zoom='0.9';",
      WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
  webkit_user_content_manager_add_script(s.mgr, zoom);
  webkit_user_script_unref(zoom);

  s.webview = webkit_web_view_new_with_user_content_manager(s.mgr);
  WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(s.webview));
  webkit_settings_set_javascript_can_open_windows_automatically(settings, true);
  webkit_settings_set_allow_file_access_from_file_urls(settings, true);
  webkit_settings_set_allow_universal_access_from_file_urls(settings, true);
  webkit_settings_set_enable_developer_extras(settings, devtools);
  webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
  g_signal_connect(s.webview, "load-changed", G_CALLBACK(on_page_loaded), nullptr);
  g_signal_connect(s.webview, "web-process-crashed", G_CALLBACK(on_web_process_crashed), nullptr);
  g_signal_connect(s.webview, "decide-policy", G_CALLBACK(on_decide_policy), nullptr);
  g_signal_connect(s.webview, "drag-motion", G_CALLBACK(on_drag_motion), nullptr);
  g_signal_connect(s.webview, "drag-drop", G_CALLBACK(on_drag_drop), nullptr);
  g_signal_connect(s.webview, "drag-data-received", G_CALLBACK(on_drag_data_received), nullptr);
  s.scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s.scroll), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_add(GTK_CONTAINER(s.scroll), s.webview);
  gtk_widget_show(s.webview);
  gtk_widget_show(s.scroll);



  s.window = gtk_plug_new_for_display(gdk_display_get_default(), parentXid);
  gtk_container_add(GTK_CONTAINER(s.window), s.scroll);
  gtk_widget_set_size_request(s.window, w, h);
  g_signal_connect(s.window, "delete-event", G_CALLBACK(on_window_delete), nullptr);
  g_signal_connect(s.window, "key-press-event", G_CALLBACK(on_key_press), nullptr);
  gtk_widget_set_can_focus(s.webview, TRUE);
  gtk_widget_grab_focus(s.webview);

  gtk_widget_show_all(s.window);
  gtk_widget_realize(s.window);

  // Force size and map directly via X11 — GtkPlug depends on the socket
  // parent for sizing/mapping, and REAPER may not drive the XEmbed
  // protocol until the user toggles to the custom UI.
  GdkWindow* gdkwin = gtk_widget_get_window(s.window);
  Window xid = 0;
  Display* dpy = nullptr;
  if (gdkwin) {
    dpy = gdk_x11_get_default_xdisplay();
    xid = gdk_x11_window_get_xid(gdkwin);
    if (dpy && xid) {
      XWindowChanges xwc;
      xwc.width  = w;
      xwc.height = h;
      xwc.x      = 0;
      xwc.y      = 0;
      XConfigureWindow(dpy, xid, CWWidth | CWHeight | CWX | CWY, &xwc);
      XMapWindow(dpy, xid);
      XFlush(dpy);
      // Wait for the X server to process the map so WebKit sees the
      // correct window state before it loads content.
      XSync(dpy, False);
    }
  }

  // Force WebKit layout at the intended size
  GtkAllocation alloc = {0, 0, w, h};
  gtk_widget_size_allocate(s.webview, &alloc);
  gtk_widget_grab_focus(s.webview);

  g_timeout_add_full(G_PRIORITY_HIGH_IDLE, 80, x11_close_poll, &s, nullptr);

  dprintf(STDOUT_FILENO, "READY %lu\n", (unsigned long)xid);

  GIOChannel* cmd_ch = g_io_channel_unix_new(STDIN_FILENO);
  g_io_channel_set_encoding(cmd_ch, nullptr, nullptr);
  g_io_add_watch(cmd_ch, G_IO_IN, on_command, nullptr);
  g_io_channel_unref(cmd_ch);

  gtk_main();
  g_state = nullptr;
  return 0;
}
