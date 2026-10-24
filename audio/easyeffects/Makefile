PORTNAME=		easyeffects
DISTVERSIONPREFIX=	v
DISTVERSION=		8.3.0
CATEGORIES=		audio

MAINTAINER=	iwtcex@gmail.com
COMMENT=	Simple audio effects for PipeWire
WWW=		https://github.com/wwmm/easyeffects

LICENSE=	GPLv3+

BUILD_DEPENDS=	${LOCALBASE}/include/ladspa.h:audio/ladspa \
		nlohmann-json>0:devel/nlohmann-json

LIB_DEPENDS=	libbs2b.so:audio/libbs2b \
		libebur128.so:audio/ebur128 \
		libfftw3.so:math/fftw3 \
		libfftw3f.so:math/fftw3-float \
		libgsl.so:math/gsl \
		liblilv-0.so:audio/lilv \
		libmysofa.so:audio/libmysofa \
		libpipewire-0.3.so:multimedia/pipewire \
		librnnoise.so:audio/rnnoise \
		libsndfile.so:audio/libsndfile \
		libSoundTouch.so:audio/soundtouch \
		libspeexdsp.so:audio/speexdsp \
		libwebrtc-audio-processing-2.so:audio/webrtc-audio-processing2 \
		libzita-convolver.so:audio/zita-convolver

RUN_DEPENDS=	lsp-plugins-lv2>0:audio/lsp-plugins-lv2

USES=		cmake gettext-tools kde:6 pkgconfig qt:6

USE_GITHUB=	yes
GH_ACCOUNT=	wwmm

USE_KDE=	breeze-icons colorscheme config coreaddons ecm i18n iconthemes kirigami2 kirigami-addons qqc2-desktop-style
USE_QT=		base declarative graphs

CMAKE_ON=	ENABLE_LIBCPP_WORKAROUNDS

# Somehow we get quite a few transitive dependencies linked (notably libOpenGL.so),
# this makes zero sense.
LDFLAGS+=	-Wl,--as-needed

# GCC's libstdc++ aparently requires -ltbb for <execution> support; we use Clang there.
pre-configure:
	${REINPLACE_CMD} -e 's/find_package(TBB REQUIRED)//g' ${WRKSRC}/CMakeLists.txt
	${REINPLACE_CMD} -e 's/TBB::tbb//g' ${WRKSRC}/src/CMakeLists.txt

.include <bsd.port.mk>
