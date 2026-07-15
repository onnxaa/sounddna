#include "IPlugPlatform.h"
#include "IPlugPaths.h"

#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>

BEGIN_IPLUG_NAMESPACE

static void GetLinuxHome(WDL_String& path)
{
  const char* home = getenv("HOME");
  if (home) {
    path.Set(home);
  } else {
    struct passwd* pw = getpwuid(getuid());
    if (pw)
      path.Set(pw->pw_dir);
  }
}

void HostPath(WDL_String& path, const char* bundleID)
{
  (void)bundleID;
  path.Set("/proc/self/exe");
  char buf[4096] = {};
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    char* lastSlash = strrchr(buf, '/');
    if (lastSlash)
      *(lastSlash + 1) = '\0';
    path.Set(buf);
  }
}

void PluginPath(WDL_String& path, PluginIDType pExtra)
{
  HostPath(path);
}

void BundleResourcePath(WDL_String& path, PluginIDType pExtra)
{
  PluginPath(path, pExtra);
  path.Append("Resources/");
}

void DesktopPath(WDL_String& path)
{
  const char* xdg = getenv("XDG_DESKTOP_DIR");
  if (xdg) {
    path.Set(xdg);
  } else {
    GetLinuxHome(path);
    path.Append("/Desktop");
  }
}

void UserHomePath(WDL_String& path)
{
  GetLinuxHome(path);
}

void AppSupportPath(WDL_String& path, bool isSystem)
{
  if (isSystem) {
    path.Set("/etc");
  } else {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
      path.Set(xdg);
    } else {
      GetLinuxHome(path);
      path.Append("/.config");
    }
  }
}

void VST3PresetsPath(WDL_String& path, const char* mfrName, const char* pluginName, bool isSystem)
{
  if (isSystem) {
    path.Set("/usr/share/vst3/presets");
  } else {
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg) {
      path.Set(xdg);
    } else {
      GetLinuxHome(path);
      path.Append("/.local/share");
    }
  }
  path.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s/%s", mfrName, pluginName);
}

void INIPath(WDL_String& path, const char* pluginName)
{
  AppSupportPath(path, false);
  path.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s", pluginName);
}

void WebViewCachePath(WDL_String& path)
{
  const char* xdg = getenv("XDG_CACHE_HOME");
  if (xdg) {
    path.Set(xdg);
  } else {
    GetLinuxHome(path);
    path.Append("/.cache");
  }
  path.Append("/iPlug2/WebViewCache");
}

EResourceLocation LocateResource(const char* name, const char* type, WDL_String& result,
    const char* bundleID, void* pHInstance, const char* sharedResourcesSubPath)
{
  WDL_String pluginDir;
  PluginPath(pluginDir, pHInstance);

  WDL_String testPath;
  testPath.SetFormatted(MAX_WIN32_PATH_LEN, "%s%s/%s", pluginDir.Get(),
      sharedResourcesSubPath ? sharedResourcesSubPath : "Resources", name);

  struct stat st;
  if (stat(testPath.Get(), &st) == 0) {
    result.Set(testPath.Get());
    return EResourceLocation::kAbsolutePath;
  }

  testPath.SetFormatted(MAX_WIN32_PATH_LEN, "%s%s", pluginDir.Get(), name);
  if (stat(testPath.Get(), &st) == 0) {
    result.Set(testPath.Get());
    return EResourceLocation::kAbsolutePath;
  }

  char cwd[4096] = {};
  if (getcwd(cwd, sizeof(cwd))) {
    testPath.SetFormatted(MAX_WIN32_PATH_LEN, "%s/Resources/%s", cwd, name);
    if (stat(testPath.Get(), &st) == 0) {
      result.Set(testPath.Get());
      return EResourceLocation::kAbsolutePath;
    }
  }

  return EResourceLocation::kNotFound;
}

const void* LoadWinResource(const char* resID, const char* type, int& sizeInBytes, void* pHInstance)
{
  (void)resID;
  (void)type;
  (void)pHInstance;
  sizeInBytes = 0;
  return nullptr;
}

END_IPLUG_NAMESPACE
