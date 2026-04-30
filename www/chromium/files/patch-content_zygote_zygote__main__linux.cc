--- content/zygote/zygote_main_linux.cc.orig	2026-04-14 21:31:37 UTC
+++ content/zygote/zygote_main_linux.cc
@@ -4,6 +4,8 @@
 
 #include "content/zygote/zygote_main.h"
 
+#include "build/build_config.h"
+
 #include <dlfcn.h>
 #include <fcntl.h>
 #include <pthread.h>
@@ -11,7 +13,9 @@
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>
+#if !BUILDFLAG(IS_BSD)
 #include <sys/prctl.h>
+#endif
 #include <sys/socket.h>
 #include <sys/types.h>
 #include <unistd.h>
@@ -29,7 +33,6 @@
 #include "base/strings/safe_sprintf.h"
 #include "base/strings/string_number_conversions.h"
 #include "base/system/sys_info.h"
-#include "build/build_config.h"
 #include "content/common/zygote/zygote_commands_linux.h"
 #include "content/public/common/content_descriptors.h"
 #include "content/public/common/zygote/sandbox_support_linux.h"
@@ -42,13 +45,16 @@
 #include "sandbox/linux/services/thread_helpers.h"
 #include "sandbox/linux/suid/client/setuid_sandbox_client.h"
 #include "sandbox/policy/linux/sandbox_debug_handling_linux.h"
+#if !BUILDFLAG(IS_BSD)
 #include "sandbox/policy/linux/sandbox_linux.h"
+#endif
 #include "sandbox/policy/sandbox.h"
 #include "sandbox/policy/switches.h"
 #include "third_party/icu/source/i18n/unicode/timezone.h"
 
 namespace content {
 
+#if !BUILDFLAG(IS_BSD)
 namespace {
 
 void CloseFds(const std::vector<int>& fds) {
@@ -158,9 +164,11 @@ static void EnterLayerOneSandbox(sandbox::policy::Sand
     CHECK(!using_layer1_sandbox);
   }
 }
+#endif // !BUILDFLAG(IS_BSD)
 
 bool ZygoteMain(
     std::vector<std::unique_ptr<ZygoteForkDelegate>> fork_delegates) {
+#if !BUILDFLAG(IS_OPENBSD)
   sandbox::SetAmZygoteOrRenderer(true, GetSandboxFD());
 
   auto* linux_sandbox = sandbox::policy::SandboxLinux::GetInstance();
@@ -175,6 +183,7 @@ bool ZygoteMain(
     linux_sandbox->PreinitializeSandbox();
   }
 
+#if !BUILDFLAG(IS_FREEBSD)
   const bool using_setuid_sandbox =
       linux_sandbox->setuid_sandbox_client()->IsSuidSandboxChild();
   const bool using_namespace_sandbox =
@@ -218,13 +227,25 @@ bool ZygoteMain(
   const bool namespace_sandbox_engaged =
       !!(sandbox_flags & sandbox::policy::SandboxLinux::kUserNS);
   CHECK_EQ(using_namespace_sandbox, namespace_sandbox_engaged);
+#else
+  VLOG(1) << "ZygoteMain: initializing " << fork_delegates.size()
+          << " fork delegates";
+  for (const auto& fork_delegate : fork_delegates) {
+    fork_delegate->Init(GetSandboxFD(), /*using_layer1_sandbox=*/false);
+  }
 
+  const int sandbox_flags = 0;
+#endif
+
   Zygote zygote(sandbox_flags, std::move(fork_delegates),
                 base::GlobalDescriptors::Descriptor(
                     static_cast<uint32_t>(kSandboxIPCChannel), GetSandboxFD()));
 
   // This function call can return multiple times, once per fork().
   return zygote.ProcessRequests();
+#else
+  return false;
+#endif
 }
 
 }  // namespace content
