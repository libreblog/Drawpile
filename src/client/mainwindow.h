/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2006-2009 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "tools.h"

class QActionGroup;
class QMessageBox;
class QUrl;
class QLabel;
class QSplitter;
class QTemporaryFile;
class QTimer;

namespace widgets {
	class ViewStatus;
	class NetStatus;
	class CanvasView;
	class DualColorButton;
	class ToolSettings;
	class UserList;
	class LayerListWidget;
	class ChatBox;
	class PaletteBox;
	class ColorBox;
	class Navigator;
}
namespace dialogs {
	class ColorDialog;
	class HostDialog;
	class JoinDialog;
}
namespace drawingboard {
	class CanvasScene;
}

namespace net {
	class Client;
}

class SessionLoader;

//! The application main window
class MainWindow : public QMainWindow {
	Q_OBJECT
	public:
		MainWindow(const MainWindow *source=0);
		~MainWindow();

		//! Load initial session state
		bool loadDocument(SessionLoader &loader);

		//! Connect to a host and join a session if full URL is provided.
		void joinSession(const QUrl& url);

		//! React to eraser tip proximity
		void eraserNear(bool near);

	public slots:
		//! Show the "new file" dialog
		void showNew();
		//! Open the file
		void open(const QString& file);
		//! Show file open dialog
		void open();
		//! Save current document
		bool save();
		//! Save current document with a new name
		bool saveas();
		//! Show settings dialog
		void showSettings();
		//! Show host session dialog
		void host();
		//! Show join session dialog
		void join();
		//! Leave session (ask confirmation first)
		void leave();
		//! Zoom in
		void zoomin();
		//! Zoom out
		void zoomout();
		//! Reset to 1:1 zoom
		void zoomone();
		//! Reset rotation
		void rotatezero();
		//! Toggle fullscreen mode
		void fullscreen(bool enable);
		//! Change current tool
		void selectTool(QAction *tool);
		//! Display about dialog
		void about();
		//! Go to drawpile homepage
		void homepage();
	private slots:
		//! Show a color dialog and set foreground color
		void setForegroundColor();
		//! Show a color dialog and set background color
		void setBackgroundColor();
		//! Set session title
		void setSessionTitle(const QString& title);
		//! Create new document
		void newDocument(const QSize &size, const QColor &color);
		//! Open a recent file
		void openRecent(QAction *action);
		//! Mark unsaved changes
		void markUnsaved();
		//! Cancel or start hosting
		void finishHost(int i);
		//! Cancel or join
		void finishJoin(int i);
		//! Leave session
		void finishLeave(int i);
		//! Connection established
		void connected();
		//! Connection cut
		void disconnected();
		//! Joined a session
		void joined();
		//! Disallow changes to the board
		void lock(const QString& reason);
		//! Allow changes to the board
		void unlock();
		//! Board settings changed
		void boardInfoChanged();
		//! Toggle annotations
		void toggleAnnotations(bool hidden);

		//! Update keyboard shortcuts for all main windows
		void updateShortcuts();

	signals:
		//! This signal is emitted when the current tool is changed
		void toolChanged(tools::Type);

	protected:
		//! Handle closing of the main window
		void closeEvent(QCloseEvent *event);

	private:
		//! Confirm saving of image in a format that doesn't support all required features
		bool confirmFlatten(QString& file) const;

		//! Utility function for creating actions
		QAction *makeAction(const char *name, const char *icon, const QString& text, const QString& tip = QString(), const QKeySequence& shortcut = QKeySequence());

		//! Load customized shortcuts
		void loadShortcuts();

		//! Check if the current board can be replaced
		bool canReplace() const;

		//! Add a new entry to recent files list
		void addRecentFile(const QString& file);

		//! Set the window title according to open file name
		void setTitle();

		//! Save settings and exit
		void exit();

		//! Display an error message
		void showErrorMessage(const QString& message, const QString& details=QString());

		//! Read settings from file/registry
		void readSettings();
		//! Clone settings from another MainWindow
		void cloneSettings(const MainWindow *source);
		//! Write settings to file/reqistry
		void writeSettings();

		//! Initialise QActions
		void initActions();
		//! Create menus
		void createMenus();
		//! Create toolbars
		void createToolbars();
		//! Create all dock windows
		void createDocks();
		//! Create tool settings dock
		void createToolSettings(QMenu *menu);
		//! Create user list dock
		void createUserList(QMenu *menu);
		//! Create layer list dock
		void createLayerList(QMenu *menu);
		//! Create palette dock
		void createPalette(QMenu *menu);
		//! Create color docks
		void createColorBoxes(QMenu *menu);
		//! Create navigator dock
		void createNavigator(QMenu *menu);

		QSplitter *splitter_;
		widgets::ToolSettingsDock *_toolsettings;
		widgets::UserList *userlist_;
		widgets::LayerListWidget *_layerlist;
		widgets::ChatBox *chatbox_;

		widgets::DualColorButton *fgbgcolor_;
		widgets::ViewStatus *viewstatus_;
		widgets::NetStatus *netstatus_;
		widgets::CanvasView *_view;
		widgets::PaletteBox *palette_;
		widgets::ColorBox *rgb_, *hsv_;
		widgets::Navigator *navigator_;
		QLabel *lockstatus_;

		dialogs::ColorDialog *fgdialog_,*bgdialog_;
		dialogs::HostDialog *hostdlg_;
		dialogs::JoinDialog *joindlg_;

		drawingboard::CanvasScene *_canvas;
		net::Client *_client;

		QString sessiontitle_;
		QString filename_;
		QString lastpath_;

		QAction *new_;
		QAction *open_;
		QMenu *recent_;
		QAction *save_;
		QAction *saveas_;
		QAction *quit_;

		QAction *host_;
		QAction *join_;
		QAction *logout_;

		QActionGroup *adminTools_;
		QAction *lock_board;
		QAction *disallowjoins_;

		QActionGroup *drawingtools_;
		QAction *pentool_;
		QAction *brushtool_;
		QAction *erasertool_;
		QAction *pickertool_;
		QAction *linetool_;
		QAction *recttool_;
		QAction *annotationtool_;

		QAction *lasttool_; // the last used tool

		QAction *zoomin_;
		QAction *zoomout_;
		QAction *zoomorig_;
		QAction *rotateorig_;
		QAction *fullscreen_;
		QAction *settings_;

		QAction *toggleoutline_;
		QAction *hideannotations_;
		QAction *swapcolors_;

		QAction *toolbartoggles_;
		QAction *docktoggles_;

		QAction *homepage_;
		QAction *about_;

		// List of customizeable actions
		QList<QAction*> customacts_;
};

#endif

