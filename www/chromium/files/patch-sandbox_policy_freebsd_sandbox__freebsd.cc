--- sandbox/policy/freebsd/sandbox_freebsd.cc.orig	2026-08-12 09:02:10 UTC
+++ sandbox/policy/freebsd/sandbox_freebsd.cc
@@ -0,0 +1,693 @@
+// Copyright (c) 2012 The Chromium Authors. All rights reserved.
+// Use of this source code is governed by a BSD-style license that can be
+// found in the LICENSE file.
+
+#include "sandbox/policy/freebsd/sandbox_freebsd.h"
+
+#include <sys/param.h>
+#include <sys/types.h>
+
+#include <sys/pciio.h>
+#include <sys/resource.h>
+#include <sys/stat.h>
+#include <sys/time.h>
+
+#include <dirent.h>
+#include <dlfcn.h>
+#include <fcntl.h>
+#include <link.h>
+#include <stdint.h>
+#include <string.h>
+#include <unistd.h>
+
+extern "C" {
+#define WITH_CASPER
+#include <capsicum_helpers.h>
+#include <casper/cap_net.h>
+#include <casper/cap_sysctl.h>
+}
+
+#include <limits>
+#include <memory>
+#include <string>
+#include <vector>
+
+#include "base/command_line.h"
+#include "base/debug/stack_trace.h"
+#include "base/feature_list.h"
+#include "base/files/file_enumerator.h"
+#include "base/files/file_path.h"
+#include "base/files/file_util.h"
+#include "base/logging.h"
+#include "base/memory/singleton.h"
+#include "base/nix/mime_util_xdg.h"
+#include "base/nix/xdg_util.h"
+#include "base/path_service.h"
+#include "base/posix/eintr_wrapper.h"
+#include "base/strings/string_number_conversions.h"
+#include "base/system/sys_info.h"
+#include "base/threading/thread.h"
+#include "base/time/time.h"
+#include "build/build_config.h"
+#include "chrome/common/chrome_constants.h"
+#include "chrome/common/chrome_paths.h"
+#include "crypto/crypto_buildflags.h"
+#include "gpu/config/gpu_preferences.h"
+#include "gpu/config/gpu_switches.h"
+#include "media/base/media_switches.h"
+#include "sandbox/constants.h"
+#include "sandbox/linux/services/credentials.h"
+#include "sandbox/linux/services/namespace_sandbox.h"
+#include "sandbox/linux/services/proc_util.h"
+#include "sandbox/linux/services/resource_limits.h"
+#include "sandbox/linux/services/thread_helpers.h"
+#include "sandbox/linux/syscall_broker/broker_command.h"
+#include "sandbox/linux/syscall_broker/broker_process.h"
+#include "sandbox/policy/mojom/sandbox.mojom.h"
+#include "sandbox/policy/sandbox_type.h"
+#include "sandbox/policy/sandbox.h"
+#include "sandbox/policy/switches.h"
+#include "sandbox/sandbox_buildflags.h"
+
+#if BUILDFLAG(USING_SANITIZER)
+#include <sanitizer/common_interface_defs.h>
+#endif
+
+#include "interceptors.h"
+
+using namespace std::string_literals;
+
+namespace sandbox {
+namespace policy {
+
+SandboxLinux::SandboxLinux()
+    : pre_initialized_(false),
+      initialize_sandbox_ran_(false) {
+}
+
+SandboxLinux::~SandboxLinux() {
+  if (pre_initialized_) {
+    CHECK(initialize_sandbox_ran_);
+  }
+}
+
+SandboxLinux* SandboxLinux::GetInstance() {
+  SandboxLinux* instance = base::Singleton<SandboxLinux>::get();
+  CHECK(instance);
+  return instance;
+}
+
+void SandboxLinux::StopThread(base::Thread* thread) {
+  DCHECK(thread);
+  thread->Stop();
+}
+
+void SandboxLinux::PreinitializeSandbox() {
+  CHECK(!pre_initialized_);
+#if BUILDFLAG(USING_SANITIZER)
+  // Sanitizers need to open some resources before the sandbox is enabled.
+  // This should not fork, not launch threads, not open a directory.
+  __sanitizer_sandbox_on_notify(sanitizer_args());
+  sanitizer_args_.reset();
+#endif
+  pre_initialized_ = true;
+}
+
+bool SandboxLinux::InitializeSandbox(sandbox::mojom::Sandbox sandbox_type,
+                                     SandboxLinux::PreSandboxHook hook,
+                                     const Options& options) {
+  DCHECK(!initialize_sandbox_ran_);
+  initialize_sandbox_ran_ = true;
+
+  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
+  const std::string process_type =
+      command_line->GetSwitchValueASCII(switches::kProcessType);
+
+  if (command_line->HasSwitch(switches::kNoSandbox))
+    return true;
+
+  if (sandbox_type == sandbox::mojom::Sandbox::kNoSandbox) {
+    LOG(ERROR) << "No sandbox requested for " << process_type << " process";
+    return true;
+  }
+
+  // Our CDM wrapper has its own suid (jail) sandbox.
+  if (sandbox_type == sandbox::mojom::Sandbox::kCdm) {
+    return true;
+  }
+
+  // We won't even bother with ALSA.
+  if (sandbox_type == sandbox::mojom::Sandbox::kAudio &&
+    media::kAudioBackendParam.Get() == media::AudioBackend::kAlsa)
+  {
+    return true;
+  }
+
+  VLOG(0) << "Setting up Capsicum sandbox for " << process_type
+    << " (" << sandbox_type << ")" << " process";
+
+  {
+    std::stringstream ss;
+    ss << sandbox_type;
+    logging::SetLogPrefix(strdup(ss.str().c_str()));
+  }
+
+  sandbox_use_vlog();
+
+  // Only one thread is running, pre-initialize if not already done.
+  if (!pre_initialized_)
+    PreinitializeSandbox();
+
+  // Attempt to limit the future size of the address space of the process.
+  int error = 0;
+  const bool limited_as = LimitAddressSpace(&error);
+  if (error) {
+    // Restore errno. Internally to |LimitAddressSpace|, the errno due to
+    // setrlimit may be lost.
+    errno = error;
+    PCHECK(limited_as);
+  }
+
+  if (hook)
+    CHECK(std::move(hook).Run(options));
+
+  base::FilePath home_dir;
+  CHECK(base::PathService::Get(base::DIR_HOME, &home_dir));
+
+  base::FilePath cache_dir;
+  CHECK(base::PathService::Get(base::DIR_CACHE, &cache_dir));
+
+  auto env = base::Environment::Create();
+
+  auto conf_dir = base::nix::GetXDGDirectory(env.get(),
+    base::nix::kXdgConfigHomeEnvVar, base::nix::kDotConfigDir);
+
+  auto data_dir = base::nix::GetXDGDataWriteLocation(env.get());
+
+  auto run_dir = env->GetVar("XDG_RUNTIME_DIR")
+    .transform([](auto str) { return base::FilePath(str); })
+    .or_else([] {
+      return std::optional<base::FilePath>{
+        base::FilePath("/var/run/user")
+          .Append(std::to_string(getuid()))
+      };
+    })
+    .value();
+
+  auto localbase = base::FilePath("/usr/local"); // ?
+
+  switch (sandbox_type) {
+    case sandbox::mojom::Sandbox::kAudio: {
+
+      auto backend = media::kAudioBackendParam.Get();
+
+      // PulseAudio (Note that sandbox might prevent autospawn.)
+      if (backend == media::AudioBackend::kAuto ||
+          backend == media::AudioBackend::kPulseAudio)
+      {
+        auto to_file_path = [](std::string str) -> base::FilePath {
+          return base::FilePath(str);
+        };
+
+        auto pulse_config_path = env->GetVar("PULSE_CONFIG_PATH")
+          .transform(to_file_path).value_or(conf_dir.Append("pulse"));
+
+        auto pulse_runtime_path = env->GetVar("PULSE_RUNTIME_PATH")
+          .transform(to_file_path).value_or(run_dir.Append("pulse"));
+
+        dlopen("libpulse.so", RTLD_LAZY);
+
+        cap_rights_t rights;
+        sandbox_open_file(base::FilePath("/dev/urandom"),
+          cap_rights_init(&rights, CAP_LOOKUP, CAP_READ));
+
+        sandbox_open_file(base::FilePath("/etc/machine-id"),
+          cap_rights_init(&rights, CAP_FCNTL | CAP_FSTATAT | CAP_READ));
+
+        cap_rights_init(&rights, CAP_FLOCK, CAP_FCNTL, CAP_FSTATAT, CAP_READ);
+        sandbox_open_dir(home_dir.Append(".pulse"), &rights);
+        sandbox_open_dir(pulse_config_path,         &rights);
+
+        cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_READ);
+        sandbox_open_dir(localbase.Append("etc/pulse"),    &rights);
+        sandbox_open_dir(localbase.Append("share/locale"), &rights);
+
+        sandbox_open_dir(pulse_runtime_path,
+          cap_rights_init(&rights, CAP_CONNECTAT, CAP_FSTATAT, CAP_READ));
+      }
+
+      // sndio
+      if (backend == media::AudioBackend::kAuto ||
+          backend == media::AudioBackend::kSndio)
+      {
+        cap_rights_t rights;
+
+        cap_rights_init(&rights, CAP_CONNECTAT);
+        auto uid = std::to_string(getuid());
+        sandbox_open_dir(base::FilePath("/tmp/sndio-"s + uid), &rights);
+        sandbox_open_dir(base::FilePath("/tmp/sndio"),         &rights);
+
+        sandbox_open_dir(home_dir.Append(".sndio"),
+          cap_rights_init(&rights, CAP_CREATE, CAP_FSTATAT, CAP_READ));
+
+        auto to_dev_index = [](std::string s) -> int {
+          auto devnum_pos = sizeof("rsnd/") - 1;
+          return s.starts_with("rsnd/") &&
+            s.find_first_not_of("0123456789", devnum_pos) == std::string::npos ?
+              std::stoi(s.substr(devnum_pos)) : -1;
+        };
+
+        auto play_dev = env->GetVar("AUDIOPLAYDEVICE")
+          .or_else([&env] { return env->GetVar("AUDIODEVICE"); })
+          .transform(to_dev_index);
+
+        auto rec_dev = env->GetVar("AUDIORECDEVICE")
+          .or_else([&env] { return env->GetVar("AUDIODEVICE"); })
+          .transform(to_dev_index);
+
+        cap_rights_init(&rights, CAP_EVENT /* this one is rather non-obvious */,
+          CAP_IOCTL, CAP_LOOKUP, CAP_PREAD, CAP_PWRITE);
+
+        auto default_dsp = base::FilePath("/dev/dsp");
+
+        if (!play_dev.has_value()) {
+          sandbox_open_file(default_dsp, &rights);
+        } else if (*play_dev != -1) {
+          auto dsp = base::FilePath("/dev/dsp"s + std::to_string(*play_dev));
+          sandbox_open_file(dsp, &rights);
+        }
+
+        if (play_dev != rec_dev) {
+          if (!rec_dev.has_value()) {
+            sandbox_open_file(default_dsp, &rights);
+          } else if (*rec_dev != -1) {
+            auto dsp = base::FilePath("/dev/dsp"s + std::to_string(*rec_dev));
+            sandbox_open_file(dsp, &rights);
+          }
+        }
+      }
+
+      break;
+    }
+    case sandbox::mojom::Sandbox::kGpu:
+    case sandbox::mojom::Sandbox::kHardwareVideoDecoding:
+    case sandbox::mojom::Sandbox::kHardwareVideoEncoding: {
+
+      gpu::GpuPreferences gpu_preferences;
+      if (command_line->HasSwitch(::switches::kGpuPreferences)) {
+        auto value = command_line->GetSwitchValueASCII(
+          ::switches::kGpuPreferences);
+        CHECK(gpu_preferences.FromSwitchValue(value));
+      }
+
+      if (gpu_preferences.gpu_sandbox_start_early) {
+
+        // X11
+        cap_rights_t rights;
+        sandbox_open_file(home_dir.Append(".Xauthority"),
+          cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_READ));
+        sandbox_open_dir(base::FilePath("/tmp/.X11-unix"),
+          cap_rights_init(&rights, CAP_CONNECTAT, CAP_FSTATAT, CAP_READ));
+
+        //TODO: Wayland
+
+#if 0 // too broad, not really required
+        // /var/run/nvidia-xdriver-*
+        sandbox_open_dir(base::FilePath("/var/run"),
+          cap_rights_init(&rights, CAP_CONNECTAT, CAP_FSTATAT, CAP_READ));
+#endif
+
+        // Mesa's libGL config
+        cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_READ);
+        sandbox_open_file(home_dir.Append(".drirc"),        &rights);
+        sandbox_open_dir(localbase.Append("etc/drirc.d"),   &rights);
+        sandbox_open_dir(localbase.Append("share/drirc.d"), &rights);
+
+        // libdrm
+        sandbox_open_dir(localbase.Append("share/libdrm"), &rights);
+
+        // Vulkan ICDs
+        cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_READ);
+        sandbox_open_dir(localbase.Append("etc/vulkan"),   &rights);
+        sandbox_open_dir(localbase.Append("share/vulkan"), &rights);
+        sandbox_open_dir(conf_dir.Append("vulkan"),        &rights);
+        sandbox_open_dir(data_dir.Append("vulkan"),        &rights);
+
+        // Device nodes for Mesa
+        cap_rights_init(&rights,
+          CAP_FCNTL, CAP_FSTATAT, CAP_IOCTL, CAP_MMAP_RW);
+
+        // Those are symlinks pointing outside of /dev/dri,
+        // so we have to open them directly.
+        {
+          base::FileEnumerator nodes(base::FilePath("/dev/dri"), false,
+            base::FileEnumerator::FILES, "card*");
+
+          for (base::FilePath p = nodes.Next(); !p.empty(); p = nodes.Next()) {
+            VLOG(0) << "found card node: " << p;
+            sandbox_open_file(p, &rights);
+          }
+        }
+        {
+          base::FileEnumerator nodes(base::FilePath("/dev/dri"), false,
+            base::FileEnumerator::FILES, "render*");
+          for (base::FilePath p = nodes.Next(); !p.empty(); p = nodes.Next()) {
+            VLOG(0) << "found render node: " << p;
+            sandbox_open_file(p, &rights);
+          }
+        }
+
+        // We still have to open /dev/dri for whatever reason.
+        sandbox_open_dir(base::FilePath("/dev/dri"),
+          cap_rights_init(&rights, CAP_FSTATAT, CAP_READ));
+
+        // drmParsePciDeviceInfo in libdrm
+        int dev_pci_fd =
+          sandbox_open_file(base::FilePath("/dev/pci"),
+            cap_rights_init(&rights, CAP_IOCTL, CAP_LOOKUP, CAP_READ));
+        if (dev_pci_fd != -1) {
+          cap_ioctl_t cmds[] = {
+#if __FreeBSD_version < 1500064
+#define PCIOCGETCONF_FREEBSD15 _IOWR('p', 10, struct pci_conf_io)
+            PCIOCGETCONF_FREEBSD15,
+#endif
+            PCIOCGETCONF
+          };
+          if (cap_ioctls_limit(dev_pci_fd, cmds, nitems(cmds)) == -1) {
+            PLOG(FATAL) << "cap_ioctls_limit failed for /dev/pci";
+          }
+        }
+
+        // Misc libs
+        base::FilePath module_path;
+        CHECK(base::PathService::Get(base::DIR_MODULE, &module_path));
+
+        for (const auto& lib : {
+            "libEGL.so",
+            "libGLESv2.so",
+            "libvk_swiftshader.so",
+            "libvulkan.so.1"})
+        {
+          dlopen(module_path.Append(lib).value().c_str(), RTLD_LAZY);
+        }
+
+        // LIBRARY_PATH_FDS
+        std::stringstream ss;
+        cap_rights_init(&rights, CAP_FSTATAT, CAP_FSTATFS, CAP_MMAP_RX);
+        ss << sandbox_open_dir(localbase.Append("lib"),    &rights) << ':';
+        ss << sandbox_open_dir(base::FilePath("/usr/lib"), &rights) << ':';
+        ss << sandbox_open_dir(base::FilePath("/lib"),     &rights);
+
+        rtld_set_var("LIBRARY_PATH_FDS", ss.str().c_str());
+
+      } else {
+        base::FilePath module_path;
+        CHECK(base::PathService::Get(base::DIR_MODULE, &module_path));
+        for (const auto& lib : {"libvk_swiftshader.so"}) {
+          dlopen(module_path.Append(lib).value().c_str(), RTLD_LAZY);
+        }
+      }
+
+      // Device nodes for Nvidia
+      {
+        base::FileEnumerator nodes(base::FilePath("/dev"), false,
+          base::FileEnumerator::FILES, "nvidia*");
+
+        cap_rights_t rights;
+        cap_rights_init(&rights,
+          CAP_FCNTL, CAP_FSTATAT, CAP_IOCTL, CAP_MMAP_RW);
+
+        for (base::FilePath p = nodes.Next(); !p.empty(); p = nodes.Next()) {
+          sandbox_open_file(p, &rights);
+        }
+      }
+
+      // Nvidia GLCache
+      cap_rights_t rights;
+      cap_rights_init(&rights,
+        CAP_CREATE,
+        CAP_FCNTL,
+        CAP_FLOCK,
+        CAP_FSTATAT,
+        CAP_FTRUNCATE,
+        CAP_MKDIRAT,
+        CAP_PREAD,
+        CAP_PWRITE,
+        // There arguably should be something to permit renames within the same
+        // dir instead of CAP_RENAMEAT_SOURCE + CAP_RENAMEAT_TARGET.
+        CAP_RENAMEAT_SOURCE,
+        CAP_RENAMEAT_TARGET,
+        CAP_UNLINKAT);
+
+      sandbox_open_dir(home_dir .Append(".nv"),    &rights);
+      sandbox_open_dir(cache_dir.Append("nvidia"), &rights);
+
+      if (auto path = env->GetVar("__GL_SHADER_DISK_CACHE_PATH")) {
+        sandbox_open_dir(base::FilePath(*path), &rights);
+      }
+
+      // We'll reuse Nvidia's cache rights for Mesa.
+      sandbox_open_dir(cache_dir.Append("mesa_shader_cache"), &rights);
+
+      break;
+    }
+    case sandbox::mojom::Sandbox::kNetwork: {
+
+      base::FilePath udata_dir;
+      CHECK(base::PathService::Get(chrome::DIR_USER_DATA, &udata_dir));
+
+      cap_rights_t rights;
+      cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_PREAD);
+      sandbox_open_file(base::FilePath("/etc/hosts"),       &rights);
+      sandbox_open_file(base::FilePath("/etc/resolv.conf"), &rights);
+
+      cap_rights_init(&rights,
+        CAP_CREATE,
+        CAP_FCNTL,
+        CAP_FPATHCONF,
+        CAP_FSTATAT,
+        CAP_FSTATFS,
+        CAP_FSYNC,
+        CAP_FTRUNCATE,
+        CAP_MKDIRAT,
+        CAP_PREAD,
+        CAP_PWRITE,
+        // There arguably should be something to permit renames within the same
+        // dir instead of CAP_RENAMEAT_SOURCE + CAP_RENAMEAT_TARGET.
+        CAP_RENAMEAT_SOURCE,
+        CAP_RENAMEAT_TARGET,
+        CAP_UNLINKAT);
+
+      sandbox_open_dir(udata_dir.Append(chrome::kInitialProfile),   &rights);
+      sandbox_open_dir(udata_dir.Append(chrome::kGuestProfileDir),  &rights);
+      sandbox_open_dir(udata_dir.Append(chrome::kSystemProfileDir), &rights);
+
+      base::FileEnumerator profiles(udata_dir, false,
+        base::FileEnumerator::DIRECTORIES,
+        std::string(chrome::kMultiProfileDirPrefix) + "*"s);
+
+      // Doesn't work with newly created profiles until restart.
+      for (base::FilePath p = profiles.Next(); !p.empty(); p = profiles.Next()) {
+        sandbox_open_dir(p, &rights);
+      }
+
+      sandbox_open_dir(cache_dir.Append("chromium/Default"), &rights); // ?
+
+      cap_channel_t* casper = cap_init();
+      if (casper == nullptr) {
+        PLOG(FATAL) << "cap_init failed";
+      }
+
+      cap_channel_t* cap_net_chan = cap_service_open(casper, "system.net");
+      if (cap_net_chan == nullptr) {
+        PLOG(FATAL) << "cap_service_open(system.net) failed";
+      }
+
+      cap_channel_t* cap_sysctl_chan = cap_service_open(casper, "system.sysctl");
+      if (cap_sysctl_chan == nullptr) {
+        PLOG(FATAL) << "cap_service_open(system.sysctl) failed";
+      }
+
+      cap_close(casper);
+
+      cap_net_limit_t* limit = cap_net_limit_init(cap_net_chan, CAPNET_CONNECT);
+      if (limit == nullptr) {
+        PLOG(FATAL) << "cap_net_limit_init failed";
+      }
+
+      if (cap_net_limit(limit) == -1) {
+        PLOG(FATAL) << "cap_net_limit failed";
+      }
+
+      sandbox_set_cap_net_channel(cap_net_chan);
+      sandbox_set_cap_sysctl_channel(cap_sysctl_chan);
+
+      break;
+    }
+    case sandbox::mojom::Sandbox::kRenderer: {
+
+      cap_rights_t rights;
+      for (const auto& path : base::nix::GetXDGDataSearchLocations(env.get())) {
+        sandbox_open_file(path.Append("mime/mime.cache"),
+          cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_PREAD));
+      }
+
+      // Fontconfig
+      cap_rights_init(&rights, CAP_FCNTL, CAP_FSTATAT, CAP_READ);
+      sandbox_open_dir(conf_dir .Append("fontconfig"),  &rights);
+      sandbox_open_dir(data_dir .Append("fonts"),       &rights);
+      sandbox_open_dir(localbase.Append("etc/fonts"),   &rights);
+      sandbox_open_dir(localbase.Append("share/fonts"), &rights);
+
+      break;
+    }
+    case sandbox::mojom::Sandbox::kPrintCompositor:
+    case sandbox::mojom::Sandbox::kService:
+    case sandbox::mojom::Sandbox::kUtility:
+      break;
+    default:
+      LOG(ERROR) << "Unknown sandbox type: " << sandbox_type;
+  }
+
+  // thread_uw_init in libthr
+  dlopen("/lib/libgcc_s.so.1", RTLD_LAZY);
+
+  // Just in case we somehow missed /home -> /usr/home.
+  if (auto home = env->GetVar("HOME")) {
+    sandbox_scan_for_symlinks(base::FilePath(*home));
+  }
+
+  sandbox_set_cwd();
+
+  sandbox_with_cap_sysctl_channel([](cap_channel_t* cap_sysctl_chan) {
+
+    cap_sysctl_limit_t* limit = cap_sysctl_limit_init(cap_sysctl_chan);
+
+    cap_sysctl_limit_name(limit, "hw.machine",      CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "hw.ncpu",         CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "hw.realmem",      CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "kern.hostname",   CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "kern.ipc.shmmax", CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "kern.osrelease",  CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "kern.ostype",     CAP_SYSCTL_READ);
+    cap_sysctl_limit_name(limit, "kern.version",    CAP_SYSCTL_READ); // uname
+
+    // Mesa (libdrm)
+    cap_sysctl_limit_name(limit, "hw.dri",
+      CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
+    cap_sysctl_limit_name(limit, "kern.devname",
+      CAP_SYSCTL_READ | CAP_SYSCTL_WRITE);
+
+    // getifaddrs in kNetwork
+    cap_sysctl_limit_name(limit, "net.routetable",
+      CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
+
+    cap_sysctl_limit_name(limit, "vm.stats.vm",
+      CAP_SYSCTL_READ | CAP_SYSCTL_RECURSIVE);
+
+    if (cap_sysctl_limit(limit) == -1) {
+      PLOG(FATAL) << "cap_sysctl_limit failed";
+    }
+  });
+
+  caph_cache_catpages();
+  caph_cache_tzdata();
+
+  if (caph_limit_stdio() == -1) {
+    PLOG(FATAL) << "caph_limit_stdio failed";
+  }
+
+  if (caph_enter_casper() == -1) {
+    PLOG(FATAL) << "caph_enter_casper failed";
+  }
+
+  return true;
+}
+
+bool SandboxLinux::LimitAddressSpace(int* error) {
+#if !defined(ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) && \
+    !defined(THREAD_SANITIZER) && !defined(LEAK_SANITIZER)
+  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
+  sandbox::mojom::Sandbox sandbox_type =
+      SandboxTypeFromCommandLine(*command_line);
+  if (sandbox_type == sandbox::mojom::Sandbox::kNoSandbox) {
+    return false;
+  }
+
+  // Unfortunately, it does not appear possible to set RLIMIT_AS such that it
+  // will both (a) be high enough to support V8's and WebAssembly's address
+  // space requirements while also (b) being low enough to mitigate exploits
+  // using integer overflows that require large allocations, heap spray, or
+  // other memory-hungry attack modes.
+
+  *error = sandbox::ResourceLimits::Lower(
+      RLIMIT_DATA, static_cast<rlim_t>(sandbox::kDataSizeLimit));
+
+  // Cache the resource limit before turning on the sandbox.
+  base::SysInfo::AmountOfVirtualMemory();
+  base::SysInfo::MaxSharedMemorySize();
+
+  return *error == 0;
+#else
+  base::SysInfo::AmountOfVirtualMemory();
+  return false;
+#endif  // !defined(ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) &&
+        // !defined(THREAD_SANITIZER) && !defined(LEAK_SANITIZER)
+}
+
+HookData ZygotePreForkHook(std::vector<std::string> args) {
+
+//~ #if !defined(THREAD_SANITIZER)
+  //~ CHECK(sandbox::ThreadHelpers::IsSingleThreaded());
+//~ #endif
+
+  static cap_channel_t* cap_sysctl_chan = nullptr;
+
+  auto child_command_line = base::CommandLine::FromArgvWithoutProgram(args);
+  auto child_sandbox_type = SandboxTypeFromCommandLine(child_command_line);
+
+  if (cap_sysctl_chan == nullptr &&
+      child_sandbox_type != sandbox::mojom::Sandbox::kNoSandbox)
+  {
+    VLOG(0) << "Setting up Capsicum (libcasper) services for zygote";
+
+    cap_channel_t* casper = cap_init();
+    if (casper == nullptr) {
+      PLOG(FATAL) << "cap_init failed";
+    }
+
+    cap_sysctl_chan = cap_service_open(casper, "system.sysctl");
+    if (cap_sysctl_chan == nullptr) {
+      PLOG(FATAL) << "cap_service_open(system.sysctl) failed";
+    }
+
+    cap_close(casper);
+  }
+
+  return {
+    .parent_cap_sysctl_chan = reinterpret_cast<cap_channel_t*>(cap_sysctl_chan),
+    .child_cap_sysctl_chan  = reinterpret_cast<cap_channel_t*>(
+      child_sandbox_type != sandbox::mojom::Sandbox::kNoSandbox ?
+        cap_clone(cap_sysctl_chan) : nullptr)
+  };
+}
+
+void ZygotePostForkHook(int child_pid, const HookData& channels) {
+
+  cap_channel_t* parent_cap_sysctl_chan =
+    reinterpret_cast<cap_channel_t*>(channels.parent_cap_sysctl_chan);
+  cap_channel_t* child_cap_sysctl_chan  =
+    reinterpret_cast<cap_channel_t*>(channels.child_cap_sysctl_chan);
+
+  if (child_pid == 0) {
+    sandbox_set_cap_sysctl_channel(child_cap_sysctl_chan);
+    if (parent_cap_sysctl_chan != nullptr) {
+      cap_close(parent_cap_sysctl_chan);
+    }
+  } else if (child_cap_sysctl_chan != nullptr) {
+    cap_close(child_cap_sysctl_chan);
+  }
+}
+
+}  // namespace policy
+}  // namespace sandbox
