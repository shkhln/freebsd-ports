--- content/browser/zygote_host/zygote_host_impl_linux.cc.orig	2026-02-17 23:34:34 UTC
+++ content/browser/zygote_host/zygote_host_impl_linux.cc
@@ -19,8 +19,10 @@
 #include "build/build_config.h"
 #include "content/common/zygote/zygote_commands_linux.h"
 #include "content/common/zygote/zygote_communication_linux.h"
+#if !BUILDFLAG(IS_BSD)
 #include "content/common/zygote/zygote_handle_impl_linux.h"
 #include "content/public/common/zygote/zygote_handle.h"
+#endif
 #include "sandbox/linux/services/credentials.h"
 #include "sandbox/linux/services/namespace_sandbox.h"
 #include "sandbox/linux/suid/client/setuid_sandbox_host.h"
@@ -38,6 +40,7 @@ namespace {
 
 namespace {
 
+#if !BUILDFLAG(IS_BSD)
 // Receive a fixed message on fd and return the sender's PID.
 // Returns true if the message received matches the expected message.
 bool ReceiveFixedMessage(int fd,
@@ -60,6 +63,7 @@ bool ReceiveFixedMessage(int fd,
     return false;
   return true;
 }
+#endif
 
 }  // namespace
 
@@ -69,9 +73,13 @@ ZygoteHostImpl::ZygoteHostImpl()
 }
 
 ZygoteHostImpl::ZygoteHostImpl()
+#if !BUILDFLAG(IS_OPENBSD)
     : use_namespace_sandbox_(false),
       use_suid_sandbox_(false),
       use_suid_sandbox_for_adj_oom_score_(false),
+#else
+    :
+#endif
       sandbox_binary_(),
       zygote_pids_lock_(),
       zygote_pids_() {}
@@ -84,6 +92,7 @@ void ZygoteHostImpl::Init(const base::CommandLine& com
 }
 
 void ZygoteHostImpl::Init(const base::CommandLine& command_line) {
+#if !BUILDFLAG(IS_BSD)
   if (command_line.HasSwitch(sandbox::policy::switches::kNoSandbox)) {
     return;
   }
@@ -138,6 +147,7 @@ void ZygoteHostImpl::Init(const base::CommandLine& com
            "you can try using --"
         << sandbox::policy::switches::kNoSandbox << ".";
   }
+#endif
 }
 
 void ZygoteHostImpl::AddZygotePid(pid_t pid) {
@@ -162,6 +172,7 @@ pid_t ZygoteHostImpl::LaunchZygote(
     base::CommandLine* cmd_line,
     base::ScopedFD* control_fd,
     base::FileHandleMappingVector additional_remapped_fds) {
+#if !BUILDFLAG(IS_OPENBSD)
   int fds[2];
   CHECK_EQ(0, socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds));
   CHECK(base::UnixDomainSocket::EnableReceiveProcessId(fds[0]));
@@ -175,6 +186,7 @@ pid_t ZygoteHostImpl::LaunchZygote(
       !cmd_line->HasSwitch(sandbox::policy::switches::kNoZygoteSandbox);
 
   base::ScopedFD dummy_fd;
+#if !BUILDFLAG(IS_FREEBSD)
   if (is_sandboxed_zygote && use_suid_sandbox_) {
     std::unique_ptr<sandbox::SetuidSandboxHost> sandbox_host(
         sandbox::SetuidSandboxHost::Create());
@@ -182,11 +194,16 @@ pid_t ZygoteHostImpl::LaunchZygote(
     sandbox_host->SetupLaunchOptions(&options, &dummy_fd);
     sandbox_host->SetupLaunchEnvironment();
   }
+#endif
 
   base::Process process =
+#if !BUILDFLAG(IS_FREEBSD)
       (is_sandboxed_zygote && use_namespace_sandbox_)
           ? sandbox::NamespaceSandbox::LaunchProcess(*cmd_line, options)
           : base::LaunchProcess(*cmd_line, options);
+#else
+      base::LaunchProcess(*cmd_line, options);
+#endif
   CHECK(process.IsValid()) << "Failed to launch zygote process";
 
   dummy_fd.reset();
@@ -195,6 +212,7 @@ pid_t ZygoteHostImpl::LaunchZygote(
 
   pid_t pid = process.Pid();
 
+#if !BUILDFLAG(IS_FREEBSD)
   if (is_sandboxed_zygote && (use_namespace_sandbox_ || use_suid_sandbox_)) {
     // The namespace and SUID sandbox will execute the zygote in a new
     // PID namespace, and the main zygote process will then fork from
@@ -227,12 +245,16 @@ pid_t ZygoteHostImpl::LaunchZygote(
     }
     pid = real_pid;
   }
+#endif
 
   AddZygotePid(pid);
   return pid;
+#else
+  return 0;
+#endif
 }
 
-#if !BUILDFLAG(IS_OPENBSD)
+#if !BUILDFLAG(IS_BSD)
 void ZygoteHostImpl::AdjustRendererOOMScore(base::ProcessHandle pid,
                                             int score) {
   // 1) You can't change the oom_score_adj of a non-dumpable process
