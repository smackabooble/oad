// Directory watching is not available in the browser; hot-reload is simply disabled.
#include "precompiled.h"
#include "lib/sysdep/dir_watch.h"

Status dir_watch_Add(const OsPath&, PDirWatch&) { return INFO::OK; }
Status dir_watch_Poll(DirWatchNotifications&) { return INFO::OK; }
