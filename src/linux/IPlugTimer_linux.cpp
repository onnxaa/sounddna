#include "IPlugTimer.h"
#include <glib.h>

BEGIN_IPLUG_NAMESPACE

Timer* Timer::Create(ITimerFunction func, uint32_t intervalMs)
{
  return new Timer_impl(func, intervalMs);
}

Timer_impl::Timer_impl(ITimerFunction func, uint32_t intervalMs)
  : mTimerFunc(std::move(func))
{
  mTag = g_timeout_add(intervalMs, [](gpointer userData) -> gboolean {
    auto timer = static_cast<Timer_impl*>(userData);
    if (timer->mTimerFunc)
      timer->mTimerFunc(*timer);
    return G_SOURCE_CONTINUE;
  }, this);
}

Timer_impl::~Timer_impl()
{
  Stop();
}

void Timer_impl::Stop()
{
  if (mTag) {
    g_source_remove(mTag);
    mTag = 0;
  }
}

END_IPLUG_NAMESPACE
