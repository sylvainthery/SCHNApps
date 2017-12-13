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

#include <schnapps/plugins/surface_deformation/surface_deformation.h>
#include <schnapps/plugins/surface_deformation/surface_deformation_dock_tab.h>

#include <schnapps/core/schnapps.h>
#include <schnapps/core/view.h>
#include <schnapps/core/camera.h>

#include <cgogn/geometry/algos/angle.h>
#include <cgogn/geometry/algos/area.h>

namespace schnapps
{

namespace plugin_surface_deformation
{

Plugin_SurfaceDeformation::Plugin_SurfaceDeformation() :
	drag_init_(false),
	dragging_(false)
{
	this->name_ = SCHNAPPS_PLUGIN_NAME;
}

QString Plugin_SurfaceDeformation::plugin_name()
{
	return SCHNAPPS_PLUGIN_NAME;
}

MapParameters& Plugin_SurfaceDeformation::get_parameters(MapHandlerGen* map)
{
	cgogn_message_assert(map, "Try to access parameters for null map");
	cgogn_message_assert(map->dimension() == 2, "Try to access parameters for map with dimension other than 2");

	if (parameter_set_.count(map) == 0)
	{
		MapParameters& p = parameter_set_[map];
		p.map_ = static_cast<CMap2Handler*>(map);
		p.working_cells_ = cgogn::make_unique<CMap2::CellCache>(*p.map_->get_map());
		return p;
	}
	else
		return parameter_set_[map];
}

bool Plugin_SurfaceDeformation::check_docktab_activation()
{
	MapHandlerGen* map = schnapps_->get_selected_map();
	View* view = schnapps_->get_selected_view();

	if (view && view->is_linked_to_plugin(this) && map && map->is_linked_to_view(view) && map->dimension() == 2)
	{
		schnapps_->enable_plugin_tab_widgets(this);
		return true;
	}
	else
	{
		schnapps_->disable_plugin_tab_widgets(this);
		return false;
	}
}

bool Plugin_SurfaceDeformation::enable()
{
	if (get_setting("Auto load position attribute").isValid())
		setting_auto_load_position_attribute_ = get_setting("Auto load position attribute").toString();
	else
		setting_auto_load_position_attribute_ = add_setting("Auto load position attribute", "position").toString();

	dock_tab_ = new SurfaceDeformation_DockTab(this->schnapps_, this);
	schnapps_->add_plugin_dock_tab(this, dock_tab_, "Surface deformation");

	return true;
}

void Plugin_SurfaceDeformation::disable()
{
	schnapps_->remove_plugin_dock_tab(this, dock_tab_);
	delete dock_tab_;
}

bool Plugin_SurfaceDeformation::keyPress(View* view, QKeyEvent* event)
{
	switch (event->key())
	{
		case Qt::Key_D: {
			MapHandlerGen* mhg = schnapps_->get_selected_map();
			if (mhg && mhg->dimension() == 2)
			{
				const MapParameters& p = get_parameters(mhg);
				if (!dragging_ && p.get_handle_vertex_set() && p.get_handle_vertex_set()->get_nb_cells() > 0)
					start_dragging(view);
				else
					stop_dragging(view);
			}
			break;
		}

		case Qt::Key_R : {
			MapHandlerGen* mhg = schnapps_->get_selected_map();
			if (mhg && mhg->dimension() == 2)
			{
				const MapParameters& p = get_parameters(mhg);
				if (p.initialized_)
				{
					view->get_current_camera()->disable_views_bb_fitting();
					as_rigid_as_possible(mhg);
					mhg->notify_attribute_change(CMap2::Vertex::ORBIT, p.get_position_attribute_name());

					for (View* view : mhg->get_linked_views())
						view->update();
					view->get_current_camera()->enable_views_bb_fitting();
				}
			}
			break;
		}
	}

	return true;
}

void Plugin_SurfaceDeformation::start_dragging(View* view)
{
	dragging_ = true;
	drag_init_ = false;
	view->setMouseTracking(true);
	view->get_current_camera()->disable_views_bb_fitting();
}

void Plugin_SurfaceDeformation::stop_dragging(View* view)
{
	dragging_ = false;
	drag_init_ = false;
	view->setMouseTracking(false);
	view->get_current_camera()->enable_views_bb_fitting();
}

bool Plugin_SurfaceDeformation::mouseMove(View* view, QMouseEvent* event)
{
	if (dragging_)
	{
		MapHandlerGen* mhg = schnapps_->get_selected_map();
		if (mhg && mhg->dimension() == 2)
		{
			MapParameters& p = get_parameters(mhg);
			if (!drag_init_)
			{
				drag_z_ = 0;
				p.handle_vertex_set_->foreach_cell([&] (CMap2::Vertex v)
				{
					const VEC3& pp = p.position_[v];
					qoglviewer::Vec q = view->camera()->projectedCoordinatesOf(qoglviewer::Vec(pp[0],pp[1],pp[2]));
					drag_z_ += q.z;
				});
				drag_z_ /= p.handle_vertex_set_->get_nb_cells();

				qoglviewer::Vec pp(event->x(), event->y(), drag_z_);
				drag_previous_ = view->camera()->unprojectedCoordinatesOf(pp);

				drag_init_ = true;
			}
			else
			{
				if (p.initialized_)
				{
					qoglviewer::Vec pp(event->x(), event->y(), drag_z_);
					qoglviewer::Vec qq = view->camera()->unprojectedCoordinatesOf(pp);

					qoglviewer::Vec vec = qq - drag_previous_;
					VEC3 t(vec.x, vec.y, vec.z);
					p.handle_vertex_set_->foreach_cell([&] (CMap2::Vertex v)
					{
						p.position_[v] += t;
					});

					drag_previous_ = qq;

					if (as_rigid_as_possible(mhg))
					{
						mhg->notify_attribute_change(CMap2::Vertex::ORBIT, p.get_position_attribute_name());

						for (View* view : mhg->get_linked_views())
							view->update();
					}
					else
					{
						// undo handle displacement & stop dragging
						p.handle_vertex_set_->foreach_cell([&] (CMap2::Vertex v)
						{
							p.position_[v] -= t;
						});
						stop_dragging(view);
					}
				}
			}
		}
	}

	return true;
}

void Plugin_SurfaceDeformation::view_linked(View* view)
{
	if (check_docktab_activation())
		dock_tab_->refresh_ui();

	connect(view, SIGNAL(map_linked(MapHandlerGen*)), this, SLOT(map_linked(MapHandlerGen*)));
	connect(view, SIGNAL(map_unlinked(MapHandlerGen*)), this, SLOT(map_unlinked(MapHandlerGen*)));

	for (MapHandlerGen* map : view->get_linked_maps()) { add_linked_map(view, map); }
}

void Plugin_SurfaceDeformation::view_unlinked(View* view)
{
	if (check_docktab_activation())
		dock_tab_->refresh_ui();

	disconnect(view, SIGNAL(map_linked(MapHandlerGen*)), this, SLOT(map_linked(MapHandlerGen*)));
	disconnect(view, SIGNAL(map_unlinked(MapHandlerGen*)), this, SLOT(map_unlinked(MapHandlerGen*)));

	for (MapHandlerGen* map : view->get_linked_maps()) { remove_linked_map(view, map); }
}

void Plugin_SurfaceDeformation::map_linked(MapHandlerGen *map)
{
	View* view = static_cast<View*>(sender());
	add_linked_map(view, map);
}

void Plugin_SurfaceDeformation::add_linked_map(View* view, MapHandlerGen* map)
{
	MapParameters& p = get_parameters(map);
	p.set_position_attribute(setting_auto_load_position_attribute_);

	connect(map, SIGNAL(attribute_added(cgogn::Orbit, const QString&)), this, SLOT(linked_map_attribute_added(cgogn::Orbit, const QString&)), Qt::UniqueConnection);
	connect(map, SIGNAL(attribute_removed(cgogn::Orbit, const QString&)), this, SLOT(linked_map_attribute_removed(cgogn::Orbit, const QString&)), Qt::UniqueConnection);
	connect(map, SIGNAL(cells_set_removed(CellType, const QString&)), this, SLOT(linked_map_cells_set_removed(CellType, const QString&)), Qt::UniqueConnection);

	if (check_docktab_activation())
		dock_tab_->refresh_ui();
}

void Plugin_SurfaceDeformation::map_unlinked(MapHandlerGen *map)
{
	View* view = static_cast<View*>(sender());
	remove_linked_map(view, map);
}

void Plugin_SurfaceDeformation::remove_linked_map(View* view, MapHandlerGen* map)
{
	disconnect(map, SIGNAL(attribute_added(cgogn::Orbit, const QString&)), this, SLOT(linked_map_attribute_added(cgogn::Orbit, const QString&)));
	disconnect(map, SIGNAL(attribute_removed(cgogn::Orbit, const QString&)), this, SLOT(linked_map_attribute_removed(cgogn::Orbit, const QString&)));
	disconnect(map, SIGNAL(cells_set_removed(CellType, const QString&)), this, SLOT(linked_map_cells_set_removed(CellType, const QString&)));

	if (check_docktab_activation())
		dock_tab_->refresh_ui();
}

void Plugin_SurfaceDeformation::linked_map_attribute_added(cgogn::Orbit orbit, const QString& name)
{
	if (orbit == CMap2::Vertex::ORBIT)
	{
		MapHandlerGen* map = static_cast<MapHandlerGen*>(sender());

		if (parameter_set_.count(map) > 0ul)
		{
			MapParameters& p = parameter_set_[map];
			if (!p.get_position_attribute().is_valid() && name == setting_auto_load_position_attribute_)
				set_position_attribute(map, name, true);
		}

		for (View* view : map->get_linked_views())
			view->update();
	}
}

void Plugin_SurfaceDeformation::linked_map_attribute_removed(cgogn::Orbit orbit, const QString& name)
{
	if (orbit == CMap2::Vertex::ORBIT)
	{
		MapHandlerGen* map = static_cast<MapHandlerGen*>(sender());

		if (parameter_set_.count(map) > 0ul)
		{
			MapParameters& p = parameter_set_[map];
			if (p.get_position_attribute_name() == name)
				set_position_attribute(map, "", true);
		}

		for (View* view : map->get_linked_views())
			view->update();
	}
}

void Plugin_SurfaceDeformation::linked_map_cells_set_removed(CellType ct, const QString& name)
{
	if (ct == Vertex_Cell)
	{
		MapHandlerGen* map = static_cast<MapHandlerGen*>(sender());

		if (parameter_set_.count(map) > 0ul)
		{
			MapParameters& p = parameter_set_[map];
			CellsSetGen* fvs = p.get_free_vertex_set();
			if (fvs && fvs->get_name() == name)
				set_free_vertex_set(map, nullptr, true);
			CellsSetGen* hvs = p.get_handle_vertex_set();
			if (hvs && hvs->get_name() == name)
				set_handle_vertex_set(map, nullptr, true);
		}

		for (View* view : map->get_linked_views())
			view->update();
	}
}

/******************************************************************************/
/*                             PUBLIC INTERFACE                               */
/******************************************************************************/

void Plugin_SurfaceDeformation::set_position_attribute(MapHandlerGen* map, const QString& name, bool update_dock_tab)
{
	if (map && map->dimension() == 2)
	{
		MapParameters& p = get_parameters(map);
		bool success = p.set_position_attribute(name);
		if (update_dock_tab && map->is_selected_map())
		{
			if (success)
				dock_tab_->set_position_attribute(name);
			else
				dock_tab_->set_position_attribute("");
		}
		stop(map, true);
	}
}

void Plugin_SurfaceDeformation::set_free_vertex_set(MapHandlerGen* map, CellsSetGen* cs, bool update_dock_tab)
{
	if (map && map->dimension() == 2)
	{
		MapParameters& p = get_parameters(map);
		bool success = p.set_free_vertex_set(cs);
		if (update_dock_tab && map->is_selected_map())
		{
			if (success)
				dock_tab_->set_free_vertex_set(cs);
			else
				dock_tab_->set_free_vertex_set(nullptr);
		}
		stop(map, true);
	}
}

void Plugin_SurfaceDeformation::set_handle_vertex_set(MapHandlerGen* map, CellsSetGen* cs, bool update_dock_tab)
{
	if (map && map->dimension() == 2)
	{
		MapParameters& p = get_parameters(map);
		bool success = p.set_handle_vertex_set(cs);
		if (update_dock_tab && map->is_selected_map())
		{
			if (success)
				dock_tab_->set_handle_vertex_set(cs);
			else
				dock_tab_->set_handle_vertex_set(nullptr);
		}
		stop(map, true);
	}
}

void Plugin_SurfaceDeformation::initialize(MapHandlerGen* map, bool update_dock_tab)
{
	if (map && map->dimension() == 2)
	{
		MapParameters& p = get_parameters(map);
		if (!p.initialized_)
		{
			p.initialize();
			if (update_dock_tab && map->is_selected_map())
				dock_tab_->set_deformation_initialized(p.initialized_);
		}
	}
}

void Plugin_SurfaceDeformation::stop(MapHandlerGen* map, bool update_dock_tab)
{
	if (map && map->dimension() == 2)
	{
		MapParameters& p = get_parameters(map);
		if (p.initialized_)
		{
			p.stop();
			if (update_dock_tab && map->is_selected_map())
				dock_tab_->set_deformation_initialized(p.initialized_);
		}
	}
}

bool Plugin_SurfaceDeformation::as_rigid_as_possible(MapHandlerGen* map)
{
	if (map && map->dimension() == 2)
	{
		MapParameters& p = get_parameters(map);
		if (p.initialized_)
		{
			if (!p.solver_ready_)
				if (!p.build_solver())
					return false;

			CMap2* map2 = p.map_->get_map();

			auto compute_rotation_matrix = [&] (CMap2::Vertex v)
			{
				MAT33 cov;
				cov.setZero();
				const VEC3& pos = p.position_[v];
				const VEC3& pos_i = p.position_init_[v];
				map2->foreach_adjacent_vertex_through_edge(v, [&] (CMap2::Vertex av)
				{
					VEC3 vec = p.position_[av] - pos;
					VEC3 vec_i = p.position_init_[av] - pos_i;
					for (uint32 i = 0; i < 3; ++i)
						for (uint32 j = 0; j < 3; ++j)
							cov(i,j) += vec[i] * vec_i[j];
				});
				Eigen::JacobiSVD<MAT33> svd(cov, Eigen::ComputeFullU | Eigen::ComputeFullV);
				MAT33 R = svd.matrixU() * svd.matrixV().transpose();
				if (R.determinant() < 0)
				{
					MAT33 U = svd.matrixU();
					for (uint32 i = 0; i < 3; ++i)
						U(i,2) *= -1;
					R = U * svd.matrixV().transpose();
				}
				p.vertex_rotation_matrix_[v] = R;
			};
			map2->parallel_foreach_cell(compute_rotation_matrix, *p.working_cells_);

			auto compute_rotated_diff_coord = [&] (CMap2::Vertex v)
			{
				uint32 degree = 0;
				MAT33 r;
				r.setZero();
				map2->foreach_adjacent_vertex_through_edge(v, [&] (CMap2::Vertex av)
				{
					r += p.vertex_rotation_matrix_[av];
					++degree;
				});
				r += p.vertex_rotation_matrix_[v];
				r /= degree + 1;
				p.rotated_diff_coord_[v] = r * p.diff_coord_[v];
			};
			map2->parallel_foreach_cell(compute_rotated_diff_coord, *p.working_cells_);

			uint32 nb_vertices = p.working_cells_->size<CMap2::Vertex>();

			Eigen::MatrixXd rdiff(nb_vertices, 3);
			map2->parallel_foreach_cell(
				[&] (CMap2::Vertex v)
				{
					const VEC3& rdcv = p.rotated_diff_coord_[v];
					rdiff(p.v_index_[v], 0) = rdcv[0];
					rdiff(p.v_index_[v], 1) = rdcv[1];
					rdiff(p.v_index_[v], 2) = rdcv[2];
				},
				*p.working_cells_
			);
			Eigen::MatrixXd rbdiff(nb_vertices, 3);
			rbdiff = p.working_LAPL_ * rdiff;
			map2->parallel_foreach_cell(
				[&] (CMap2::Vertex v)
				{
					VEC3& rbdcv = p.rotated_bi_diff_coord_[v];
					rbdcv[0] = rbdiff(p.v_index_[v], 0);
					rbdcv[1] = rbdiff(p.v_index_[v], 1);
					rbdcv[2] = rbdiff(p.v_index_[v], 2);
				},
				*p.working_cells_
			);

			Eigen::MatrixXd x(nb_vertices, 3);
			Eigen::MatrixXd b(nb_vertices, 3);

			map2->parallel_foreach_cell(
				[&] (CMap2::Vertex v)
				{
					uint32 vidx = p.v_index_[v];
					if (p.free_vertex_set_->is_selected(v))
					{
						const VEC3& rbdc = p.rotated_bi_diff_coord_[v];
						b.coeffRef(vidx, 0) = rbdc[0];
						b.coeffRef(vidx, 1) = rbdc[1];
						b.coeffRef(vidx, 2) = rbdc[2];
					}
					else
					{
						const VEC3& pos = p.position_[v];
						b.coeffRef(vidx, 0) = pos[0];
						b.coeffRef(vidx, 1) = pos[1];
						b.coeffRef(vidx, 2) = pos[2];
					}
				},
				*p.working_cells_
			);

			x = p.solver_->solve(b);

			map2->parallel_foreach_cell(
				[&] (CMap2::Vertex v)
				{
					uint32 vidx = p.v_index_[v];
					VEC3& pos = p.position_[v];
					pos[0] = x(vidx, 0);
					pos[1] = x(vidx, 1);
					pos[2] = x(vidx, 2);
				},
				*p.working_cells_
			);

			return true;
		}
		else
		{
			cgogn_log_warning("surface_deformation") << "initial state not initialized";
			return false;
		}
	}

	cgogn_log_warning("surface_deformation") << "wrong map dimension";
	return false;
}

} // namespace plugin_surface_deformation

} // namespace schnapps