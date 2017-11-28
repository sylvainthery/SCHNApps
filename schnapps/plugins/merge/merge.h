/*******************************************************************************
* SCHNApps                                                                     *
* Copyright (C) 2016, IGG Group, ICube, University of Strasbourg, France       *
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

#ifndef SCHNAPPS_PLUGIN_MERGE_PLUGIN_H_
#define SCHNAPPS_PLUGIN_MERGE_PLUGIN_H_

#include "dll.h"
#include <schnapps/core/plugin_processing.h>

#include <QAction>

namespace schnapps
{

class MapHandlerGen;

namespace plugin_merge
{

class MergeDialog;

/**
* @brief Merge plugin
*/
class SCHNAPPS_PLUGIN_MERGE_PLUGIN_API Plugin_Merge : public PluginProcessing
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "SCHNApps.Plugin")
	Q_INTERFACES(schnapps::Plugin)

public:

	Plugin_Merge();
	~Plugin_Merge() override;

	static QString plugin_name();

private:

	bool enable() override;
	void disable() override;

private slots:

	// slots called from SCHNApps signals
	void schnapps_closing();

	// slots called from action signals
	void merge_dialog();

public slots:

	/**
	 * @brief merge second_map into first_map
	 */
	bool merge(MapHandlerGen* first_map, const MapHandlerGen* second_map);

private:

	MergeDialog* merge_dialog_;
	QAction* merge_action_;
};

} // namespace plugin_merge

} // namespace schnapps

#endif // SCHNAPPS_PLUGIN_MERGE_PLUGIN_H_
