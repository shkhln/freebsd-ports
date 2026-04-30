--- content/zygote/zygote_linux.cc.orig	2026-03-13 06:02:14 UTC
+++ content/zygote/zygote_linux.cc
@@ -1,6 +1,7 @@
 // Copyright 2012 The Chromium Authors
 // Use of this source code is governed by a BSD-style license that can be
 // found in the LICENSE file.
+#if __FreeBSD__
 
 #include "content/zygote/zygote_linux.h"
 
@@ -50,7 +51,9 @@
 #include "ipc/ipc_channel.h"
 #include "sandbox/linux/services/credentials.h"
 #include "sandbox/linux/services/namespace_sandbox.h"
+#if !BUILDFLAG(IS_BSD)
 #include "sandbox/policy/linux/sandbox_linux.h"
+#endif
 #include "sandbox/policy/sandbox.h"
 #include "third_party/icu/source/i18n/unicode/timezone.h"
 
@@ -428,6 +431,7 @@ int Zygote::ForkWithRealPid(const std::string& process
     CHECK_NE(pid, 0);
   } else {
     PCHECK(base::CreatePipe(&read_pipe, &write_pipe));
+#if !BUILDFLAG(IS_FREEBSD)
     if (sandbox_flags_ & sandbox::policy::SandboxLinux::kPIDNS &&
         sandbox_flags_ & sandbox::policy::SandboxLinux::kUserNS) {
       pid = sandbox::NamespaceSandbox::ForkInNewPidNamespace(
@@ -435,11 +439,15 @@ int Zygote::ForkWithRealPid(const std::string& process
     } else {
       pid = sandbox::Credentials::ForkAndDropCapabilitiesInChild();
     }
+#else
+    pid = fork();
+#endif
   }
 
   if (pid == 0) {
     // In the child process.
 
+#if !BUILDFLAG(IS_FREEBSD)
     // If the process is the init process inside a PID namespace, it must have
     // explicit signal handlers.
     if (getpid() == 1) {
@@ -450,6 +458,7 @@ int Zygote::ForkWithRealPid(const std::string& process
             sig, sandbox::NamespaceSandbox::SignalExitCode(sig));
       }
     }
+#endif
 
     write_pipe.reset();
 
@@ -510,6 +519,12 @@ int Zygote::ForkWithRealPid(const std::string& process
     }
   }
 
+#if BUILDFLAG(IS_FREEBSD)
+  // FreeBSD has no PID namespaces to worry about.
+  CHECK_EQ(real_pid, -1);
+  real_pid = pid;
+#endif
+
   // If we successfully forked a child, but it crashed without sending
   // a message to the browser, the browser won't have found its PID.
   if (real_pid < 0) {
@@ -589,6 +604,10 @@ base::ProcessId Zygote::ReadArgsAndFork(base::PickleIt
 
   mapping.push_back(ipc_backchannel_);
 
+#if BUILDFLAG(IS_FREEBSD)
+  auto hook_data = sandbox::policy::ZygotePreForkHook(args);
+#endif
+
   // Returns at most twice: once with a valid PID (in the parent process,
   // returning the PID of the new child); and optionally once with a zero PID
   // in the forked child process. Note that a delegate may spawn the child
@@ -633,6 +652,11 @@ base::ProcessId Zygote::ReadArgsAndFork(base::PickleIt
     LOG(ERROR) << "Zygote could not fork: process_type " << process_type
                << " numfds " << numfds << " child_pid " << child_pid;
   }
+
+#if BUILDFLAG(IS_FREEBSD)
+  sandbox::policy::ZygotePostForkHook(child_pid, hook_data);
+#endif
+
   return child_pid;
 }
 
@@ -714,3 +715,4 @@ void Zygote::HandleReinitializeLoggingRequest(base::Pi
 }
 
 }  // namespace content
+#endif
