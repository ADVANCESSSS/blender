/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_MeshObject.h
 *  \ingroup bgerast
 */

#ifndef __RAS_MESHOBJECT_H__
#define __RAS_MESHOBJECT_H__

#ifdef _MSC_VER
/* disable the STL warnings ("debug information length > 255") */
#  pragma warning (disable:4786)
#endif

#include <vector>
#include <list>
#include <string>

#include "RAS_MeshMaterial.h"
#include "MT_Transform.h"

class RAS_MaterialBucket;
class RAS_MeshUser;
class RAS_Deformer;
class RAS_IVertex;
class RAS_BoundingBox;
class RAS_BoundingBoxManager;
struct Mesh;

/* RAS_MeshObject is a mesh used for rendering. It stores polygons,
 * but the actual vertices and index arrays are stored in material
 * buckets, referenced by the list of RAS_MeshMaterials. */

class RAS_MeshObject
{
public:
	/** Additionals data stored in mesh layers. These datas can be the colors layer or the
	 * UV map layers. They are used to find attribute's layers index by looking for similar
	 * attribute's names in shader and names of the mesh layers here.
	 */
	struct Layer {
		/// The type of the layer, uv or color.
		enum Type {
			UV,
			COLOR
		} type;
		/// The index of the color or uv layer in the vertices.
		unsigned short index;
		/// The name of the color or uv layer used to find corresponding material attributes.
		std::string name;
	};

	typedef std::vector<Layer> LayerList;

	struct LayersInfo {
		LayerList layers;
		// The active color layer index as default.
		unsigned short activeColor;
		// The active uv layer index as default.
		unsigned short activeUv;
		// The number of uv layers.
		unsigned short uvCount;
		// The number of color layers.
		unsigned short colorCount;
	};

	/** Polygon info generate when getting a polygon through
	 * RAS_MeshObject::GetPolygon. */
	struct PolygonInfo {
		enum Flags {
			NONE = 0,
			VISIBLE = (1 << 0),
			COLLIDER = (1 << 1),
			TWOSIDE = (1 << 2)
		};

		/// Display array owning the polygon, used to get vertices.
		RAS_IDisplayArray *array;
		/// Polygon vertices indices in the display array.
		unsigned int indices[3];
		/// Polygon flags depending on material using this display array.
		Flags flags;
		/// Material index owning the display array of this polygon.
		unsigned short matId;
	};

private:
	/** Polygon info per range depending of display array stored to generate
	 * the individual polygon info. */
	struct PolygonRangeInfo {
		/// Display array owning polygons for this index range.
		RAS_IDisplayArray *array;
		/// Start absolute vertex index of the range.
		unsigned int startIndex;
		/// End absolute vertex index of the range.
		unsigned int endIndex;
		/// Polygon flags depending on material using this display array.
		PolygonInfo::Flags flags;
		/// Material index owning the display array of this polygon range.
		unsigned short matId;
	};

	std::vector<PolygonRangeInfo> m_polygonRanges;
	unsigned int m_numPolygons;

	std::string m_name;

	LayersInfo m_layersInfo;

	/// The mesh bounding box.
	RAS_BoundingBox *m_boundingBox;

protected:
	RAS_MeshMaterialList m_materials;
	Mesh *m_mesh;

public:
	// for now, meshes need to be in a certain layer (to avoid sorting on lights in realtime)
	RAS_MeshObject(Mesh *mesh, const LayersInfo& layersInfo);
	virtual ~RAS_MeshObject();

	// materials
	unsigned short GetNumMaterials() const;
	std::string GetMaterialName(unsigned int matid) const;
	std::string GetTextureName(unsigned int matid) const;

	const RAS_MeshMaterialList& GetMeshMaterialList() const;
	RAS_MeshMaterial *GetMeshMaterial(unsigned int matid) const;
	RAS_MeshMaterial *GetMeshMaterialBlenderIndex(unsigned int index) const;
	RAS_MeshMaterial *FindMaterialName(const std::string& name) const;

	// name
	const std::string& GetName() const;

	// original blender mesh
	Mesh *GetMesh()
	{
		return m_mesh;
	}

	// mesh construction
	RAS_MeshMaterial *AddMaterial(RAS_MaterialBucket *bucket, unsigned int index, const RAS_VertexFormat& format);

	RAS_IDisplayArray *GetDisplayArray(unsigned int matid) const;
	RAS_IVertex *GetVertex(unsigned int matid, unsigned int index) const;

	unsigned int GetNumPolygons() const;
	PolygonInfo GetPolygon(unsigned int index) const;

	RAS_BoundingBox *GetBoundingBox() const;
	// buckets
	RAS_MeshUser *AddMeshUser(void *clientobj, RAS_Deformer *deformer);

	void EndConversion(RAS_BoundingBoxManager *boundingBoxManager);

	/// Return the list of blender's layers.
	const LayersInfo& GetLayersInfo() const;

	/** Generate attribute's layers for every material use by this mesh.
	 * WARNING: Always call when shader in the material are valid.
	 */
	void GenerateAttribLayers();
};

#endif  // __RAS_MESHOBJECT_H__
