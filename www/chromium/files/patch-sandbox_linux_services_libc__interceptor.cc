--- sandbox/linux/services/libc_interceptor.cc.orig	2026-02-17 23:34:34 UTC
+++ sandbox/linux/services/libc_interceptor.cc
@@ -12,7 +12,9 @@
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>
+#if !BUILDFLAG(IS_BSD)
 #include <sys/prctl.h>
+#endif
 #include <sys/socket.h>
 #include <sys/types.h>
 #include <time.h>
@@ -168,7 +170,7 @@ bool ReadTimeStruct(base::PickleIterator* iter,
   } else {
     base::AutoLock lock(g_timezones_lock.Get());
     auto ret_pair = g_timezones.Get().insert(timezone);
-    output->tm_zone = ret_pair.first->c_str();
+    output->tm_zone = (char *)ret_pair.first->c_str();
   }
 
   return true;
@@ -351,10 +353,14 @@ __attribute__((__visibility__("default"))) struct tm* 
 __attribute__((__visibility__("default"))) struct tm* localtime_r_override(
     const time_t* timep,
     struct tm* result) {
+// Capsicum doesn't forbid localtime_r and ProxyLocaltimeCallToBrowser somehow
+// results in a deadlock on the logger init with zygotes, so we'll just skip it.
+#if !BUILDFLAG(IS_FREEBSD)
   if (g_am_zygote_or_renderer) {
     ProxyLocaltimeCallToBrowser(*timep, result, nullptr, 0);
     return result;
   }
+#endif
 
   InitLibcLocaltimeFunctions();
   struct tm* res = g_libc_localtime_r(timep, result);
