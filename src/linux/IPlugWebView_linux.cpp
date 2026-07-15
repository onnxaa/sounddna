#include "IPlugWebView.h"
#include "IPlugPaths.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>

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

  IWebView* mIWebView = nullptr;
  GtkWidget* mPlug = nullptr;
  GtkWidget* mScrolledWindow = nullptr;
  GtkWidget* mWebView = nullptr;
  WebKitUserContentManager* mContentManager = nullptr;
  Window mParentWindow = 0;
  WDL_String mWebRoot;
  bool mVisible = true;
  bool mInitialized = false;
  bool mScrollEnabled = false;
  bool mDevToolsEnabled = false;
  bool isClosing_ = false;
};

END_IPLUG_NAMESPACE

using namespace iplug;

namespace {

void EnsureGtkInit() {
  static bool done = false;
  if (!done) {
    gtk_init_check(nullptr, nullptr);
    done = true;
  }
}

GdkWindow* GetGdkWindowFromX11(Window xid) {
  GdkDisplay* display = gdk_display_get_default();
  if (!display) return nullptr;
  return gdk_x11_window_foreign_new_for_display(display, xid);
}

}  // namespace

IWebViewImpl::IWebViewImpl(IWebView* owner)
  : mIWebView(owner)
{
  mWebRoot.Set("web");
  mDevToolsEnabled = owner->GetEnableDevTools();
}

IWebViewImpl::~IWebViewImpl()
{
  if (!isClosing_) CloseWebView();
}

void* IWebViewImpl::OpenWebView(void* pParent, float x, float y, float w, float h, float scale)
{
  EnsureGtkInit();
  mParentWindow = reinterpret_cast<Window>(pParent);
  if (!mParentWindow) return nullptr;

  GdkDisplay* display = gdk_display_get_default();
  if (!display) {
    gdk_display_manager_open_display(gdk_display_manager_get(), nullptr);
    display = gdk_display_get_default();
    if (!display) return nullptr;
  }

  isClosing_ = false;

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

  mPlug = gtk_plug_new_for_display(display, mParentWindow);
  gtk_container_add(GTK_CONTAINER(mPlug), mScrolledWindow);
  gtk_widget_set_size_request(mPlug,
      static_cast<gint>(w), static_cast<gint>(h));
  gtk_widget_add_events(mPlug,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
      GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
      GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
      GDK_FOCUS_CHANGE_MASK | GDK_KEY_PRESS_MASK |
      GDK_KEY_RELEASE_MASK);
  gtk_widget_add_events(GTK_WIDGET(mWebView),
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
      GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
      GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
      GDK_FOCUS_CHANGE_MASK | GDK_KEY_PRESS_MASK |
      GDK_KEY_RELEASE_MASK);

  gtk_widget_show(mPlug);

  gtk_widget_set_can_focus(mWebView, TRUE);
  gtk_widget_grab_focus(mWebView);

  mInitialized = true;
  mIWebView->OnWebViewReady();

  while (gtk_events_pending())
    gtk_main_iteration();

  return mPlug;
}

void IWebViewImpl::CloseWebView()
{
  if (isClosing_ || !mPlug) return;
  isClosing_ = true;

  if (mWebView) {
    g_signal_handlers_disconnect_by_data(mWebView, this);
  }
  if (mContentManager) {
    g_signal_handlers_disconnect_by_data(mContentManager, this);
  }

  gtk_widget_hide(mPlug);

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

  if (mPlug) {
    gtk_widget_destroy(mPlug);
    mPlug = nullptr;
  }
  mContentManager = nullptr;

  while (gtk_events_pending())
    gtk_main_iteration();

  mInitialized = false;
}

void IWebViewImpl::HideWebView(bool hide)
{
  if (isClosing_) return;
  mVisible = !hide;
  if (mPlug)
    gtk_widget_set_visible(mPlug, !hide);
}

void IWebViewImpl::LoadHTML(const char* html)
{
  if (isClosing_ || !mWebView) return;
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(mWebView), html, nullptr);
}

void IWebViewImpl::LoadURL(const char* url)
{
  if (isClosing_ || !mWebView) return;
  webkit_web_view_load_uri(WEBKIT_WEB_VIEW(mWebView), url);
}

void IWebViewImpl::LoadFile(const char* fileName, const char* bundleID)
{
  if (isClosing_ || !mWebView) return;
  (void)bundleID;
  char absPath[4096] = {};
  if (fileName[0] == '/') {
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(mWebView), fileName);
  } else {
    if (getcwd(absPath, sizeof(absPath))) {
      std::string uri = std::string("file://") + absPath + "/" + fileName;
      webkit_web_view_load_uri(WEBKIT_WEB_VIEW(mWebView), uri.c_str());
    }
  }
}

void IWebViewImpl::ReloadPageContent()
{
  if (isClosing_ || !mWebView) return;
  webkit_web_view_reload(WEBKIT_WEB_VIEW(mWebView));
}

void IWebViewImpl::EvaluateJavaScript(const char* scriptStr, IWebView::completionHandlerFunc func)
{
  if (isClosing_ || !mWebView) return;
  if (func) {
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(mWebView), scriptStr, -1, nullptr, nullptr, nullptr,
        [](GObject* obj, GAsyncResult* res, gpointer userData) {
          std::unique_ptr<IWebView::completionHandlerFunc> cb(
              static_cast<IWebView::completionHandlerFunc*>(userData));
          GError* err = nullptr;
          JSCValue* value = webkit_web_view_evaluate_javascript_finish(
              WEBKIT_WEB_VIEW(obj), res, &err);
          if (value) {
            char* str = jsc_value_to_string(value);
            if (str) {
              (*cb)(str);
              g_free(str);
            }
          }
        }, new IWebView::completionHandlerFunc(func));
  } else {
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(mWebView), scriptStr, -1, nullptr, nullptr, nullptr, nullptr, nullptr);
  }
}

void IWebViewImpl::EnableScroll(bool enable)
{
  if (isClosing_) return;
  mScrollEnabled = enable;
  if (mScrolledWindow) {
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(mScrolledWindow),
        enable ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
        enable ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
  }
}

void IWebViewImpl::EnableInteraction(bool enable)
{
  if (isClosing_ || !mWebView) return;
  gtk_widget_set_sensitive(mWebView, enable);
}

void IWebViewImpl::SetWebViewBounds(float x, float y, float w, float h, float scale)
{
  if (isClosing_) return;
  (void)x; (void)y;
  if (mPlug) {
    gtk_widget_set_size_request(mPlug,
        static_cast<gint>(w * scale),
        static_cast<gint>(h * scale));
  }
}

void IWebViewImpl::GetLocalDownloadPathForFile(const char* fileName, WDL_String& downloadPath)
{
  DesktopPath(downloadPath);
  downloadPath.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s", fileName);
}

void IWebViewImpl::OnPageLoaded(WebKitWebView* webView, WebKitLoadEvent event, gpointer userData)
{
  auto self = static_cast<IWebViewImpl*>(userData);
  if (self->isClosing_) return;
  if (event == WEBKIT_LOAD_FINISHED) {
    self->mIWebView->OnWebContentLoaded();
    if (self->isClosing_) return;
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
  if (self->isClosing_) return TRUE;
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
  if (self->isClosing_) { webkit_policy_decision_use(decision); return; }
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
