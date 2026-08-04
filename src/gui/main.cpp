// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "main_window.h"
#include "app_define.h"
#include <QApplication>

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	app.setApplicationName(APPNAME);
	app.setApplicationVersion(APP_VERSION);
	app.setOrganizationName("LGTV Companion");
	app.setDesktopFileName(APP_ID);

	MainWindow window;
	window.show();
	return app.exec();
}
