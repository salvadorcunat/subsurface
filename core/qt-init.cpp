// SPDX-License-Identifier: GPL-2.0
#include <QApplication>
#include <QNetworkProxy>
#include <QLibraryInfo>
#include <QTextCodec>
#include "helpers.h"
#include "core/subsurface-qt/SettingsObjectWrapper.h"

char *settings_suffix = NULL;
static QTranslator *qtTranslator, *ssrfTranslator;

void init_qt_late()
{
	QApplication *application = qApp;
	// tell Qt to use system proxies
	// note: on Linux, "system" == "environment variables"
	QNetworkProxyFactory::setUseSystemConfiguration(true);

	// for Win32 and Qt5 we try to set the locale codec to UTF-8.
	// this makes QFile::encodeName() work.
#ifdef Q_OS_WIN
	QTextCodec::setCodecForLocale(QTextCodec::codecForMib(106));
#endif

	QCoreApplication::setOrganizationName("Subsurface");
	QCoreApplication::setOrganizationDomain("subsurface.hohndel.org");
	// enable user specific settings (based on command line argument)
	if (settings_suffix) {
		if (verbose)
#if defined(SUBSURFACE_MOBILE) && ((defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || (defined(Q_OS_DARWIN) && !defined(Q_OS_IOS)))
			qDebug() << "using custom config for" << QString("Subsurface-Mobile-%1").arg(settings_suffix);
#else
			qDebug() << "using custom config for" << QString("Subsurface-%1").arg(settings_suffix);
#endif
#if defined(SUBSURFACE_MOBILE) && ((defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || (defined(Q_OS_DARWIN) && !defined(Q_OS_IOS)))
		QCoreApplication::setApplicationName(QString("Subsurface-Mobile-%1").arg(settings_suffix));
#else
		QCoreApplication::setApplicationName(QString("Subsurface-%1").arg(settings_suffix));
#endif
	} else {
#if defined(SUBSURFACE_MOBILE) && ((defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || (defined(Q_OS_DARWIN) && !defined(Q_OS_IOS)))
		QCoreApplication::setApplicationName("Subsurface-Mobile");
#else
		QCoreApplication::setApplicationName("Subsurface");
#endif
	}
	// find plugins installed in the application directory (without this SVGs don't work on Windows)
	SettingsObjectWrapper::instance()->load();

	QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
	QLocale loc;
	QString uiLang = uiLanguage(&loc);
	QLocale::setDefault(loc);

	qtTranslator = new QTranslator;
	QString translationLocation;
#if defined(Q_OS_ANDROID)
	translationLocation = QLatin1Literal("assets:/translations");
#elif defined(Q_OS_IOS)
	translationLocation = QLatin1Literal(":/translations/");
#else
	translationLocation = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
	if (qtTranslator->load(loc, "qt", "_", translationLocation)) {
		application->installTranslator(qtTranslator);
	} else {
		if (verbose && uiLang != "en_US" && uiLang != "en-US")
			qDebug() << "can't find Qt localization for locale" << uiLang << "searching in" << translationLocation;
	}
	ssrfTranslator = new QTranslator;
	if (ssrfTranslator->load(loc, "subsurface", "_") ||
	    ssrfTranslator->load(loc, "subsurface", "_", translationLocation) ||
	    ssrfTranslator->load(loc, "subsurface", "_", getSubsurfaceDataPath("translations")) ||
	    ssrfTranslator->load(loc, "subsurface", "_", getSubsurfaceDataPath("../translations"))) {
		application->installTranslator(ssrfTranslator);
	} else {
		qDebug() << "can't find Subsurface localization for locale" << uiLang;
	}
}
