/*******************************************************************************
* SCHNApps                                                                     *
* Copyright (C) 2015, IGG Group, ICube, University of Strasbourg, France       *
*                                                                              *
* This library is free software; you can redistribute it and/or modify it      *
* under the terms of the GNU Lesser General Public License as published by the *
* Free Software Foundation; either version 2.1 of the License, or (at your     *
* option) any later version.                                                   *
*                                                                              *
* This library is distributed in the hope that it will be useful, but WITHOUT  *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License  *
* for more details.                                                            *
*                                                                              *
* You should have received a copy of the GNU Lesser General Public License     *
* along with this library; if not, write to the Free Software Foundation,      *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.           *
*                                                                              *
* Web site: http://cgogn.unistra.fr/                                           *
* Contact information: cgogn@unistra.fr                                        *
*                                                                              *
*******************************************************************************/

#include <schnapps/core/schnapps.h>

#include <schnapps/core/camera.h>
#include <schnapps/core/view.h>
#include <schnapps/core/plugin.h>
#include <schnapps/core/plugin_interaction.h>
#include <schnapps/core/map_handler.h>

#include <schnapps/core/control_dock_camera_tab.h>
#include <schnapps/core/control_dock_plugin_tab.h>
#include <schnapps/core/control_dock_map_tab.h>

#include <schnapps/core/schnapps_window.h>

#include <QVBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <QDockWidget>
#include <QPluginLoader>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFile>
#include <QByteArray>
#include <QAction>

namespace schnapps
{

SCHNApps::SCHNApps(const QString& app_path, SCHNAppsWindow* window) :
	app_path_(app_path),
	first_view_(nullptr),
	selected_view_(nullptr),
	window_(window)
{
	// create & setup control dock

	control_camera_tab_ = new ControlDock_CameraTab(this);
	window_->control_dock_tab_widget_->addTab(control_camera_tab_, control_camera_tab_->title());
	control_plugin_tab_ = new ControlDock_PluginTab(this);
	window_->control_dock_tab_widget_->addTab(control_plugin_tab_, control_plugin_tab_->title());
	control_map_tab_ = new ControlDock_MapTab(this);
	window_->control_dock_tab_widget_->addTab(control_map_tab_, control_map_tab_->title());

	// create & setup central widget (views)

	root_splitter_ = new QSplitter(window_->centralwidget);
	root_splitter_initialized_ = false;
	window_->central_layout_->addWidget(root_splitter_);

	first_view_ = add_view();
	set_selected_view(first_view_);
	root_splitter_->addWidget(first_view_);

	register_plugins_directory(app_path + QString("/../lib"));
}

SCHNApps::~SCHNApps()
{}

/*********************************************************
 * MANAGE CAMERAS
 *********************************************************/

Camera* SCHNApps::add_camera(const QString& name)
{
	if (cameras_.count(name) > 0ul)
		return nullptr;

	Camera* camera = new Camera(name, this);
	cameras_.insert(std::make_pair(name, camera));
	emit(camera_added(camera));
	return camera;
}

Camera* SCHNApps::add_camera()
{
	return add_camera(QString("camera_") + QString::number(Camera::camera_count_));
}

void SCHNApps::remove_camera(const QString& name)
{
	Camera* camera = get_camera(name);
	if (camera && !camera->is_used())
	{
		cameras_.erase(name);
		emit(camera_removed(camera));
		delete camera;
	}
}

Camera* SCHNApps::get_camera(const QString& name) const
{
	if (cameras_.count(name) > 0ul)
		return cameras_.at(name);
	else
		return nullptr;
}

/*********************************************************
 * MANAGE PLUGINS
 *********************************************************/

void SCHNApps::register_plugins_directory(const QString& path)
{
#ifdef WIN32
#ifdef _DEBUG
	QDir directory(path + QString("Debug/"));
#else
	QDir directory(path + QString("Release/"));
#endif
#else
	QDir directory(path);
#endif
	if (directory.exists())
	{
		QStringList filters;
		filters << "lib*plugin*.so";
		filters << "lib*plugin*.dylib";
		filters << "*plugin*.dll";

		QStringList plugin_files = directory.entryList(filters, QDir::Files);

		for (QString plugin_file : plugin_files)
		{
			QFileInfo pfi(plugin_file);
#ifdef WIN32
			QString plugin_name = pfi.baseName();
#else
			QString plugin_name = pfi.baseName().remove(0, 3);
#endif
			QString plugin_file_path = directory.absoluteFilePath(plugin_file);

			if (!available_plugins_.count(plugin_name) > 0ul)
			{
				available_plugins_.insert(std::make_pair(plugin_name, plugin_file_path));
				emit(plugin_available_added(plugin_name));
			}
		}
	}
}

Plugin* SCHNApps::enable_plugin(const QString& plugin_name)
{
	if (plugins_.count(plugin_name) > 0ul)
		return plugins_.at(plugin_name);

	if (available_plugins_.count(plugin_name) > 0ul)
	{
		QString plugin_file_path = available_plugins_[plugin_name];

		std::cout << plugin_file_path.toStdString() << std::endl;
		QPluginLoader loader(plugin_file_path);

		// if the loader loads a plugin instance
		if (QObject* plugin_object = loader.instance())
		{
			Plugin* plugin = qobject_cast<Plugin*>(plugin_object);

			// set the plugin with correct parameters (name, filepath, SCHNApps)
			plugin->set_name(plugin_name);
			plugin->set_file_path(plugin_file_path);
			plugin->set_schnapps(this);

			// then we call its enable() methods
			if (plugin->enable())
			{
				// if it succeeded we reference this plugin
				plugins_.insert(std::make_pair(plugin_name, plugin));

				status_bar_message(plugin_name + QString(" successfully loaded."), 2000);
				emit(plugin_enabled(plugin));
//				menubar->repaint();
				return plugin;
			}
			else
			{
				delete plugin;
				return nullptr;
			}
		}
		// if loading fails
		else
		{
			std::cout << "loader.instance() failed.." << std::endl;
			return nullptr;
		}
	}
	else
	{
		return nullptr;
	}
}

void SCHNApps::disable_plugin(const QString& plugin_name)
{
	if (plugins_.count(plugin_name) > 0ul)
	{
		Plugin* plugin = plugins_[plugin_name];

		// unlink linked views (for interaction plugins)
		PluginInteraction* pi = dynamic_cast<PluginInteraction*>(plugin);
		if (pi)
		{
			for (View* view : pi->get_linked_views())
				view->unlink_plugin(pi);
		}

		// call disable() method and dereference plugin
		plugin->disable();
		plugins_.erase(plugin_name);

		QPluginLoader loader(plugin->get_file_path());
		loader.unload();

		status_bar_message(plugin_name + QString(" successfully unloaded."), 2000);
		emit(plugin_disabled(plugin));

		delete plugin;
	}
}

Plugin* SCHNApps::get_plugin(const QString& name) const
{
	if (plugins_.count(name) > 0ul)
		return plugins_.at(name);
	else
		return nullptr;
}

void SCHNApps::add_plugin_dock_tab(Plugin* plugin, QWidget* tab_widget, const QString& tab_text)
{
	std::list<QWidget*>& widget_list = plugin_dock_tabs_[plugin];
	if (plugin && tab_widget && std::find(widget_list.begin(), widget_list.end(), tab_widget) == widget_list.end())
	{
		int current_tab = window_->plugin_dock_tab_widget_->currentIndex();

		int idx = window_->plugin_dock_tab_widget_->addTab(tab_widget, tab_text);
		window_->plugin_dock_->setVisible(true);

		PluginInteraction* pi = dynamic_cast<PluginInteraction*>(plugin);
		if (pi)
		{
			if (pi->is_linked_to_view(selected_view_))
				window_->plugin_dock_tab_widget_->setTabEnabled(idx, true);
			else
				window_->plugin_dock_tab_widget_->setTabEnabled(idx, false);
		}

		if (current_tab != -1)
			window_->plugin_dock_tab_widget_->setCurrentIndex(current_tab);

		widget_list.push_back(tab_widget);
	}
}

void SCHNApps::remove_plugin_dock_tab(Plugin* plugin, QWidget *tab_widget)
{
	std::list<QWidget*>& widget_list = plugin_dock_tabs_[plugin];
	auto widget_it = std::find(widget_list.begin(), widget_list.end(), tab_widget);
	if (plugin && tab_widget && widget_it != widget_list.end())
	{
		window_->plugin_dock_tab_widget_->removeTab(window_->plugin_dock_tab_widget_->indexOf(tab_widget));
		widget_list.erase(widget_it);
	}
}

void SCHNApps::enable_plugin_tab_widgets(PluginInteraction* plugin)
{
	if (plugin_dock_tabs_.count(plugin) > 0ul)
	{
		for (const auto& widget : plugin_dock_tabs_.at(plugin))
			window_->plugin_dock_tab_widget_->setTabEnabled(window_->plugin_dock_tab_widget_->indexOf(widget), true);
	}
}

void SCHNApps::disable_plugin_tab_widgets(PluginInteraction* plugin)
{
	if (plugin_dock_tabs_.count(plugin) > 0ul)
	{
		for (const auto& widget : plugin_dock_tabs_.at(plugin))
			window_->plugin_dock_tab_widget_->setTabEnabled(window_->plugin_dock_tab_widget_->indexOf(widget), false);
	}
}

/*********************************************************
 * MANAGE MAPS
 *********************************************************/

MapHandlerGen* SCHNApps::add_map(const QString &name, unsigned int dimension)
{
	QString final_name = name;
	if (maps_.count(name) > 0ul)
	{
		int i = 1;
		do
		{
			final_name = name + QString("_") + QString::number(i);
			++i;
		} while (maps_.count(final_name) > 0ul);
	}

	MapHandlerGen* mh = nullptr;
	switch(dimension)
	{
		case 2 : {
			CMap2* map = new CMap2();
			mh = new MapHandler<CMap2>(final_name, this, map);
			break;
		}
		case 3 : {
			CMap3* map = new CMap3();
			mh = new MapHandler<CMap3>(final_name, this, map);
			break;
		}
	}

	maps_.insert(std::make_pair(final_name, mh));
	emit(map_added(mh));

	return mh;
}

void SCHNApps::remove_map(const QString &name)
{
	if (maps_.count(name) > 0ul)
	{
		MapHandlerGen* map = maps_[name];
		for (View* view : map->get_linked_views())
			view->unlink_map(map);

		maps_.erase(name);
		emit(map_removed(map));

		delete map;
	}
}

MapHandlerGen* SCHNApps::get_map(const QString& name) const
{
	if (maps_.count(name) > 0ul)
		return maps_.at(name);
	else
		return nullptr;
}

MapHandlerGen* SCHNApps::get_selected_map() const
{
	return control_map_tab_->get_selected_map();
}

void SCHNApps::set_selected_map(const QString& name)
{
	control_map_tab_->set_selected_map(name);
}

/*********************************************************
 * MANAGE VIEWS
 *********************************************************/

View* SCHNApps::add_view(const QString& name)
{
	if (views_.count(name) > 0ul)
		return nullptr;

	View* view = new View(name, this);
	views_.insert(std::make_pair(name, view));
	emit(view_added(view));
	return view;
}

View* SCHNApps::add_view()
{
	return add_view(QString("view_") + QString::number(View::view_count_));
}

void SCHNApps::remove_view(const QString& name)
{
	if (views_.count(name) > 0ul)
	{
		if (views_.size() > 1)
		{
			View* view = views_[name];
			if (view == first_view_)
			{
				for (const auto& view_it : views_)
				{
					if (view_it.second != view)
					{
						first_view_ = view_it.second;
						break;
					}
				}
			}
			if (view == selected_view_)
				set_selected_view(first_view_);

			views_.erase(name);
			emit(view_removed(view));
			delete view;
		}
	}
}

View* SCHNApps::get_view(const QString& name) const
{
	if (views_.count(name) > 0ul)
		return views_.at(name);
	else
		return nullptr;
}

void SCHNApps::set_selected_view(View* view)
{
	int current_tab = window_->plugin_dock_tab_widget_->currentIndex();

	if (selected_view_)
	{
		for (PluginInteraction* p : selected_view_->get_linked_plugins())
			disable_plugin_tab_widgets(p);
		disconnect(selected_view_, SIGNAL(plugin_linked(PluginInteraction*)), this, SLOT(enable_plugin_tab_widgets(PluginInteraction*)));
		disconnect(selected_view_, SIGNAL(plugin_unlinked(PluginInteraction*)), this, SLOT(disable_plugin_tab_widgets(PluginInteraction*)));
	}

	View* old_selected = selected_view_;
	selected_view_ = view;
	if (old_selected)
		old_selected->hide_dialogs();

	for (PluginInteraction* p : selected_view_->get_linked_plugins())
		enable_plugin_tab_widgets(p);
	connect(selected_view_, SIGNAL(plugin_linked(PluginInteraction*)), this, SLOT(enable_plugin_tab_widgets(PluginInteraction*)));
	connect(selected_view_, SIGNAL(plugin_unlinked(PluginInteraction*)), this, SLOT(disable_plugin_tab_widgets(PluginInteraction*)));

	window_->plugin_dock_tab_widget_->setCurrentIndex(current_tab);

	emit(selected_view_changed(old_selected, selected_view_));

	if (old_selected)
		old_selected->update();
	selected_view_->update();
}

void SCHNApps::set_selected_view(const QString& name)
{
	View* v = this->get_view(name);
	set_selected_view(v);
}

View* SCHNApps::split_view(const QString& name, Qt::Orientation orientation)
{
	View* new_view = add_view();

	View* view = views_[name];
	QSplitter* parent = static_cast<QSplitter*>(view->parentWidget());

	if (parent == root_splitter_ && !root_splitter_initialized_)
	{
		root_splitter_->setOrientation(orientation);
		root_splitter_initialized_ = true;
	}

	if (parent->orientation() == orientation)
	{
		parent->insertWidget(parent->indexOf(view) + 1, new_view);
		QList<int> sz = parent->sizes();
		int tot = 0;
		for (int i = 0; i < parent->count(); ++i)
			tot += sz[i];
		sz[0] = tot / parent->count() + tot % parent->count();
		for (int i = 1; i < parent->count(); ++i)
			sz[i] = tot / parent->count();
		parent->setSizes(sz);
	}
	else
	{
		int idx = parent->indexOf(view);
		view->setParent(nullptr);
		QSplitter* spl = new QSplitter(orientation);
		spl->addWidget(view);
		spl->addWidget(new_view);
		parent->insertWidget(idx, spl);

		QList<int> sz = spl->sizes();
		int tot = sz[0] + sz[1];
		sz[0] = tot / 2;
		sz[1] = tot - sz[0];
		spl->setSizes(sz);
	}

	return new_view;
}

QString SCHNApps::get_split_view_positions()
{
	QList<QSplitter*> liste;
	liste.push_back(root_splitter_);

	QString result;
	QTextStream qts(&result);
	while (!liste.empty())
	{
		QSplitter* spl = liste.first();
		for (int i = 0; i < spl->count(); ++i)
		{
			QWidget* w = spl->widget(i);
			QSplitter* qw = dynamic_cast<QSplitter*>(w);
			if (qw != nullptr)
				liste.push_back(qw);
		}
		QByteArray ba = spl->saveState();
		qts << ba.count() << " ";
		for (int j = 0; j < ba.count(); ++j)
			qts << int(ba[j]) << " ";
		liste.pop_front();
	}
	return result;
}

void SCHNApps::set_split_view_positions(QString positions)
{
	QList<QSplitter*> liste;
	liste.push_back(root_splitter_);

	QTextStream qts(&positions);
	while (!liste.empty())
	{
		QSplitter* spl = liste.first();
		for (int i = 0; i < spl->count(); ++i)
		{
			QWidget *w = spl->widget(i);
			QSplitter* qw = dynamic_cast<QSplitter*>(w);
			if (qw != nullptr)
				liste.push_back(qw);
		}
		if (qts.atEnd())
		{
			std::cerr << "Problem restoring view split configuration" << std::endl;
			return;
		}

		int nb;
		qts >> nb;
		QByteArray ba(nb + 1, 0);
		for (int j = 0; j < nb; ++j)
		{
			int v;
			qts >> v;
			ba[j] = char(v);
		}
		spl->restoreState(ba);
		liste.pop_front();
	}
}

/*********************************************************
 * MANAGE MENU ACTIONS
 *********************************************************/

QAction* SCHNApps::add_menu_action(Plugin* plugin, const QString& menu_path, const QString& action_text)
{
	QAction* action = nullptr;

	if (!menu_path.isEmpty())
	{
		// extracting all the substring separated by ';'
		QStringList step_names = menu_path.split(";");
		step_names.removeAll("");
		unsigned int nb_steps = step_names.count();

		// if only one substring: error + failure
		// No action directly in the menu bar
		if (nb_steps >= 1)
		{
			unsigned int i = 0;
			QMenu* last_menu = nullptr;
			for (QString step : step_names)
			{
				++i;
				if (i < nb_steps) // if not last substring (= menu)
				{
					// try to find an existing sub_menu with step name
					bool found = false;
					QList<QAction*> actions;
					if (i == 1) actions = window_->menubar->actions();
					else actions = last_menu->actions();
					for (QAction* action : actions)
					{
						QMenu* sub_menu = action->menu();
						if (sub_menu && sub_menu->title() == step)
						{
							last_menu = sub_menu;
							found = true;
							break;
						}
					}
					if (!found)
					{
						QMenu* new_menu;
						if (i == 1)
						{
							new_menu = window_->menubar->addMenu(step);
							new_menu->setParent(window_->menubar);
						}
						else
						{
							new_menu = last_menu->addMenu(step);
							new_menu->setParent(last_menu);
						}
						last_menu = new_menu;
					}
				}
				else // if last substring (= action name)
				{
					action = new QAction(action_text, last_menu);
					last_menu->addAction(action);
					action->setText(step);
					plugin_menu_actions_[plugin].push_back(action);

					// just to update the menu in buggy Qt5 on macOS
//					#if (defined CGOGN_APPLE) && ((QT_VERSION>>16) == 5)
//					QMenu* fakemenu = window_->menubar->addMenu("X");
//					delete fakemenu;
//					#endif
				}
			}
		}
	}

	return action;
}

void SCHNApps::remove_menu_action(Plugin* plugin, QAction* action)
{
	std::list<QAction*>& action_list = plugin_menu_actions_[plugin];
	auto action_it = std::find(action_list.begin(), action_list.end(), action);
	if (action_it != action_list.end())
	{
		action->setEnabled(false);

		action_list.erase(action_it);

		// parent of the action
		// which is an instance of QMenu if the action was created
		// using the add_menu_action() method
		QObject* parent = action->parent();
		delete action;

		while(parent != nullptr)
		{
			QMenu* parent_menu = dynamic_cast<QMenu*>(parent);
			if (parent_menu && parent_menu->actions().empty())
			{
				parent = parent->parent();
				delete parent_menu;
			}
			else
				parent = nullptr;
		}
	}
}

/*********************************************************
 * MANAGE WINDOW
 *********************************************************/

void SCHNApps::close_window()
{
	window_->close();
}

void SCHNApps::schnapps_window_closing()
{
	emit(schnapps_closing());
}

void SCHNApps::status_bar_message(const QString& msg, int msec)
{
	window_->statusbar->showMessage(msg, msec);
}

void SCHNApps::set_window_size(int w, int h)
{
	window_->resize(w, h);
}

} // namespace schnapps