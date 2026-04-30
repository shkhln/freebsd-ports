--- base/process/set_process_title.cc.orig	2026-04-06 16:25:54 UTC
+++ base/process/set_process_title.cc
@@ -40,6 +40,13 @@
 #include "base/process/set_process_title_linux.h"
 #endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
 
+#if BUILDFLAG(IS_FREEBSD)
+#include <sys/param.h>
+#include <sys/sysctl.h>
+// setproctitle_fast sidesteps kern.ps_arg_cache_limit sysctl.
+#define setproctitle(...) setproctitle_fast(__VA_ARGS__)
+#endif
+
 namespace base {
 
 // TODO(jrg): Find out if setproctitle or equivalent is available on Android.
@@ -92,6 +99,16 @@ void SetProcessTitleFromCommandLine(const char** main_
     program_invocation_short_name = &(*base_name_storage)[0];
   }
 #endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
+
+#if BUILDFLAG(IS_FREEBSD)
+  int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
+  title.resize(PATH_MAX);
+  size_t len = PATH_MAX;
+  if (sysctl(name, nitems(name), title.data(), &len, nullptr, 0) != -1) {
+    title.resize(len - 1);
+    have_argv0 = true;
+  }
+#endif
 
   const base::CommandLine* command_line =
       base::CommandLine::ForCurrentProcess();
