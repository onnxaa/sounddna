#include "IPlugWebView.h"
#include "IPlugPaths.h"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>

static struct StderrUnbuffered { StderrUnbuffered() { setbuf(stderr, nullptr); } } s_unbuf;

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>

// -------------------------------------------------------------------
// GtkThread – runs gtk_main() in a dedicated background thread so
// that GDK events are actually dispatched by GTK (gtk_main_do_event).
//
// GTK3 silently drops all events unless gtk_main() is active.  This
// class solves that: a dedicated thread calls gtk_init_check + gtk_main
// and all GTK widget work is scheduled via RunOnGtkThread().
//
// The host thread calls RunOnGtkThread() which blocks until the work
// completes inside the GTK thread.
//
// Limitations:
//   - GTK signal handlers always run on the GTK thread.
//   - CloseWebView must NOT stop the thread when called from a signal
//     handler on the GTK thread (the thread can't join itself).
// -------------------------------------------------------------------
class GtkThread
{
  GMainContext* mCtx = nullptr;
  std::thread mThread;
  std::atomic<bool> mRunning{false};

public:
  GtkThread() = default;
  ~GtkThread() { Stop(); }

  void Start()
  {
    if (mRunning.load(std::memory_order_relaxed)) return;
    mRunning.store(true, std::memory_order_relaxed);
    std::promise<void> ready;
    auto future = ready.get_future();
    mThread = std::thread([this, r = std::move(ready)]() mutable {
      fprintf(stderr, "[SDNA] GTK thread: init...\n");
      gtk_init_check(nullptr, nullptr);
      mCtx = g_main_context_default();
      // Heartbeat timer: forces poll() to return every 1ms
      g_timeout_add(1, [](gpointer) -> gboolean { return G_SOURCE_CONTINUE; }, nullptr);
      fprintf(stderr, "[SDNA] GTK thread: init done, ctx=%p, signalling ready\n", (void*)mCtx);
      r.set_value();
      fprintf(stderr, "[SDNA] GTK thread: entering gtk_main\n");
      gtk_main();
      fprintf(stderr, "[SDNA] GTK thread: gtk_main exited\n");
      mRunning.store(false, std::memory_order_relaxed);
    });
    fprintf(stderr, "[SDNA] host: waiting for GTK thread ready...\n");
    future.wait();
    fprintf(stderr, "[SDNA] host: GTK thread ready, host.default_ctx=%p gtk.default_ctx=%p\n",
            (void*)g_main_context_default(), (void*)mCtx);
    if (g_main_context_default() != mCtx)
      fprintf(stderr, "[SDNA] ** CONTEXT MISMATCH! host context != GTK context **\n");
  }

  void Stop()
  {
    if (!mRunning.load(std::memory_order_relaxed)) return;
    if (std::this_thread::get_id() == mThread.get_id())
      return;                            // can't join ourselves
    if (mCtx) {
      GSource* src = g_idle_source_new();
      g_source_set_callback(src, [](gpointer) -> gboolean {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
      }, nullptr, nullptr);
      g_source_attach(src, mCtx);
      g_source_unref(src);
      g_main_context_wakeup(mCtx);
    }
    if (mThread.joinable())
      mThread.join();
  }

  bool IsCurrentThread() const
  {
    return mThread.joinable() && std::this_thread::get_id() == mThread.get_id();
  }

    // Non-void return
  template <typename F, typename R = std::invoke_result_t<F>,
            std::enable_if_t<!std::is_void_v<R>, int> = 0>
  R Run(F&& f)
  {
    if (std::this_thread::get_id() == mThread.get_id())
      return f();

    struct Work {
      std::function<R()> fn;
      R result;
      std::mutex mtx;
      std::condition_variable cv;
      bool done = false;
    };
    Work w{std::forward<F>(f), R{}, {}, {}, false};

    fprintf(stderr, "[SDNA] Run: creating idle source\n");
    GSource* src = g_idle_source_new();
    g_source_set_priority(src, G_PRIORITY_HIGH);
    g_source_set_callback(src, [](gpointer data) -> gboolean {
      fprintf(stderr, "[SDNA] IDLE CB ENTERED!\n");
      Work* wp = static_cast<Work*>(data);
      fprintf(stderr, "[SDNA] IDLE CB calling fn...\n");
      wp->result = wp->fn();
      fprintf(stderr, "[SDNA] IDLE CB fn done\n");
      {
        std::lock_guard<std::mutex> lock(wp->mtx);
        wp->done = true;
      }
      wp->cv.notify_one();
      fprintf(stderr, "[SDNA] IDLE CB done\n");
      return G_SOURCE_REMOVE;
    }, &w, nullptr);
    fprintf(stderr, "[SDNA] Run: attaching to ctx=%p\n", (void*)mCtx);
    g_source_attach(src, mCtx);
    g_source_unref(src);
    fprintf(stderr, "[SDNA] Run: waking ctx=%p\n", (void*)mCtx);
    g_main_context_wakeup(mCtx);
    fprintf(stderr, "[SDNA] Run: waiting on cv...\n");

    std::unique_lock<std::mutex> lock(w.mtx);
    w.cv.wait(lock, [&w]() { return w.done; });
    return w.result;
  }

  // Void return
  template <typename F, typename R = std::invoke_result_t<F>,
            std::enable_if_t<std::is_void_v<R>, int> = 0>
  void Run(F&& f)
  {
    if (std::this_thread::get_id() == mThread.get_id()) {
      f();
      return;
    }

    struct Work {
      std::function<void()> fn;
      std::mutex mtx;
      std::condition_variable cv;
      bool done = false;
    };
    Work w{std::forward<F>(f), {}, {}, false};

    GSource* src = g_idle_source_new();
    g_source_set_priority(src, G_PRIORITY_HIGH);
    g_source_set_callback(src, [](gpointer data) -> gboolean {
      Work* wp = static_cast<Work*>(data);
      wp->fn();
      {
        std::lock_guard<std::mutex> lock(wp->mtx);
        wp->done = true;
      }
      wp->cv.notify_one();
      return G_SOURCE_REMOVE;
    }, &w, nullptr);
    g_source_attach(src, mCtx);
    g_source_unref(src);
    g_main_context_wakeup(mCtx);

    std::unique_lock<std::mutex> lock(w.mtx);
    w.cv.wait(lock, [&w]() { return w.done; });
  }

  template <typename F>
  auto RunOnGtkThread(F&& f) -> decltype(f()) { return Run(std::forward<F>(f)); }
};

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
  static void OnPageLoaded(WebKitWebView* webView, WebKitLoadEvent event, gpointer userData);
  static gboolean OnScriptMessageReceived(WebKitUserContentManager* manager,
      WebKitJavascriptResult* result, gpointer userData);
  static void OnDecidePolicy(WebKitWebView* webView, WebKitPolicyDecision* decision,
      WebKitPolicyDecisionType type, gpointer userData);

  void DestroyWidgets();
  void GtkThreadClose();

  static gboolean OnWindowDelete(GtkWidget*, GdkEvent* event, gpointer data);

  GtkThread mGtk;
  IWebView* mIWebView = nullptr;
  GtkWidget* mWindow = nullptr;
  GtkWidget* mScrolledWindow = nullptr;
  GtkWidget* mWebView = nullptr;
  WebKitUserContentManager* mContentManager = nullptr;
  Window mParentWindow = 0;
  WDL_String mWebRoot;
  bool mVisible = true;
  bool mInitialized = false;
  bool mScrollEnabled = false;
  bool mDevToolsEnabled = false;
  std::atomic<bool> isClosing_{true};   // start closed; OpenWebView resets to false
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
  if (!isClosing_.load()) CloseWebView();
  // mGtk destructor stops the thread (safe from host thread)
}

// ===================================================================
//  OpenWebView – create widgets inside the GTK thread
// ===================================================================

void* IWebViewImpl::OpenWebView(void* pParent, float x, float y, float w, float h, float scale)
{
  fprintf(stderr, "[SDNA] OpenWebView ENTER\n");

  mParentWindow = reinterpret_cast<Window>(pParent);
  if (!mParentWindow) { fprintf(stderr, "[SDNA] OpenWebView: no parent\n"); return nullptr; }

  mGtk.Start();

  isClosing_.store(false);

  fprintf(stderr, "[SDNA] calling RunOnGtkThread for widget creation...\n");
  // Everything GTK-related runs on the GTK thread
  GtkWidget* result = mGtk.RunOnGtkThread([&]() -> GtkWidget* {
    mContentManager = webkit_user_content_manager_new();
    WebKitUserContentManager* contentManager = mContentManager;

    XSetErrorHandler([](Display*, XErrorEvent*) -> int { return 0; });

    WebKitUserScript* zoomScript = webkit_user_script_new(
        "document.documentElement.dataset.zoom = '0.9';",
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(contentManager, zoomScript);
    webkit_user_script_unref(zoomScript);

    g_signal_connect(contentManager, "script-message-received::iPlug",
        G_CALLBACK(&IWebViewImpl::OnScriptMessageReceived), this);
    webkit_user_content_manager_register_script_message_handler(contentManager, "iPlug");

    mWebView = webkit_web_view_new_with_user_content_manager(contentManager);
    WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(mWebView));
    webkit_settings_set_javascript_can_open_windows_automatically(settings, true);
    webkit_settings_set_allow_file_access_from_file_urls(settings, true);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, true);
    webkit_settings_set_enable_developer_extras(settings, mDevToolsEnabled);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    WebKitWebView* webView = WEBKIT_WEB_VIEW(mWebView);

    g_signal_connect(webView, "load-changed", G_CALLBACK(&IWebViewImpl::OnPageLoaded), this);
    g_signal_connect(webView, "decide-policy", G_CALLBACK(&IWebViewImpl::OnDecidePolicy), this);

    mScrolledWindow = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(mScrolledWindow),
        GTK_POLICY_NEVER, GTK_POLICY_NEVER);
    gtk_container_add(GTK_CONTAINER(mScrolledWindow), mWebView);
    gtk_widget_show(mWebView);
    gtk_widget_show(mScrolledWindow);

    // Standalone GtkWindow (not embedded)
    mWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(mWindow), "SoundDNA");
    gtk_window_set_default_size(GTK_WINDOW(mWindow),
        static_cast<gint>(w), static_cast<gint>(h));
    gtk_window_set_resizable(GTK_WINDOW(mWindow), FALSE);
    gtk_container_add(GTK_CONTAINER(mWindow), mScrolledWindow);

    // When window is closed by the WM (X button) → destroy it and clean up
    g_signal_connect(mWindow, "delete-event",
        G_CALLBACK(IWebViewImpl::OnWindowDelete), this);

    // Also close on Escape key
    g_signal_connect(mWindow, "key-press-event", G_CALLBACK(+[](
        GtkWidget* w, GdkEventKey* ev, gpointer data) -> gboolean {
      if (ev->keyval == GDK_KEY_Escape) {
        fprintf(stderr, "[SDNA] Escape pressed, closing\n");
        auto self = static_cast<IWebViewImpl*>(data);
        self->OnWindowDelete(w, nullptr, data);
        return TRUE;
      }
      return FALSE;
    }), this);

    gtk_widget_set_can_focus(mWebView, TRUE);
    gtk_widget_grab_focus(mWebView);

    gtk_widget_show_all(mWindow);
    gtk_widget_realize(mWindow);

    // Poll for WM_DELETE_WINDOW via X11
    g_timeout_add_full(G_PRIORITY_HIGH_IDLE, 80, [](gpointer data) -> gboolean {
      auto self = static_cast<IWebViewImpl*>(data);
      if (self->isClosing_.load()) return G_SOURCE_REMOVE;
      if (!self->mWindow) return G_SOURCE_REMOVE;

      Display* dpy = gdk_x11_get_default_xdisplay();
      if (!dpy) return G_SOURCE_CONTINUE;

      static Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
      static int x11poll_cnt = 0;
      if (++x11poll_cnt % 62 == 0)
        fprintf(stderr, "[SDNA] X11poll alive (cnt=%d)\n", x11poll_cnt);

      // Predicate for XCheckIfEvent
      struct Pred {
        static Bool check(Display* d, XEvent* ev, XPointer arg) {
          if (ev->type != ClientMessage) return False;
          static Atom wp = XInternAtom(d, "WM_PROTOCOLS", False);
          Atom* wm_del = (Atom*)arg;
          return ev->xclient.message_type == wp &&
                 (Atom)ev->xclient.data.l[0] == *wm_del;
        }
      };

      XEvent xev;
      if (XCheckIfEvent(dpy, &xev, Pred::check, (XPointer)&wm_delete))
      {
        fprintf(stderr, "[SDNA] WM_DELETE_WINDOW via X11 poll\n");
        self->GtkThreadClose();
      }

      return G_SOURCE_CONTINUE;
    }, this, nullptr);

    return mWindow;
  });

  mInitialized = true;
  mIWebView->OnWebViewReady();

  fprintf(stderr, "[SDNA] OpenWebView EXIT (standalone window)\n");
  return result;
}

// ===================================================================
//  Close helpers
// ===================================================================

void IWebViewImpl::DestroyWidgets()
{
  // MUST be called from the GTK thread
  if (mWebView) {
    g_signal_handlers_disconnect_by_data(mWebView, this);
  }
  if (mContentManager) {
    g_signal_handlers_disconnect_by_data(mContentManager, this);
  }

  if (mWebView) {
    WebKitWebView* wv = WEBKIT_WEB_VIEW(mWebView);
    webkit_web_view_try_close(wv);
    webkit_web_view_terminate_web_process(wv);
  }

  if (mWebView) {
    gtk_widget_destroy(mWebView);
    mWebView = nullptr;
  }
  mScrolledWindow = nullptr;

  if (mWindow) {
    gtk_widget_destroy(mWindow);
    mWindow = nullptr;
  }
  mContentManager = nullptr;
}

void IWebViewImpl::GtkThreadClose()
{
  // Called from the GTK thread when the user clicks X on the window.
  // Clean up widgets but do NOT stop the thread.
  if (isClosing_.exchange(true)) return;
  DestroyWidgets();
  mInitialized = false;
  fprintf(stderr, "[SDNA] GtkThreadClose done\n");
}

gboolean IWebViewImpl::OnWindowDelete(GtkWidget*, GdkEvent*, gpointer data)
{
  auto self = static_cast<IWebViewImpl*>(data);
  fprintf(stderr, "[SDNA] delete-event (GTK thread)\n");
  self->GtkThreadClose();
  return TRUE;
}

void IWebViewImpl::CloseWebView()
{
  // Called from the HOST thread (VST3 removed / plugin destroy).
  fprintf(stderr, "[SDNA] CloseWebView ENTER\n");
  if (isClosing_.exchange(true)) { fprintf(stderr, "[SDNA] CloseWebView EARLY\n"); return; }

  mGtk.RunOnGtkThread([this]() { DestroyWidgets(); });
  mGtk.Stop();

  mInitialized = false;
  fprintf(stderr, "[SDNA] CloseWebView EXIT\n");
}

// ===================================================================
//  Public helpers
// ===================================================================

void IWebViewImpl::HideWebView(bool hide)
{
  if (isClosing_.load()) return;
  mVisible = !hide;
  mGtk.RunOnGtkThread([this, hide]() {
    if (!mWindow) return;
    if (hide) gtk_widget_hide(mWindow);
    else      gtk_widget_show(mWindow);
  });
}

void IWebViewImpl::LoadHTML(const char* html)
{
  if (isClosing_.load() || !mWebView) return;
  std::string h(html);
  mGtk.RunOnGtkThread([this, h]() {
    if (!mWebView) return;
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(mWebView), h.c_str(), nullptr);
  });
}

void IWebViewImpl::LoadURL(const char* url)
{
  if (isClosing_.load() || !mWebView) return;
  std::string u(url);
  mGtk.RunOnGtkThread([this, u]() {
    if (!mWebView) return;
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(mWebView), u.c_str());
  });
}

void IWebViewImpl::LoadFile(const char* fileName, const char* bundleID)
{
  if (isClosing_.load() || !mWebView) return;
  (void)bundleID;
  char absPath[4096] = {};
  if (fileName[0] == '/') {
    std::string uri(fileName);
    mGtk.RunOnGtkThread([this, uri]() {
      if (!mWebView) return;
      webkit_web_view_load_uri(WEBKIT_WEB_VIEW(mWebView), uri.c_str());
    });
  } else {
    if (getcwd(absPath, sizeof(absPath))) {
      std::string uri = std::string("file://") + absPath + "/" + fileName;
      mGtk.RunOnGtkThread([this, uri]() {
        if (!mWebView) return;
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(mWebView), uri.c_str());
      });
    }
  }
}

void IWebViewImpl::ReloadPageContent()
{
  if (isClosing_.load() || !mWebView) return;
  mGtk.RunOnGtkThread([this]() {
    if (!mWebView) return;
    webkit_web_view_reload(WEBKIT_WEB_VIEW(mWebView));
  });
}

void IWebViewImpl::EvaluateJavaScript(const char* scriptStr, IWebView::completionHandlerFunc func)
{
  if (isClosing_.load() || !mWebView) return;
  std::string script(scriptStr ? scriptStr : "");
  if (func) {
    auto cb = std::make_shared<IWebView::completionHandlerFunc>(std::move(func));
    mGtk.RunOnGtkThread([this, script, cb]() {
      if (!mWebView) return;
      webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(mWebView),
          script.c_str(), -1, nullptr, nullptr, nullptr,
          [](GObject* obj, GAsyncResult* res, gpointer userData) {
            auto funcPtr = static_cast<IWebView::completionHandlerFunc*>(userData);
            std::unique_ptr<IWebView::completionHandlerFunc> guard(funcPtr);
            GError* err = nullptr;
            JSCValue* value = webkit_web_view_evaluate_javascript_finish(
                WEBKIT_WEB_VIEW(obj), res, &err);
            if (value) {
              char* str = jsc_value_to_string(value);
              if (str) {
                (*funcPtr)(str);
                g_free(str);
              }
            }
          }, new IWebView::completionHandlerFunc(*cb));
    });
  } else {
    mGtk.RunOnGtkThread([this, script]() {
      if (!mWebView) return;
      webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(mWebView),
          script.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
    });
  }
}

void IWebViewImpl::EnableScroll(bool enable)
{
  if (isClosing_.load()) return;
  mScrollEnabled = enable;
  mGtk.RunOnGtkThread([this, enable]() {
    if (!mScrolledWindow) return;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(mScrolledWindow),
        enable ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
        enable ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
  });
}

void IWebViewImpl::EnableInteraction(bool enable)
{
  if (isClosing_.load() || !mWebView) return;
  mGtk.RunOnGtkThread([this, enable]() {
    if (!mWebView) return;
    gtk_widget_set_sensitive(mWebView, enable);
  });
}

void IWebViewImpl::SetWebViewBounds(float x, float y, float w, float h, float scale)
{
  if (isClosing_.load()) return;
  (void)x; (void)y;
  mGtk.RunOnGtkThread([this, w, h, scale]() {
    if (!mWindow) return;
    gtk_widget_set_size_request(mWindow,
        static_cast<gint>(w * scale),
        static_cast<gint>(h * scale));
  });
}

void IWebViewImpl::GetLocalDownloadPathForFile(const char* fileName, WDL_String& downloadPath)
{
  DesktopPath(downloadPath);
  downloadPath.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s", fileName);
}

// ===================================================================
//  Static callbacks – invoked by GTK/WebKit on the GTK thread
// ===================================================================

void IWebViewImpl::OnPageLoaded(WebKitWebView* webView, WebKitLoadEvent event, gpointer userData)
{
  auto self = static_cast<IWebViewImpl*>(userData);
  if (self->isClosing_.load()) return;
  if (event == WEBKIT_LOAD_FINISHED) {
    self->mIWebView->OnWebContentLoaded();
    if (self->isClosing_.load()) return;
    webkit_web_view_evaluate_javascript(webView,
        "try { document.documentElement.focus(); } catch(e) {};"
        "try { document.body.focus(); } catch(e) {};"
        "try { window.focus(); } catch(e) {};",
        -1, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (self->mWebView)
      gtk_widget_grab_focus(self->mWebView);
  }
}

gboolean IWebViewImpl::OnScriptMessageReceived(WebKitUserContentManager* manager,
    WebKitJavascriptResult* result, gpointer userData)
{
  auto self = static_cast<IWebViewImpl*>(userData);
  if (self->isClosing_.load()) return TRUE;
  JSCValue* value = webkit_javascript_result_get_js_value(result);
  char* str = jsc_value_to_string(value);
  if (str) {
    self->mIWebView->OnMessageFromWebView(str);
    g_free(str);
  }
  return TRUE;
}

void IWebViewImpl::OnDecidePolicy(WebKitWebView* webView, WebKitPolicyDecision* decision,
    WebKitPolicyDecisionType type, gpointer userData)
{
  auto self = static_cast<IWebViewImpl*>(userData);
  if (self->isClosing_.load()) { webkit_policy_decision_use(decision); return; }
  if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
    WebKitNavigationPolicyDecision* navDecision =
        WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    WebKitNavigationAction* navAction =
        webkit_navigation_policy_decision_get_navigation_action(navDecision);
    const gchar* uri = webkit_uri_request_get_uri(
        webkit_navigation_action_get_request(navAction));
    if (uri && !self->mIWebView->OnCanNavigateToURL(uri))
      webkit_policy_decision_ignore(decision);
    else
      webkit_policy_decision_use(decision);
  } else {
    webkit_policy_decision_use(decision);
  }
}

#include "IPlugWebView.cpp"
