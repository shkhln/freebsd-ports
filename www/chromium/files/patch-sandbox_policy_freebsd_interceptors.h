--- sandbox/policy/freebsd/interceptors.h.orig	2026-04-15 07:33:28 UTC
+++ sandbox/policy/freebsd/interceptors.h
@@ -0,0 +1,35 @@
+#ifndef SANDBOX_POLICY_FREEBSD_INTERCEPTORS_H_
+#define SANDBOX_POLICY_FREEBSD_INTERCEPTORS_H_
+
+#include <sys/capsicum.h>
+extern "C" {
+#define WITH_CASPER
+#include <libcasper.h>
+}
+
+#include <functional>
+
+#include "base/files/file_path.h"
+
+int sandbox_open_dir (const base::FilePath& path, const cap_rights_t* rights);
+int sandbox_open_file(const base::FilePath& path, const cap_rights_t* rights);
+
+void sandbox_scan_for_symlinks(const base::FilePath& path);
+
+void sandbox_set_cap_net_channel   (cap_channel_t* chan);
+void sandbox_set_cap_sysctl_channel(cap_channel_t* chan);
+
+void sandbox_set_cwd();
+void sandbox_set_cwd(const base::FilePath& path);
+
+void sandbox_use_vlog();
+
+void sandbox_with_cap_sysctl_channel(
+  std::function<void(cap_channel_t*)>& action);
+
+void sandbox_with_cap_sysctl_channel(auto action) {
+  std::function<void(cap_channel_t*)> f = action;
+  sandbox_with_cap_sysctl_channel(f);
+}
+
+#endif // SANDBOX_POLICY_FREEBSD_INTERCEPTORS_H_
