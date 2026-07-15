#include "IPlugWebView.h"
#include "IPlugPaths.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <map>
#include <dirent.h>

static struct StderrUnbuffered { StderrUnbuffered() { setbuf(stderr, nullptr); } } s_unbuf;

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <execinfo.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <dlfcn.h>

// -------------------------------------------------------------------
//  IWebViewImpl  –  lives in the parent (plugin) process.
//  Delegates WebView work to a child process (WebViewHelper) via
//  g_spawn_async_with_pipes.
//
//  Protocol (line-based, \n terminated):
//    Parent → Child (stdin):
//      LOAD_URL url\n
//      LOAD_HTML html\n        – html must not contain \n
//      EVAL_JS id script\n     – id = opaque integer for response matching
//      RESIZE w h\n
//      SHOW 0|1\n
//      RELOAD\n
//      SCROLL 0|1\n
//      INTERACT 0|1\n
//      QUIT\n
//
//    Child → Parent (stdout):
//      READY xid\n
//      PAGE_LOADED\n
//      JS_RESULT id value\n
//      SCRIPT_MSG msg\n
//      CLOSED\n
// -------------------------------------------------------------------

// All state the child keeps.  It lives in the child's address space
// after fork(), so it can freely use GLib / GTK / WebKit.
struct WebViewChildState
{
  // --- widgets ---
  GtkWidget* window = nullptr;
  GtkWidget* scroll = nullptr;
  GtkWidget* webview = nullptr;
  WebKitUserContentManager* mgr = nullptr;

  // --- pipes (in child: stdin = commands, stdout = events) ---
  // stdin / stdout are already dup'd to the pipe ends by the parent
};

static WebViewChildState* g_state = nullptr;

// Forward declarations for child process helpers
static void child_shutdown_webview();

// ---- X11 poll for WM_DELETE_WINDOW (in child) ----
static gboolean x11_close_poll(gpointer data)
{
  auto state = static_cast<WebViewChildState*>(data);
  if (!state->window) return G_SOURCE_REMOVE;

  Display* dpy = gdk_x11_get_default_xdisplay();
  if (!dpy) return G_SOURCE_CONTINUE;

  static Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

  struct Pred {
    static Bool check(Display* d, XEvent* ev, XPointer arg) {
      if (ev->type != ClientMessage) return False;
      static Atom wp = XInternAtom(d, "WM_PROTOCOLS", False);
      auto* wm_del = (Atom*)arg;
      return ev->xclient.message_type == wp &&
             (Atom)ev->xclient.data.l[0] == *wm_del;
    }
  };

  XEvent xev;
  if (XCheckIfEvent(dpy, &xev, Pred::check, (XPointer)&wm_delete))
  {
    dprintf(1, "CLOSED\n");
    gtk_main_quit();
  }
  return G_SOURCE_CONTINUE;
}

// ---- WebKit/GTK callbacks (all run in child process) ----
static void on_page_loaded(WebKitWebView*, WebKitLoadEvent event, gpointer)
{
  if (event == WEBKIT_LOAD_FINISHED)
  {
    dprintf(1, "PAGE_LOADED\n");
    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(g_state->webview),
        "try { document.documentElement.focus(); } catch(e) {};"
        "try { document.body.focus(); } catch(e) {};"
        "try { window.focus(); } catch(e) {};",
        -1, nullptr, nullptr, nullptr, nullptr, nullptr);
  }
}

static gboolean on_script_msg(WebKitUserContentManager*,
    WebKitJavascriptResult* result, gpointer)
{
  JSCValue* val = webkit_javascript_result_get_js_value(result);
  char* str = jsc_value_to_string(val);
  if (str) {
    // Escape newlines to keep the line-based protocol clean
    for (char* p = str; *p; ++p) if (*p == '\n') *p = ' ';
    dprintf(1, "SCRIPT_MSG %s\n", str);
    g_free(str);
  }
  return TRUE;
}

static void on_decide_policy(WebKitWebView*, WebKitPolicyDecision* decision,
    WebKitPolicyDecisionType type, gpointer)
{
  if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
    auto* nav = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const gchar* uri = webkit_uri_request_get_uri(
        webkit_navigation_action_get_request(nav));
    if (uri && strstr(uri, "sounddna://") == uri) {
      dprintf(1, "SCRIPT_MSG %s\n", uri + 11);
      webkit_policy_decision_ignore(decision);
      return;
    }
    webkit_policy_decision_use(decision);
  } else {
    webkit_policy_decision_use(decision);
  }
}

static gboolean on_window_delete(GtkWidget*, GdkEvent*, gpointer)
{
  dprintf(1, "CLOSED\n");
  gtk_main_quit();
  return TRUE;
}

static gboolean on_key_press(GtkWidget*, GdkEventKey* ev, gpointer)
{
  if (ev->keyval == GDK_KEY_Escape) {
    dprintf(1, "CLOSED\n");
    gtk_main_quit();
    return TRUE;
  }
  return FALSE;
}

// JS eval completion callback
struct JsEvalCtx {
  int id;
};
static void on_js_finished(GObject* obj, GAsyncResult* res, gpointer data)
{
  auto* ctx = static_cast<JsEvalCtx*>(data);
  GError* err = nullptr;
  JSCValue* val = webkit_web_view_evaluate_javascript_finish(
      WEBKIT_WEB_VIEW(obj), res, &err);
  if (val) {
    char* str = jsc_value_to_string(val);
    if (str) {
      for (char* p = str; *p; ++p) if (*p == '\n') *p = ' ';
      dprintf(1, "JS_RESULT %d %s\n", ctx->id, str);
      g_free(str);
    }
  }
  delete ctx;
}

// ---- Command reader (GIOChannel on stdin) ----
static gboolean on_child_command(GIOChannel* ch, GIOCondition cond, gpointer)
{
  if (cond & (G_IO_HUP | G_IO_ERR)) {
    gtk_main_quit();
    return FALSE;
  }

  char* line = nullptr;
  gsize len = 0;
  if (g_io_channel_read_line(ch, &line, &len, nullptr, nullptr) != G_IO_STATUS_NORMAL) {
    g_free(line);
    gtk_main_quit();
    return FALSE;
  }

  // Strip trailing \n
  if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

  auto* s = g_state;
  if (!s) { g_free(line); return TRUE; }

       if (strcmp(line, "QUIT") == 0) {
    g_free(line);
    child_shutdown_webview();
    gtk_main_quit();
    return FALSE;   // remove watch
  }
  else if (strncmp(line, "LOAD_URL ", 9) == 0) {
    if (s->webview)
      webkit_web_view_load_uri(WEBKIT_WEB_VIEW(s->webview), line + 9);
  }
  else if (strncmp(line, "LOAD_HTML ", 10) == 0) {
    if (s->webview)
      webkit_web_view_load_html(WEBKIT_WEB_VIEW(s->webview), line + 10, nullptr);
  }
  else if (strncmp(line, "EVAL_JS ", 8) == 0) {
    // Format: EVAL_JS id script
    char* rest = line + 8;
    char* id_str = rest;
    char* space = strchr(rest, ' ');
    if (space) {
      *space = '\0';
      char* script = space + 1;
      int id = atoi(id_str);
      auto* ctx = new JsEvalCtx{id};
      webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(s->webview),
          script, -1, nullptr, nullptr, nullptr, on_js_finished, ctx);
    }
  }
  else if (strncmp(line, "RESIZE ", 7) == 0) {
    int nw, nh;
    if (sscanf(line + 7, "%d %d", &nw, &nh) == 2 && s->window)
      gtk_window_resize(GTK_WINDOW(s->window), nw, nh);
  }
  else if (strncmp(line, "SHOW ", 5) == 0) {
    bool vis = line[5] == '1';
    if (s->window) gtk_widget_set_visible(s->window, vis);
  }
  else if (strcmp(line, "RELOAD") == 0) {
    if (s->webview) webkit_web_view_reload(WEBKIT_WEB_VIEW(s->webview));
  }
  else if (strncmp(line, "SCROLL ", 7) == 0) {
    bool en = line[7] == '1';
    if (s->scroll)
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s->scroll),
          en ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
          en ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
  }
  else if (strncmp(line, "INTERACT ", 9) == 0) {
    bool en = line[9] == '1';
    if (s->webview) gtk_widget_set_sensitive(s->webview, en);
  }

  g_free(line);
  return TRUE;
}

// ---- Clean up WebKit before destroying widgets ----
static void child_shutdown_webview()
{
  if (!g_state || !g_state->webview) return;
  auto* wv = WEBKIT_WEB_VIEW(g_state->webview);
  webkit_web_view_stop_loading(wv);
  webkit_web_view_terminate_web_process(wv);
}

static void on_crash(int sig)
{
  fprintf(stderr, "\n[SDNA-child] CRASH: signal %d (%s)\n", sig, strsignal(sig));
  void* frames[64];
  int n = backtrace(frames, 64);
  backtrace_symbols_fd(frames, n, STDERR_FILENO);
  fprintf(stderr, "[SDNA-child] aborting\n");
  _exit(128 + sig);
}

// ---- Child entry point ----
// Called after fork().  stdin and stdout have already been dup'd to
// the pipe ends by the parent (see OpenWebView).
static void child_main(int w, int h, const char* title, bool devtools)
{
  // Install crash handler to get backtraces
  signal(SIGSEGV, on_crash);
  signal(SIGABRT, on_crash);
  signal(SIGFPE, on_crash);
  signal(SIGILL, on_crash);

  fprintf(stderr, "[SDNA-child] started (pid=%d)\n", getpid());

  prctl(PR_SET_PDEATHSIG, SIGTERM);
  signal(SIGPIPE, SIG_DFL);
  signal(SIGCHLD, SIG_DFL);

  fprintf(stderr, "[SDNA-child] gtk_init_check...\n");
  if (!gtk_init_check(nullptr, nullptr)) {
    fprintf(stderr, "[SDNA-child] gtk_init_check FAILED\n");
    _exit(2);
  }
  fprintf(stderr, "[SDNA-child] gtk_init_check OK\n");

  WebViewChildState s;
  g_state = &s;

  fprintf(stderr, "[SDNA-child] content manager...\n");
  s.mgr = webkit_user_content_manager_new();
  fprintf(stderr, "[SDNA-child] content manager OK\n");
  webkit_user_content_manager_register_script_message_handler(s.mgr, "iPlug");
  g_signal_connect(s.mgr, "script-message-received::iPlug",
      G_CALLBACK(on_script_msg), nullptr);

  fprintf(stderr, "[SDNA-child] creating webkit user script...\n");
  WebKitUserScript* zoom = webkit_user_script_new(
      "document.documentElement.dataset.zoom = '0.9';",
      WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
      nullptr, nullptr);
  webkit_user_content_manager_add_script(s.mgr, zoom);
  webkit_user_script_unref(zoom);

  fprintf(stderr, "[SDNA-child] creating webkit web view...\n");
  s.webview = webkit_web_view_new_with_user_content_manager(s.mgr);
  WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(s.webview));
  webkit_settings_set_javascript_can_open_windows_automatically(settings, true);
  webkit_settings_set_allow_file_access_from_file_urls(settings, true);
  webkit_settings_set_allow_universal_access_from_file_urls(settings, true);
  webkit_settings_set_enable_developer_extras(settings, devtools);
  webkit_settings_set_hardware_acceleration_policy(settings,
      WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

  g_signal_connect(s.webview, "load-changed", G_CALLBACK(on_page_loaded), nullptr);
  g_signal_connect(s.webview, "decide-policy", G_CALLBACK(on_decide_policy), nullptr);

  s.scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s.scroll),
      GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_container_add(GTK_CONTAINER(s.scroll), s.webview);
  gtk_widget_show(s.webview);
  gtk_widget_show(s.scroll);

  s.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(s.window), title);
  gtk_window_set_default_size(GTK_WINDOW(s.window), w, h);
  gtk_window_set_resizable(GTK_WINDOW(s.window), FALSE);
  gtk_container_add(GTK_CONTAINER(s.window), s.scroll);

  g_signal_connect(s.window, "delete-event", G_CALLBACK(on_window_delete), nullptr);
  g_signal_connect(s.window, "key-press-event", G_CALLBACK(on_key_press), nullptr);

  gtk_widget_set_can_focus(s.webview, TRUE);
  gtk_widget_grab_focus(s.webview);

  gtk_widget_show_all(s.window);
  gtk_widget_realize(s.window);

  // X11 poll for WM_DELETE_WINDOW (fallback for WMs that swallow delete-event)
  g_timeout_add_full(G_PRIORITY_HIGH_IDLE, 80, x11_close_poll, &s, nullptr);

  // Send the X11 window ID to the parent
  Window xid = gdk_x11_window_get_xid(gtk_widget_get_window(s.window));
  dprintf(1, "READY %lu\n", (unsigned long)xid);

  // Watch stdin for parent commands
  GIOChannel* cmd_ch = g_io_channel_unix_new(STDIN_FILENO);
  g_io_channel_set_encoding(cmd_ch, nullptr, nullptr);
  g_io_add_watch(cmd_ch, G_IO_IN, on_child_command, nullptr);
  g_io_channel_unref(cmd_ch);

  gtk_main();

  g_state = nullptr;
  _exit(0);
}

// -------------------------------------------------------------------
//  IWebViewImpl  –  lives in the parent (plugin) process
// -------------------------------------------------------------------
BEGIN_IPLUG_NAMESPACE

class IWebViewImpl
{
public:
  IWebViewImpl(IWebView* owner);
  ~IWebViewImpl();

  void* OpenWebView(void* pParent, float x, float y, float w, float h, float scale);
  void CloseWebView();
  void HideWebView(bool hide);
  void LoadHTML(const char* html);
  void LoadURL(const char* url);
  void LoadFile(const char* fileName, const char* bundleID);
  void ReloadPageContent();
  void EvaluateJavaScript(const char* scriptStr, IWebView::completionHandlerFunc func);
  void EnableScroll(bool enable);
  void EnableInteraction(bool enable);
  void SetWebViewBounds(float x, float y, float w, float h, float scale);
  void GetWebRoot(WDL_String& path) const { path.Set(mWebRoot.Get()); }
  void GetLocalDownloadPathForFile(const char* fileName, WDL_String& downloadPath);

private:
  // Send a command line to the child (appends \n internally)
  void Send(const char* fmt, ...);
  // Read one event line from the child (blocking, with timeout)
  // Returns false on EOF / timeout.
  bool ReadEvent(char* buf, size_t len, int timeoutMs = -1);
  // Drain pending events (non-blocking)
  void DrainEvents();

  IWebView* mIWebView = nullptr;

  int mWriteFd = -1;   // parent → child
  int mReadFd  = -1;   // child  → parent
  pid_t mChildPid = 0;

  WDL_String mWebRoot;
  Window mParentWindow = 0;
  Window mChildXid = 0;
  int mLastW = 800, mLastH = 600;
  bool mDevToolsEnabled = false;
  bool mInitialized = false;
  std::atomic<bool> mClosed{true};
  int mNextJsId = 1;

  // Protects pipe I/O (mWriteFd, mReadFd) – can be accessed from plugin thread
  // (EvaluateJavaScript) and the UI timer thread (DrainEvents).
  std::mutex mPipeMtx; // protects mWriteFd/mReadFd across plugin + UI timer threads

  // Pending JS completion handlers, indexed by id
  std::mutex mJsMtx;
  std::map<int, IWebView::completionHandlerFunc> mJsCallbacks;
};

END_IPLUG_NAMESPACE

using namespace iplug;

// ===================================================================
//  Construction / Destruction
// ===================================================================

IWebViewImpl::IWebViewImpl(IWebView* owner)
  : mIWebView(owner)
{
  mWebRoot.Set("web");
  mDevToolsEnabled = owner->GetEnableDevTools();
}

IWebViewImpl::~IWebViewImpl()
{
  CloseWebView();
}

// ===================================================================
//  IPC helpers
// ===================================================================

void IWebViewImpl::Send(const char* fmt, ...)
{
  if (mWriteFd < 0) return;
  va_list ap;
  va_start(ap, fmt);
  char* buf = nullptr;
  int n = vasprintf(&buf, fmt, ap);
  va_end(ap);
  if (n > 0 && buf) {
    {
      std::lock_guard<std::mutex> lock(mPipeMtx);
      if (mWriteFd >= 0) {
        write(mWriteFd, buf, n);
        write(mWriteFd, "\n", 1);
      }
    }
  }
  free(buf);
}

bool IWebViewImpl::ReadEvent(char* buf, size_t len, int timeoutMs)
{
  int fd;
  {
    std::lock_guard<std::mutex> lock(mPipeMtx);
    fd = mReadFd;
  }
  if (fd < 0) return false;

  if (timeoutMs > 0) {
    struct pollfd pfd = {fd, POLLIN, 0};
    int ret = poll(&pfd, 1, timeoutMs);
    if (ret <= 0) return false;
  }

  // Read one line
  size_t i = 0;
  while (i < len - 1) {
    char c;
    ssize_t n;
    {
      std::lock_guard<std::mutex> lock(mPipeMtx);
      if (mReadFd < 0) return false;
      n = read(mReadFd, &c, 1);
    }
    if (n <= 0) return false;
    if (c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = '\0';
  return true;
}

void IWebViewImpl::DrainEvents()
{
  // Non-blocking drain – handles CLOSED, JS_RESULT, SCRIPT_MSG, PAGE_LOADED
  char line[8192];
  for (;;) {
    int fd;
    {
      std::lock_guard<std::mutex> lock(mPipeMtx);
      fd = mReadFd;
    }
    if (fd < 0) break;
    struct pollfd pfd = {fd, POLLIN, 0};
    int ret = poll(&pfd, 1, 0);
    if (ret <= 0) break;
    if (!ReadEvent(line, sizeof(line), 0)) break;

    if (strcmp(line, "CLOSED") == 0) {
      fprintf(stderr, "[SDNA] child CLOSED\n");
      mClosed.store(true);
    }
    else if (strncmp(line, "JS_RESULT ", 10) == 0) {
      int id;
      char* rest = line + 10;
      char* val = strchr(rest, ' ');
      if (val) {
        *val++ = '\0';
        id = atoi(rest);
        std::lock_guard<std::mutex> lock(mJsMtx);
        auto it = mJsCallbacks.find(id);
        if (it != mJsCallbacks.end()) {
          it->second(val);
          mJsCallbacks.erase(it);
        }
      }
    }
    else if (strncmp(line, "SCRIPT_MSG ", 11) == 0) {
      if (mIWebView)
        mIWebView->OnMessageFromWebView(line + 11);
    }
    else if (strcmp(line, "PAGE_LOADED") == 0) {
      if (mIWebView)
        mIWebView->OnWebContentLoaded();
    }
  }
}

// Marker function used to locate the plugin .so via dladdr
extern "C" void SDNA_WebView_Marker() {}

static std::string FindHelperPath()
{
  // Try to find the helper next to the plugin .so via dladdr
  Dl_info info;
  if (dladdr((void*)SDNA_WebView_Marker, &info) && info.dli_fname) {
    std::string path(info.dli_fname);
    auto pos = path.rfind('/');
    if (pos != std::string::npos) {
      std::string hp = path.substr(0, pos + 1) + "WebViewHelper";
      if (access(hp.c_str(), X_OK) == 0) return hp;
    }
  }
  // Fallback: search PATH
  const char* env = getenv("PATH");
  if (env) {
    std::string paths(env);
    const char* sep = ":";
    size_t start = 0, end;
    while ((end = paths.find(sep, start)) != std::string::npos) {
      std::string dir = paths.substr(start, end - start);
      if (!dir.empty()) {
        std::string hp = dir + "/WebViewHelper";
        if (access(hp.c_str(), X_OK) == 0) return hp;
      }
      start = end + 1;
    }
    // Last entry
    std::string dir = paths.substr(start);
    if (!dir.empty()) {
      std::string hp = dir + "/WebViewHelper";
      if (access(hp.c_str(), X_OK) == 0) return hp;
    }
  }
  // Last resort
  return "";
}

// Find the path to resources/web/index.html relative to the plugin .so.
// Returns empty string if not found.
static std::string FindIndexPath()
{
  Dl_info info;
  if (!dladdr((void*)SDNA_WebView_Marker, &info) || !info.dli_fname)
    return "";
  std::string path(info.dli_fname);
  auto pos = path.rfind('/');
  if (pos == std::string::npos) return "";
  std::string dir = path.substr(0, pos);

  const char* suffix = "/x86_64-linux";
  size_t suffix_len = 13;

  // VST3 bundle: …/Contents/x86_64-linux/ → …/Contents/Resources/web/index.html
  auto arch = dir.rfind(suffix);
  if (arch != std::string::npos && arch + suffix_len == dir.size()) {
    std::string idx = dir.substr(0, arch) + "/Resources/web/index.html";
    if (access(idx.c_str(), F_OK) == 0) return idx;
  }

  // Dev / CLAP: try resources/web/index.html next to the .so
  std::string dev = dir + "/resources/web/index.html";
  if (access(dev.c_str(), F_OK) == 0) return dev;

  // One level up (e.g. .so in build/out/, repo root has resources/web/)
  auto parent = dir.rfind('/');
  if (parent != std::string::npos) {
    std::string up = dir.substr(0, parent + 1) + "resources/web/index.html";
    if (access(up.c_str(), F_OK) == 0) return up;
  }

  return "";
}

// ===================================================================
//  OpenWebView  –  fork the child process
// ===================================================================

void* IWebViewImpl::OpenWebView(void* pParent, float x, float y, float w, float h, float scale)
{
  fprintf(stderr, "[SDNA] OpenWebView ENTER\n");
  mParentWindow = reinterpret_cast<Window>(pParent);
  if (!mParentWindow) { fprintf(stderr, "[SDNA] OpenWebView: no parent\n"); return nullptr; }

  if (!mClosed.load()) { fprintf(stderr, "[SDNA] OpenWebView: already open\n"); return nullptr; }
  std::string helperPath = FindHelperPath();
  if (helperPath.empty()) {
    fprintf(stderr, "[SDNA] WebViewHelper binary not found\n");
    return nullptr;
  }
  fprintf(stderr, "[SDNA] helper binary: %s\n", helperPath.c_str());

  // Build argv for helper: parentXid w h devtools
  char xidStr[32], wStr[32], hStr[32], dtStr[8];
  snprintf(xidStr, sizeof(xidStr), "%lu", (unsigned long)mParentWindow);
  snprintf(wStr, sizeof(wStr), "%d", static_cast<int>(w));
  snprintf(hStr, sizeof(hStr), "%d", static_cast<int>(h));
  snprintf(dtStr, sizeof(dtStr), "%d", mDevToolsEnabled ? 1 : 0);
  const char* argv[] = {
    helperPath.c_str(), xidStr, wStr, hStr, dtStr, nullptr
  };

  // Spawn helper with pipes for stdin/stdout.
  // NOT using G_SPAWN_LEAVE_DESCRIPTORS_OPEN so GLib closes all inherited
  // fds (X11, D-Bus) before exec – the helper starts clean.
  GSpawnFlags flags = G_SPAWN_DEFAULT;
  int child_stdin_fd = -1, child_stdout_fd = -1;
  GError* err = nullptr;
  if (!g_spawn_async_with_pipes(nullptr, (char**)argv, nullptr, flags,
        nullptr, nullptr, &mChildPid,
        &child_stdin_fd, &child_stdout_fd, nullptr, &err)) {
    fprintf(stderr, "[SDNA] g_spawn failed: %s\n", err->message);
    g_error_free(err);
    return nullptr;
  }

  mWriteFd = child_stdin_fd;
  mReadFd  = child_stdout_fd;

  fprintf(stderr, "[SDNA] child pid=%d, waiting for READY...\n", mChildPid);

  // Read the X11 window ID from child
  char line[8192];
  if (!ReadEvent(line, sizeof(line), 10000)) {
    fprintf(stderr, "[SDNA] timeout waiting for child READY\n");
    CloseWebView();
    return nullptr;
  }

  unsigned long xid = 0;
  if (sscanf(line, "READY %lu", &xid) != 1) {
    fprintf(stderr, "[SDNA] unexpected child event: %s\n", line);
    CloseWebView();
    return nullptr;
  }

  mChildXid = xid;
  mClosed.store(false);
  mInitialized = true;
  mIWebView->OnWebViewReady();

  fprintf(stderr, "[SDNA] OpenWebView EXIT (child xid=%lu)\n", xid);
  fprintf(stderr, "[SDNA] helper OPEN=%s\n", mClosed.load() ? "NO" : "YES");
  return reinterpret_cast<void*>(xid);
}

// ===================================================================
//  CloseWebView  –  stop child with SIGTERM fallback
// ===================================================================

void IWebViewImpl::CloseWebView()
{
  if (mClosed.exchange(true)) return;
  fprintf(stderr, "[SDNA] CloseWebView ENTER\n");

  Send("QUIT");

  // Give the child 2 seconds to exit cleanly
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    int status;
    pid_t ret = waitpid(mChildPid, &status, WNOHANG);
    if (ret == mChildPid) {
      fprintf(stderr, "[SDNA] child exited (status=%d)\n", WEXITSTATUS(status));
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // If still alive, force kill
  if (waitpid(mChildPid, nullptr, WNOHANG) == 0) {
    fprintf(stderr, "[SDNA] child still alive, sending SIGTERM\n");
    kill(mChildPid, SIGTERM);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (waitpid(mChildPid, nullptr, WNOHANG) == 0) {
      fprintf(stderr, "[SDNA] child still alive, sending SIGKILL\n");
      kill(mChildPid, SIGKILL);
      waitpid(mChildPid, nullptr, 0);
    }
  }

  {
    std::lock_guard<std::mutex> lock(mPipeMtx);
    if (mWriteFd >= 0) { close(mWriteFd); mWriteFd = -1; }
    if (mReadFd >= 0)  { close(mReadFd);  mReadFd  = -1; }
  }

  mChildPid = 0;
  mChildXid = 0;
  mInitialized = false;
  fprintf(stderr, "[SDNA] CloseWebView EXIT\n");
}

// ===================================================================
//  Public helpers – send commands to child
// ===================================================================

void IWebViewImpl::HideWebView(bool hide)
{
  if (mClosed.load()) return;
  Send("SHOW %d", hide ? 0 : 1);
  DrainEvents();
}

void IWebViewImpl::LoadHTML(const char* html)
{
  if (mClosed.load()) return;

  // Try loading from the actual index.html file so that relative CSS/JS
  // paths resolve correctly inside WebKit (webkit_web_view_load_html with
  // a base URI doesn't reliably fetch file:// resources).
  std::string idxPath = FindIndexPath();
  fprintf(stderr, "[SDNA] LoadHTML: idxPath=%s\n", idxPath.empty() ? "(empty)" : idxPath.c_str());
  if (!idxPath.empty()) {
    std::string uri = "file://" + idxPath;
    fprintf(stderr, "[SDNA] LoadHTML -> LOAD_URI %s\n", uri.c_str());
    Send("LOAD_URI %s", uri.c_str());
    Send("RESIZE %d %d", mLastW, mLastH);
    DrainEvents();
    return;
  }

  // Fallback: send HTML inline (no relative resources will load)
  std::string flat;
  if (html) {
    flat.reserve(strlen(html));
    for (const char* p = html; *p; ++p)
      flat.push_back(*p == '\n' ? ' ' : *p);
  }
  Send("LOAD_HTML %s", flat.c_str());
  Send("RESIZE %d %d", mLastW, mLastH);
  DrainEvents();
}

void IWebViewImpl::LoadURL(const char* url)
{
  if (mClosed.load() || !url) return;
  Send("LOAD_URL %s", url);
  DrainEvents();
}

void IWebViewImpl::LoadFile(const char* fileName, const char* bundleID)
{
  if (mClosed.load() || !fileName) return;
  (void)bundleID;
  char absPath[4096] = {};
  if (fileName[0] == '/') {
    Send("LOAD_URL %s", fileName);
  } else if (getcwd(absPath, sizeof(absPath))) {
    std::string uri = std::string("file://") + absPath + "/" + fileName;
    Send("LOAD_URL %s", uri.c_str());
  }
  DrainEvents();
}

void IWebViewImpl::ReloadPageContent()
{
  if (mClosed.load()) return;
  Send("RELOAD");
  DrainEvents();
}

void IWebViewImpl::EvaluateJavaScript(const char* scriptStr, IWebView::completionHandlerFunc func)
{
  if (mClosed.load() || !scriptStr) return;

  int id = 0;
  if (func) {
    std::lock_guard<std::mutex> lock(mJsMtx);
    id = mNextJsId++;
    mJsCallbacks[id] = std::move(func);
  }

  Send("EVAL_JS %d %s", id, scriptStr);

  if (id == 0) {
    DrainEvents();
  }
  // If there's a callback, the result arrives asynchronously via DrainEvents
  // which is called in the timer or by the host.  We also do a quick poll now.
  DrainEvents();

  // If callback not yet received, the next DrainEvents() call will pick it up.
}

void IWebViewImpl::EnableScroll(bool enable)
{
  if (mClosed.load()) return;
  Send("SCROLL %d", enable ? 1 : 0);
  DrainEvents();
}

void IWebViewImpl::EnableInteraction(bool enable)
{
  if (mClosed.load()) return;
  Send("INTERACT %d", enable ? 1 : 0);
  DrainEvents();
}

void IWebViewImpl::SetWebViewBounds(float x, float y, float w, float h, float scale)
{
  if (mClosed.load()) return;
  (void)x; (void)y;
  mLastW = static_cast<int>(w * scale);
  mLastH = static_cast<int>(h * scale);
  Send("RESIZE %d %d", mLastW, mLastH);
  DrainEvents();
}

void IWebViewImpl::GetLocalDownloadPathForFile(const char* fileName, WDL_String& downloadPath)
{
  DesktopPath(downloadPath);
  downloadPath.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s", fileName);
}

#include "IPlugWebView.cpp"
