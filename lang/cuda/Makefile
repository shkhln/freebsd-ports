PORTNAME=	cuda
DISTVERSION=	13.5
CATEGORIES=	lang

MASTER_SITES=	https://developer.download.nvidia.com/compute/cuda/repos/rhel9/x86_64/

# nsight was left out
DISTFILES=	cccl-13-5-13.3.5.0.6-1.x86_64.rpm \
		cuda-crt-13-5-13.5.37-1.x86_64.rpm \
		cuda-ctadvisor-13-5-13.5.37-1.x86_64.rpm \
		cuda-cudart-13-5-13.5.37-1.x86_64.rpm \
		cuda-cudart-devel-13-5-13.5.37-1.x86_64.rpm \
		cuda-culibos-devel-13-5-13.5.25-1.x86_64.rpm \
		cuda-cuobjdump-13-5-13.5.37-1.x86_64.rpm \
		cuda-cupti-13-5-13.5.25-1.x86_64.rpm \
		cuda-cuxxfilt-13-5-13.5.37-1.x86_64.rpm \
		cuda-driver-devel-13-5-13.5.37-1.x86_64.rpm \
		cuda-gdb-13-5-13.5.25-1.x86_64.rpm \
		cuda-gdb-src-13-5-13.5.25-1.x86_64.rpm \
		cuda-nvcc-13-5-13.5.37-1.x86_64.rpm \
		cuda-nvdisasm-13-5-13.5.37-1.x86_64.rpm \
		cuda-nvml-devel-13-5-13.5.37-1.x86_64.rpm \
		cuda-nvprune-13-5-13.5.37-1.x86_64.rpm \
		cuda-nvrtc-13-5-13.5.37-1.x86_64.rpm \
		cuda-nvrtc-devel-13-5-13.5.37-1.x86_64.rpm \
		cuda-nvtx-13-5-13.5.25-1.x86_64.rpm \
		cuda-opencl-13-5-13.5.25-1.x86_64.rpm \
		cuda-opencl-devel-13-5-13.5.25-1.x86_64.rpm \
		cuda-profiler-api-13-5-13.5.25-1.x86_64.rpm \
		cuda-sandbox-devel-13-5-13.5.25-1.x86_64.rpm \
		cuda-sanitizer-13-5-13.5.25-1.x86_64.rpm \
		cuda-tileiras-13-5-13.5.37-1.x86_64.rpm \
		cuda-toolkit-13-5-13.5.0-1.x86_64.rpm \
		cuda-toolkit-13-5-config-common-13.5.37-1.noarch.rpm \
		cuda-toolkit-13-config-common-13.5.37-1.noarch.rpm \
		cuda-toolkit-config-common-13.5.37-1.noarch.rpm \
		gds-tools-13-5-1.20.0.107-1.x86_64.rpm \
		libcublas-13-5-13.10.0.20-1.x86_64.rpm \
		libcublas-devel-13-5-13.10.0.20-1.x86_64.rpm \
		libcufft-13-5-12.5.0.14-1.x86_64.rpm \
		libcufft-devel-13-5-12.5.0.14-1.x86_64.rpm \
		libcufile-13-5-1.20.0.107-1.x86_64.rpm \
		libcufile-devel-13-5-1.20.0.107-1.x86_64.rpm \
		libcuobjclient-13-5-1.4.0.107-1.x86_64.rpm \
		libcuobjclient-devel-13-5-1.4.0.107-1.x86_64.rpm \
		libcurand-13-5-10.4.5.25-1.x86_64.rpm \
		libcurand-devel-13-5-10.4.5.25-1.x86_64.rpm \
		libcusolver-13-5-12.4.1.2-1.x86_64.rpm \
		libcusolver-devel-13-5-12.4.1.2-1.x86_64.rpm \
		libcusparse-13-5-12.9.4.2-1.x86_64.rpm \
		libcusparse-devel-13-5-12.9.4.2-1.x86_64.rpm \
		libnpp-13-5-13.2.0.93-1.x86_64.rpm \
		libnpp-devel-13-5-13.2.0.93-1.x86_64.rpm \
		libnvfatbin-13-5-13.5.37-1.x86_64.rpm \
		libnvfatbin-devel-13-5-13.5.37-1.x86_64.rpm \
		libnvjitlink-13-5-13.5.37-1.x86_64.rpm \
		libnvjitlink-devel-13-5-13.5.37-1.x86_64.rpm \
		libnvjpeg-13-5-13.3.2.43-1.x86_64.rpm \
		libnvjpeg-devel-13-5-13.3.2.43-1.x86_64.rpm \
		libnvptxcompiler-13-5-13.5.37-1.x86_64.rpm \
		libnvptxcompiler-devel-13-5-13.5.37-1.x86_64.rpm \
		libnvvm-13-5-13.5.37-1.x86_64.rpm

DIST_SUBDIR?=	cuda

MAINTAINER=	iwtcex@gmail.com
COMMENT=	GPU programming toolkit
WWW=		https://developer.nvidia.com/cuda/toolkit

LICENSE=	CUDA_EULA # https://docs.nvidia.com/cuda/eula/index.html
LICENSE_NAME=	NVIDIA Software License Agreement and CUDA Supplement to Software License Agreement
LICENSE_FILE=	${WRKSRC}/usr/share/licenses/cuda-nvcc-13-5/LICENSE
LICENSE_PERMS=	none

ONLY_FOR_ARCHS=	amd64

USES=		linux:rl9

# it's a port with Linux binaries, ffs
QA_ENV=		IGNORE_QA_shebang=1

NO_BUILD=	yes
NO_WRKSUBDIR=	yes

#TODO: ldconfig?
do-install:
	${CP} -R ${WRKSRC}/usr/local/cuda-${DISTVERSION} ${STAGEDIR}${PREFIX}
	${CP} -R ${WRKSRC}/usr/share ${STAGEDIR}${PREFIX}/cuda-${DISTVERSION}
	${CP} -R ${WRKSRC}/usr/lib64/pkgconfig ${STAGEDIR}${PREFIX}/cuda-${DISTVERSION}/share
	cd ${STAGEDIR}${PREFIX} && ${LN} -s cuda-${DISTVERSION} cuda

.include <bsd.port.mk>
