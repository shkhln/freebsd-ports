PORTNAME=		easyeffects
DISTVERSIONPREFIX=	v
DISTVERSION=		8.3.0
CATEGORIES=		audio

MAINTAINER=	iwtcex@gmail.com
COMMENT=	Simple audio effects for PipeWire
WWW=		https://github.com/wwmm/easyeffects

LICENSE=	GPLv3+


BUILD_DEPENDS=	${LOCALBASE}/bin/msgmerge:devel/gettext-tools \
		${LOCALBASE}/include/ladspa.h:audio/ladspa \
		nlohmann-json>0:devel/nlohmann-json

LIB_DEPENDS=	libabsl_base.so:devel/abseil \
		libabsl_log_severity.so:devel/abseil \
		libabsl_raw_logging_internal.so:devel/abseil \
		libabsl_spinlock_wait.so:devel/abseil \
		libbs2b.so:audio/libbs2b \
		libebur128.so:audio/ebur128 \
		libfftw3.so:math/fftw3 \
		libfftw3f.so:math/fftw3-float \
		libgsl.so:math/gsl \
		libgslcblas.so:math/gsl \
		liblilv-0.so:audio/lilv \
		libmysofa.so:audio/libmysofa \
		libpipewire-0.3.so:multimedia/pipewire \
		librnnoise.so:audio/rnnoise \
		libsndfile.so:audio/libsndfile \
		libSoundTouch.so:audio/soundtouch \
		libspeexdsp.so:audio/speexdsp \
		libtbb.so:devel/onetbb \
		libwebrtc-audio-processing-2.so:audio/webrtc-audio-processing2 \
		libzita-convolver.so:audio/zita-convolver

RUN_DEPENDS=	lsp-plugins-lv2>0:audio/lsp-plugins-lv2

USES=		cmake kde:6 pkgconfig qt:6

USE_GITHUB=	yes
GH_ACCOUNT=	wwmm

USE_KDE=	breeze-icons colorscheme config coreaddons ecm i18n iconthemes kirigami2 kirigami-addons qqc2-desktop-style
USE_QT=		base graphs

CMAKE_ON=	ENABLE_LIBCPP_WORKAROUNDS

.include <bsd.port.mk>
